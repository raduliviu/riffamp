// webamp-helper: the native audio engine, remote-controlled over ws://127.0.0.1.
//
//   webamp-helper --assets F:\codework\webamp\assets [--port 43717] [--api asio]
//                 [--buffer 64] [--sr 48000] [--in-ch 2]
//
// Chain: input gain -> gate -> NAM -> tone stack -> cab IR -> output gain.
// All parameters are atomics read by the audio callback; model/IR swaps happen
// on the control thread with an atomic pointer exchange + grace delete.
//
// Protocol (JSON text frames):
//   -> {"type":"hello"}
//   -> {"type":"setParam","id":"gainIn|gate|bass|mid|treble|gainOut|mute","value":N}
//   -> {"type":"setModel","name":"<from state.models>"}
//   -> {"type":"setIr","name":"<from state.irs>"}
//   -> {"type":"panic"}
//   <- {"type":"state",...}   (on hello and after every change)
//   <- {"type":"meters","in":N,"out":N}   (~15 Hz)
//   <- {"type":"error","message":"..."}
//
// Security: binds 127.0.0.1 only; Origin allowlist (localhost/127.0.0.1 pages
// or no Origin for native tools). Pairing codes: TODO with the real web UI (P2c).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "portaudio.h"
#include "pa_win_wasapi.h"
#include "NAM/get_dsp.h"
#include "TwoStageFFTConvolver.h"
#include "json.hpp"
#include "dsp_extra.h"

#include <ixwebsocket/IXHttpServer.h>
#include <ixwebsocket/IXWebSocketServer.h>

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include <windows.h>
#include <shellapi.h>
#include "web_ui.h"  // generated: kWebUiHtml[], kWebUiHtml_len

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr const char* kVersion = "0.2.0";
constexpr const char* kDeviceMatch = "komplete";
constexpr size_t kTailBlock = 1024;
constexpr int kUiPort = 43718;
constexpr const char* kUiUrl = "http://127.0.0.1:43718";

std::atomic<bool> gRunning{true};
std::function<void()> gToggleMute;  // set in main; called from the tray thread

// ---- tray icon ---------------------------------------------------------------
constexpr UINT kTrayMsg = WM_APP + 1;
constexpr UINT kCmdOpen = 1, kCmdMute = 2, kCmdQuit = 3;

LRESULT CALLBACK trayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kTrayMsg) {
        if (lp == WM_LBUTTONDBLCLK) ShellExecuteA(nullptr, "open", kUiUrl, nullptr, nullptr, SW_SHOWNORMAL);
        else if (lp == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuA(menu, MF_STRING, kCmdOpen, "Open webamp");
            AppendMenuA(menu, MF_STRING, kCmdMute, "Toggle mute");
            AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(menu, MF_STRING, kCmdQuit, "Quit");
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);  // required or the menu won't dismiss
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
        }
        return 0;
    }
    if (msg == WM_COMMAND) {
        switch (LOWORD(wp)) {
            case kCmdOpen: ShellExecuteA(nullptr, "open", kUiUrl, nullptr, nullptr, SW_SHOWNORMAL); break;
            case kCmdMute: if (gToggleMute) gToggleMute(); break;
            case kCmdQuit: gRunning.store(false); PostQuitMessage(0); break;
        }
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

void trayThread() {
    WNDCLASSA wc{};
    wc.lpfnWndProc = trayWndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "webampTray";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("webampTray", "", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                              wc.hInstance, nullptr);
    NOTIFYICONDATAA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kTrayMsg;
    nid.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    lstrcpynA(nid.szTip, "webamp engine — double-click to open", sizeof(nid.szTip));
    Shell_NotifyIconA(NIM_ADD, &nid);

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0 && gRunning.load()) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    Shell_NotifyIconA(NIM_DELETE, &nid);
}

fs::path exeDir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
}

struct Options {
    std::string assets;
    std::string api = "asio";
    int port = 43717;
    int buffer = 64;
    int sr = 48000;
    int inCh = 2;
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<float> loadIrFile(const fs::path& path, int streamSr, std::string* err) {
    unsigned int ch = 0, sr = 0;
    drwav_uint64 frames = 0;
    float* data =
        drwav_open_file_and_read_pcm_frames_f32(path.string().c_str(), &ch, &sr, &frames, nullptr);
    if (!data) {
        *err = "failed to read wav";
        return {};
    }
    std::vector<float> ir(frames);
    for (drwav_uint64 f = 0; f < frames; ++f) ir[f] = data[f * ch];
    drwav_free(data, nullptr);
    if (static_cast<int>(sr) != streamSr) *err = "sample-rate mismatch (non-fatal)";
    double energy = 0.0;
    for (float v : ir) energy += static_cast<double>(v) * v;
    if (energy > 0.0) {
        const float k = static_cast<float>(1.0 / std::sqrt(energy));
        for (float& v : ir) v *= k;
    }
    return ir;
}

struct Engine {
    Options opt;
    int chIn = 2, chOut = 2;

    // Hot-swappable processors (audio thread reads, control thread swaps).
    std::atomic<nam::DSP*> model{nullptr};
    std::atomic<fftconvolver::TwoStageFFTConvolver*> convolver{nullptr};

    // Parameters (atomics; audio thread reads every block).
    std::atomic<float> gainIn{1.0f}, gainOut{1.0f};
    std::atomic<float> gateDb{-100.0f};                       // <= -90 = off
    std::atomic<float> bassDb{0.0f}, midDb{0.0f}, trebleDb{0.0f};
    std::atomic<bool> mute{false};
    std::atomic<bool> toneDirty{true};

    webamp::ToneStack tone;
    webamp::NoiseGate gate;
    std::vector<float> bufA, bufB;
    std::atomic<float> peakIn{0.0f}, peakOut{0.0f};
    std::atomic<long> xruns{0};

    // Currently loaded asset names (control thread only; guarded for state msg).
    std::mutex stateMx;
    std::string modelName, irName;

    void initBuffers() {
        bufA.resize(opt.buffer);
        bufB.resize(opt.buffer);
        gate.configure(static_cast<float>(opt.sr));
    }

    void process(const float* in, float* out, unsigned long frames) {
        nam::DSP* m = model.load(std::memory_order_acquire);
        fftconvolver::TwoStageFFTConvolver* cv = convolver.load(std::memory_order_acquire);
        if (mute.load(std::memory_order_relaxed) || !m || !cv) {
            std::memset(out, 0, frames * chOut * sizeof(float));
            return;
        }
        if (toneDirty.exchange(false, std::memory_order_relaxed))
            tone.configure(static_cast<float>(opt.sr), bassDb.load(), midDb.load(),
                           trebleDb.load());

        const int inIdx = std::min(opt.inCh - 1, chIn - 1);
        const float gIn = gainIn.load(std::memory_order_relaxed);
        const float gOut = gainOut.load(std::memory_order_relaxed);
        const float gateLin =
            gateDb.load(std::memory_order_relaxed) <= -90.0f
                ? 0.0f
                : std::pow(10.0f, gateDb.load(std::memory_order_relaxed) / 20.0f);

        float pIn = 0.0f;
        for (unsigned long f = 0; f < frames; ++f) {
            float v = in[f * chIn + inIdx] * gIn;
            pIn = std::max(pIn, std::fabs(v));
            bufA[f] = gate.process(v, gateLin);
        }

        float* namIn = bufA.data();
        float* namOut = bufB.data();
        m->process(&namIn, &namOut, static_cast<int>(frames));
        for (unsigned long f = 0; f < frames; ++f) bufB[f] = tone.process(bufB[f]);
        cv->process(bufB.data(), bufA.data(), frames);

        float pOut = 0.0f;
        for (unsigned long f = 0; f < frames; ++f) {
            const float v = std::clamp(bufA[f] * gOut, -1.0f, 1.0f);
            pOut = std::max(pOut, std::fabs(v));
            for (int c = 0; c < chOut; ++c) out[f * chOut + c] = v;
        }
        float cur = peakIn.load(std::memory_order_relaxed);
        while (pIn > cur && !peakIn.compare_exchange_weak(cur, pIn)) {}
        cur = peakOut.load(std::memory_order_relaxed);
        while (pOut > cur && !peakOut.compare_exchange_weak(cur, pOut)) {}
    }
};

int audioCallback(const void* input, void* output, unsigned long frames,
                  const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags flags, void* user) {
    auto* e = static_cast<Engine*>(user);
    if (flags & (paInputUnderflow | paInputOverflow | paOutputUnderflow | paOutputOverflow))
        e->xruns.fetch_add(1, std::memory_order_relaxed);
    if (!input || !output) {
        if (output) std::memset(output, 0, frames * e->chOut * sizeof(float));
        return paContinue;
    }
    e->process(static_cast<const float*>(input), static_cast<float*>(output), frames);
    return paContinue;
}

// --- Asset registry (names exposed to clients; paths stay server-side) ------
struct Assets {
    fs::path root;
    std::vector<fs::path> models, irs;
    void scan() {
        models.clear();
        irs.clear();
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) continue;
            const std::string ext = lower(entry.path().extension().string());
            if (ext == ".nam") models.push_back(entry.path());
            else if (ext == ".wav") irs.push_back(entry.path());
        }
        std::sort(models.begin(), models.end());
        std::sort(irs.begin(), irs.end());
    }
    static std::string displayName(const fs::path& p) { return p.stem().string(); }
    const fs::path* find(const std::vector<fs::path>& list, const std::string& name) const {
        for (const auto& p : list)
            if (displayName(p) == name) return &p;
        return nullptr;
    }
};

// --- Control plane -----------------------------------------------------------
struct Control {
    Engine& engine;
    Assets assets;

    bool loadModel(const fs::path& path, std::string* err) {
        try {
            auto next = nam::get_dsp(path);
            next->ResetAndPrewarm(engine.opt.sr, engine.opt.buffer);
            nam::DSP* old = engine.model.exchange(next.release(), std::memory_order_acq_rel);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));  // audio-thread grace
            delete old;
            std::lock_guard<std::mutex> lk(engine.stateMx);
            engine.modelName = Assets::displayName(path);
            return true;
        } catch (const std::exception& ex) {
            *err = ex.what();
            return false;
        }
    }

    bool loadIr(const fs::path& path, std::string* err) {
        std::string warn;
        auto ir = loadIrFile(path, engine.opt.sr, &warn);
        if (ir.empty()) {
            *err = warn;
            return false;
        }
        auto next = std::make_unique<fftconvolver::TwoStageFFTConvolver>();
        if (!next->init(engine.opt.buffer, kTailBlock, ir.data(), ir.size())) {
            *err = "convolver init failed";
            return false;
        }
        auto* old = engine.convolver.exchange(next.release(), std::memory_order_acq_rel);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        delete old;
        std::lock_guard<std::mutex> lk(engine.stateMx);
        engine.irName = Assets::displayName(path);
        return true;
    }

    json stateJson() {
        json models = json::array(), irs = json::array();
        for (const auto& p : assets.models) models.push_back(Assets::displayName(p));
        for (const auto& p : assets.irs) irs.push_back(Assets::displayName(p));
        std::lock_guard<std::mutex> lk(engine.stateMx);
        return {
            {"type", "state"},
            {"version", kVersion},
            {"params",
             {{"gainIn", engine.gainIn.load()},
              {"gainOut", engine.gainOut.load()},
              {"gate", engine.gateDb.load()},
              {"bass", engine.bassDb.load()},
              {"mid", engine.midDb.load()},
              {"treble", engine.trebleDb.load()},
              {"mute", engine.mute.load()}}},
            {"model", engine.modelName},
            {"ir", engine.irName},
            {"models", models},
            {"irs", irs},
            {"engine",
             {{"api", engine.opt.api},
              {"sampleRate", engine.opt.sr},
              {"buffer", engine.opt.buffer},
              {"xruns", engine.xruns.load()}}},
        };
    }

    // Returns the reply; state broadcasts are handled by the caller via changed=true.
    json handle(const json& msg, bool* changed) {
        const std::string type = msg.value("type", "");
        *changed = false;
        if (type == "hello") return stateJson();
        if (type == "panic") {
            engine.mute.store(true);
            *changed = true;
            return stateJson();
        }
        if (type == "setParam") {
            const std::string id = msg.value("id", "");
            const float v = msg.value("value", 0.0f);
            if (id == "gainIn") engine.gainIn.store(std::clamp(v, 0.0f, 8.0f));
            else if (id == "gainOut") engine.gainOut.store(std::clamp(v, 0.0f, 4.0f));
            else if (id == "gate") engine.gateDb.store(std::clamp(v, -100.0f, 0.0f));
            else if (id == "bass") { engine.bassDb.store(std::clamp(v, -12.0f, 12.0f)); engine.toneDirty.store(true); }
            else if (id == "mid") { engine.midDb.store(std::clamp(v, -12.0f, 12.0f)); engine.toneDirty.store(true); }
            else if (id == "treble") { engine.trebleDb.store(std::clamp(v, -12.0f, 12.0f)); engine.toneDirty.store(true); }
            else if (id == "mute") engine.mute.store(v != 0.0f);
            else return {{"type", "error"}, {"message", "unknown param: " + id}};
            *changed = true;
            return stateJson();
        }
        if (type == "setModel" || type == "setIr") {
            const std::string name = msg.value("name", "");
            const bool isModel = (type == "setModel");
            const fs::path* p = assets.find(isModel ? assets.models : assets.irs, name);
            if (!p) return {{"type", "error"}, {"message", "unknown asset: " + name}};
            std::string err;
            const bool ok = isModel ? loadModel(*p, &err) : loadIr(*p, &err);
            if (!ok) return {{"type", "error"}, {"message", err}};
            *changed = true;
            return stateJson();
        }
        return {{"type", "error"}, {"message", "unknown message type"}};
    }
};

bool originAllowed(const std::string& origin) {
    if (origin.empty()) return true;  // native tools / same-machine scripts
    const std::string o = lower(origin);
    for (const char* prefix : {"http://localhost", "https://localhost", "http://127.0.0.1",
                               "https://127.0.0.1"})
        if (o.rfind(prefix, 0) == 0) return true;
    return false;  // TODO(P2c): pairing-code flow for the real hosted origin
}

bool findDevices(const Options& opt, int* inDev, int* outDev) {
    PaHostApiTypeId type = paASIO;
    if (opt.api == "wasapi") type = paWASAPI;
    else if (opt.api == "wdmks") type = paWDMKS;
    const PaHostApiIndex apiIdx = Pa_HostApiTypeIdToHostApiIndex(type);
    if (apiIdx < 0) return false;
    *inDev = *outDev = -1;
    const PaHostApiInfo* apiInfo = Pa_GetHostApiInfo(apiIdx);
    for (int i = 0; i < apiInfo->deviceCount; ++i) {
        const PaDeviceIndex dev = Pa_HostApiDeviceIndexToDeviceIndex(apiIdx, i);
        const PaDeviceInfo* di = Pa_GetDeviceInfo(dev);
        if (lower(di->name).find(kDeviceMatch) == std::string::npos) continue;
        if (di->maxInputChannels > 0 && *inDev < 0) *inDev = dev;
        if (di->maxOutputChannels > 0 && *outDev < 0) *outDev = dev;
    }
    return *inDev >= 0 && *outDev >= 0;
}

}  // namespace

int main(int argc, char** argv) {
    // No console in WIN32 subsystem: log to a file next to the exe.
    const fs::path logPath = exeDir() / "webamp-helper.log";
    freopen(logPath.string().c_str(), "w", stdout);
    freopen(logPath.string().c_str(), "a", stderr);
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    Engine engine;
    engine.opt.assets = (exeDir() / "assets").string();  // default; --assets overrides
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", what); std::exit(2); }
            return argv[++i];
        };
        if (a == "--assets") engine.opt.assets = next("--assets");
        else if (a == "--api") engine.opt.api = next("--api");
        else if (a == "--port") engine.opt.port = std::atoi(next("--port"));
        else if (a == "--buffer") engine.opt.buffer = std::atoi(next("--buffer"));
        else if (a == "--sr") engine.opt.sr = std::atoi(next("--sr"));
        else if (a == "--in-ch") engine.opt.inCh = std::atoi(next("--in-ch"));
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    auto fatal = [](const std::string& m) {
        std::fprintf(stderr, "FATAL: %s\n", m.c_str());
        MessageBoxA(nullptr, m.c_str(), "webamp engine", MB_ICONERROR | MB_OK);
        return 1;
    };
    if (!fs::is_directory(engine.opt.assets))
        return fatal("Assets folder not found: " + engine.opt.assets +
                     "\n\nPut your .nam models and .wav IRs there, or pass --assets <dir>.");
    engine.initBuffers();

    Control control{engine};
    control.assets.root = engine.opt.assets;
    control.assets.scan();
    if (control.assets.models.empty() || control.assets.irs.empty())
        return fatal("Assets folder needs at least one .nam model and one .wav IR: " +
                     engine.opt.assets);
    std::printf("Assets: %zu models, %zu IRs\n", control.assets.models.size(),
                control.assets.irs.size());
    std::string err;
    if (!control.loadModel(control.assets.models.front(), &err) ||
        !control.loadIr(control.assets.irs.front(), &err)) {
        std::fprintf(stderr, "initial load failed: %s\n", err.c_str());
        return 1;
    }

    PaError paErr = Pa_Initialize();
    if (paErr != paNoError) {
        std::fprintf(stderr, "Pa_Initialize: %s\n", Pa_GetErrorText(paErr));
        return 1;
    }
    int inDev, outDev;
    if (!findDevices(engine.opt, &inDev, &outDev)) {
        std::fprintf(stderr, "audio device not found on %s\n", engine.opt.api.c_str());
        return 1;
    }
    const double latencySec = static_cast<double>(engine.opt.buffer) / engine.opt.sr;
    PaStreamParameters inP{}, outP{};
    inP.device = inDev;
    inP.channelCount = engine.chIn;
    inP.sampleFormat = paFloat32;
    inP.suggestedLatency = latencySec;
    outP.device = outDev;
    outP.channelCount = engine.chOut;
    outP.sampleFormat = paFloat32;
    outP.suggestedLatency = latencySec;
    PaStream* stream = nullptr;
    paErr = Pa_OpenStream(&stream, &inP, &outP, engine.opt.sr, engine.opt.buffer, paNoFlag,
                          audioCallback, &engine);
    if (paErr != paNoError) {
        std::fprintf(stderr, "Pa_OpenStream: %s\n", Pa_GetErrorText(paErr));
        return 1;
    }
    Pa_StartStream(stream);
    std::printf("Audio running: %s, %d Hz, buffer %d — %s + %s\n", engine.opt.api.c_str(),
                engine.opt.sr, engine.opt.buffer, engine.modelName.c_str(),
                engine.irName.c_str());

    ix::initNetSystem();
    ix::WebSocketServer server(engine.opt.port, "127.0.0.1");
    server.setOnClientMessageCallback([&](std::shared_ptr<ix::ConnectionState> state,
                                          ix::WebSocket& ws, const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Open) {
            const auto it = msg->openInfo.headers.find("Origin");
            const std::string origin = it != msg->openInfo.headers.end() ? it->second : "";
            if (!originAllowed(origin)) {
                std::printf("Rejected connection from origin: %s\n", origin.c_str());
                ws.close(1008, "origin not allowed");
                return;
            }
            std::printf("Client connected (origin: %s)\n",
                        origin.empty() ? "<none>" : origin.c_str());
        } else if (msg->type == ix::WebSocketMessageType::Message) {
            json reply;
            bool changed = false;
            try {
                reply = control.handle(json::parse(msg->str), &changed);
            } catch (const std::exception& ex) {
                reply = {{"type", "error"}, {"message", ex.what()}};
            }
            if (changed) {
                const std::string s = reply.dump();
                for (auto&& client : server.getClients()) client->send(s);
            } else {
                ws.send(reply.dump());
            }
        }
    });
    auto res = server.listen();
    if (!res.first) return fatal("Control port busy (is webamp already running?): " + res.second);
    server.start();
    std::printf("Control server: ws://127.0.0.1:%d\n", engine.opt.port);

    // UI server: the helper serves its own page.
    ix::HttpServer ui(kUiPort, "127.0.0.1");
    ui.setOnConnectionCallback(
        [](ix::HttpRequestPtr req, std::shared_ptr<ix::ConnectionState>) -> ix::HttpResponsePtr {
            if (req->method == "GET" && (req->uri == "/" || req->uri == "/index.html")) {
                ix::WebSocketHttpHeaders h;
                h["Content-Type"] = "text/html; charset=utf-8";
                h["Cache-Control"] = "no-store";
                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok, h,
                    std::string(reinterpret_cast<const char*>(kWebUiHtml), kWebUiHtml_len));
            }
            return std::make_shared<ix::HttpResponse>(404, "Not Found");
        });
    auto uiRes = ui.listen();
    if (!uiRes.first) return fatal("UI port busy: " + uiRes.second);
    ui.start();
    std::printf("UI: %s\n", kUiUrl);

    gToggleMute = [&engine, &control, &server]() {
        engine.mute.store(!engine.mute.load());
        const std::string s = control.stateJson().dump();
        for (auto&& client : server.getClients()) client->send(s);
    };
    std::thread tray(trayThread);

    // Meter broadcast loop (~15 Hz) until the tray quits us.
    while (gRunning.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(66));
        const auto clients = server.getClients();
        if (clients.empty()) continue;
        const json m = {{"type", "meters"},
                        {"in", engine.peakIn.exchange(0.0f)},
                        {"out", engine.peakOut.exchange(0.0f)}};
        const std::string s = m.dump();
        for (auto&& client : clients) client->send(s);
    }

    std::printf("Shutting down.\n");
    ui.stop();
    server.stop();
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    tray.join();
    return 0;
}
