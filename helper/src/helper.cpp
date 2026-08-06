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
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
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
#include "tuner.h"
#include "pedals.h"

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

// Renders "♪" into a 32x32 ARGB icon (UI-accent amber) — no .ico file needed.
HICON makeNoteIcon() {
    const int S = 32;
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = S;
    bi.bV5Height = -S;  // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS,
                                   &bits, nullptr, 0);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    HGDIOBJ oldBmp = SelectObject(dc, dib);

    RECT rc{0, 0, S, S};
    SetBkColor(dc, RGB(0, 0, 0));
    FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    HFONT font = CreateFontW(34, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH, L"Segoe UI Symbol");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, L"♪", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    GdiFlush();

    // Luminance -> alpha, tinted to the UI accent (blue-500 #3b82f6), premultiplied.
    auto* px = static_cast<unsigned char*>(bits);
    for (int i = 0; i < S * S; ++i) {
        unsigned char* p = px + i * 4;  // BGRA
        const unsigned a = std::max({p[0], p[1], p[2]});
        p[0] = static_cast<unsigned char>(0xF6 * a / 255);  // B
        p[1] = static_cast<unsigned char>(0x82 * a / 255);  // G
        p[2] = static_cast<unsigned char>(0x3B * a / 255);  // R
        p[3] = static_cast<unsigned char>(a);
    }
    SelectObject(dc, oldFont);
    DeleteObject(font);
    SelectObject(dc, oldBmp);
    DeleteDC(dc);

    HBITMAP mask = CreateBitmap(S, S, 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = dib;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(dib);
    DeleteObject(mask);
    return icon ? icon : LoadIconA(nullptr, IDI_APPLICATION);
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
    nid.hIcon = makeNoteIcon();
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
    int buffer = 128;  // safe default (~11 ms); 64 available for lowest latency
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
    std::atomic<int> chIn{2}, chOut{2};   // set by AudioIO when a stream opens
    std::atomic<int> inCh{2};             // 1-based physical input channel for the guitar

    // Hot-swappable processors (audio thread reads, control thread swaps).
    std::atomic<nam::DSP*> model{nullptr};
    std::atomic<fftconvolver::TwoStageFFTConvolver*> convolver{nullptr};

    // Parameters (atomics; audio thread reads every block).
    std::atomic<float> gainIn{1.0f}, gainOut{1.0f};
    std::atomic<float> gateDb{-100.0f};                       // <= -90 = off
    std::atomic<float> bassDb{0.0f}, midDb{0.0f}, trebleDb{0.0f};
    // Starts muted: the guitar path is silent until the user deliberately
    // enables it (safety against feedback/blasts on open). Never persisted —
    // a fresh, conscious click each launch. Metronome + tuner still work muted.
    std::atomic<bool> mute{true};
    std::atomic<bool> toneDirty{true};
    std::atomic<bool> metroOn{false}, metroAccent{true};
    std::atomic<float> metroBpm{120.0f}, metroVol{0.5f};
    std::atomic<int> metroBeats{4};
    std::atomic<long long> beatCount{0};  // clicks fired; UI flashes on change
    std::atomic<int> beatInBar{0};

    // Tuner: audio thread writes pre-gate input into a ring; control thread
    // snapshots the last window and runs YIN on it.
    std::atomic<bool> tunerOn{false};
    static constexpr uint32_t kRingSize = 16384;  // power of two
    std::vector<float> tunerRing = std::vector<float>(kRingSize, 0.0f);
    std::atomic<uint32_t> tunerPos{0};

    webamp::ToneStack tone;
    webamp::NoiseGate gate;
    webamp::Metronome metro;

    // Pedalboard: fixed roster, each placeable pre/post-amp, reorderable.
    // Defaults: comp+drive before the amp, chorus/delay/reverb after; all off.
    webamp::CompressorPedal comp;
    webamp::DrivePedal drivePedal;
    webamp::ChorusPedal chorus;
    webamp::DelayPedal delayPedal;
    webamp::ReverbPedal reverb;
    std::array<webamp::Pedal*, 5> pedals = {&comp, &drivePedal, &chorus, &delayPedal, &reverb};

    std::vector<float> bufA, bufB;
    std::atomic<float> peakIn{0.0f}, peakOut{0.0f};
    std::atomic<long> xruns{0};

    webamp::Pedal* findPedal(const std::string& t) {
        for (auto* p : pedals)
            if (t == p->type()) return p;
        return nullptr;
    }

    // Serialize the current rig for persistence. Excludes transient state that
    // should reset each launch: mute, tunerOn, and the metronome on/off flag.
    json ampJson() {
        json pedalsArr = json::array();
        for (auto* p : pedals) {
            json params = json::object();
            for (const auto& kv : p->paramList()) params[kv.first] = kv.second;
            pedalsArr.push_back({{"type", p->type()},
                                 {"enabled", p->enabled.load()},
                                 {"placement", p->placement.load()},
                                 {"order", p->order.load()},
                                 {"params", params}});
        }
        std::lock_guard<std::mutex> lk(stateMx);  // modelName / irName
        return {{"gainIn", gainIn.load()},
                {"gainOut", gainOut.load()},
                {"gate", gateDb.load()},
                {"bass", bassDb.load()},
                {"mid", midDb.load()},
                {"treble", trebleDb.load()},
                {"model", modelName},
                {"ir", irName},
                {"metroBpm", metroBpm.load()},
                {"metroBeats", metroBeats.load()},
                {"metroAccent", metroAccent.load()},
                {"metroVol", metroVol.load()},
                {"pedals", pedalsArr}};
    }

    // Currently loaded asset names (control thread only; guarded for state msg).
    std::mutex stateMx;
    std::string modelName, irName;
    double repInMs = 0, repOutMs = 0;  // PortAudio-reported stream latency (estimate)

    void initBuffers() {
        bufA.resize(opt.buffer);
        bufB.resize(opt.buffer);
        const float sr = static_cast<float>(opt.sr);
        gate.configure(sr);
        metro.configure(sr);
        inCh.store(opt.inCh);
        for (auto* p : pedals) p->configure(sr);
        // Default placement/order (all disabled until the user switches one on).
        comp.placement.store(0); comp.order.store(0);
        drivePedal.placement.store(0); drivePedal.order.store(1);
        chorus.placement.store(1); chorus.order.store(0);
        delayPedal.placement.store(1); delayPedal.order.store(1);
        reverb.placement.store(1); reverb.order.store(2);
    }

    void process(const float* in, float* out, unsigned long frames) {
        const int chIn_ = chIn.load(std::memory_order_relaxed);
        const int chOut_ = chOut.load(std::memory_order_relaxed);
        nam::DSP* m = model.load(std::memory_order_acquire);
        fftconvolver::TwoStageFFTConvolver* cv = convolver.load(std::memory_order_acquire);
        if (!m || !cv) {
            std::memset(out, 0, frames * chOut_ * sizeof(float));
            return;
        }
        const bool muted = mute.load(std::memory_order_relaxed);
        if (toneDirty.exchange(false, std::memory_order_relaxed))
            tone.configure(static_cast<float>(opt.sr), bassDb.load(), midDb.load(),
                           trebleDb.load());

        const int inIdx = std::clamp(inCh.load(std::memory_order_relaxed) - 1, 0, chIn_ - 1);
        const float gIn = gainIn.load(std::memory_order_relaxed);
        const float gOut = gainOut.load(std::memory_order_relaxed);
        const float gateLin =
            gateDb.load(std::memory_order_relaxed) <= -90.0f
                ? 0.0f
                : std::pow(10.0f, gateDb.load(std::memory_order_relaxed) / 20.0f);

        float pIn = 0.0f;
        const uint32_t ringBase = tunerPos.load(std::memory_order_relaxed);
        for (unsigned long f = 0; f < frames; ++f) {
            const float raw = in[f * chIn_ + inIdx] * gIn;
            pIn = std::max(pIn, std::fabs(raw));               // meter shows the real input
            tunerRing[(ringBase + f) & (kRingSize - 1)] = raw;  // tuner works even while muted
            bufA[f] = gate.process(muted ? 0.0f : raw, gateLin);  // amp path silent when muted
        }
        tunerPos.store(ringBase + static_cast<uint32_t>(frames), std::memory_order_release);

        // Build the ordered pre/post pedal chains from atomics (<=5 items).
        webamp::Pedal* pre[5];
        webamp::Pedal* post[5];
        int preN = 0, postN = 0;
        for (auto* p : pedals) {
            if (!p->enabled.load(std::memory_order_relaxed)) continue;
            if (p->needReset.exchange(false, std::memory_order_relaxed)) p->reset();
            (p->placement.load(std::memory_order_relaxed) == 0 ? pre[preN++] : post[postN++]) = p;
        }
        auto byOrder = [](webamp::Pedal* a, webamp::Pedal* b) {
            return a->order.load(std::memory_order_relaxed) < b->order.load(std::memory_order_relaxed);
        };
        std::sort(pre, pre + preN, byOrder);
        std::sort(post, post + postN, byOrder);

        for (int k = 0; k < preN; ++k) pre[k]->processBlock(bufA.data(), static_cast<int>(frames));

        float* namIn = bufA.data();
        float* namOut = bufB.data();
        m->process(&namIn, &namOut, static_cast<int>(frames));
        for (unsigned long f = 0; f < frames; ++f) bufB[f] = tone.process(bufB[f]);
        cv->process(bufB.data(), bufA.data(), frames);

        for (int k = 0; k < postN; ++k) post[k]->processBlock(bufA.data(), static_cast<int>(frames));

        const bool mOn = metroOn.load(std::memory_order_relaxed);
        const float mBpm = metroBpm.load(std::memory_order_relaxed);
        const float mVol = metroVol.load(std::memory_order_relaxed);
        const int mBeats = metroBeats.load(std::memory_order_relaxed);
        const bool mAccent = metroAccent.load(std::memory_order_relaxed);
        metro.blockStart(mOn);

        float pOut = 0.0f;
        for (unsigned long f = 0; f < frames; ++f) {
            const float click = metro.process(mOn, mBpm, mBeats, mAccent) * mVol;
            const float v = std::clamp(bufA[f] * gOut + click, -1.0f, 1.0f);
            pOut = std::max(pOut, std::fabs(v));
            for (int c = 0; c < chOut_; ++c) out[f * chOut_ + c] = v;
        }
        beatCount.store(metro.clickCount, std::memory_order_relaxed);
        beatInBar.store(metro.beat, std::memory_order_relaxed);
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
        if (output) std::memset(output, 0, frames * e->chOut.load(std::memory_order_relaxed) * sizeof(float));
        return paContinue;
    }
    e->process(static_cast<const float*>(input), static_cast<float*>(output), frames);
    return paContinue;
}

// --- Audio device enumeration + stream management ----------------------------
struct DeviceInfo {
    int index;
    std::string name;
    std::string api;
    int channels;
};

// Low-latency devices only: ASIO, WASAPI, WDM-KS (skip MME/DirectSound legacy).
std::vector<DeviceInfo> enumerateDevices(bool wantInput) {
    std::vector<DeviceInfo> out;
    for (PaHostApiIndex a = 0; a < Pa_GetHostApiCount(); ++a) {
        const PaHostApiInfo* ai = Pa_GetHostApiInfo(a);
        std::string apiName;
        if (ai->type == paASIO) apiName = "ASIO";
        else if (ai->type == paWASAPI) apiName = "WASAPI";
        else if (ai->type == paWDMKS) apiName = "WDM-KS";
        else continue;
        for (int i = 0; i < ai->deviceCount; ++i) {
            const PaDeviceIndex dev = Pa_HostApiDeviceIndexToDeviceIndex(a, i);
            const PaDeviceInfo* di = Pa_GetDeviceInfo(dev);
            const int ch = wantInput ? di->maxInputChannels : di->maxOutputChannels;
            if (ch <= 0) continue;
            out.push_back({dev, di->name, apiName, ch});
        }
    }
    return out;
}

std::string deviceApiName(int dev) {
    const PaDeviceInfo* di = Pa_GetDeviceInfo(dev);
    if (!di) return "";
    switch (Pa_GetHostApiInfo(di->hostApi)->type) {
        case paASIO: return "ASIO";
        case paWASAPI: return "WASAPI";
        case paWDMKS: return "WDM-KS";
        default: return "?";
    }
}

// Device identity that survives across launches (indices can shift).
struct DeviceRef {
    std::string name, api;
    bool operator==(const DeviceRef& o) const { return name == o.name && api == o.api; }
    bool empty() const { return name.empty(); }
};

int resolveDevice(const DeviceRef& ref, bool wantInput) {
    if (ref.empty()) return -1;
    for (const auto& d : enumerateDevices(wantInput))
        if (d.name == ref.name && d.api == ref.api) return d.index;
    return -1;
}

// Monotonic milliseconds, used to debounce config writes.
static long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Persisted device/channel choice + amp rig, next to the exe. Applied at startup
// so ASIO devices (which can't reliably re-init mid-process) always open once.
struct Config {
    DeviceRef input, output;
    int inCh = 2;
    int buffer = 0;  // 0 = unspecified (use default)
    bool present = false;
    json amp;  // saved rig (amp params, model/IR, pedals, metro); null if none
};

Config loadConfig(const fs::path& path) {
    Config c;
    try {
        std::ifstream f(path);
        if (!f) return c;
        json j; f >> j;
        c.input = {j["input"].value("name", ""), j["input"].value("api", "")};
        c.output = {j["output"].value("name", ""), j["output"].value("api", "")};
        c.inCh = j.value("inCh", 2);
        c.buffer = j.value("buffer", 0);
        if (j.contains("amp")) c.amp = j["amp"];
        c.present = !c.input.empty();
    } catch (...) {}
    return c;
}

void saveConfig(const fs::path& path, const DeviceRef& in, const DeviceRef& out, int inCh, int buffer,
                const json& amp) {
    try {
        json j = {{"input", {{"name", in.name}, {"api", in.api}}},
                  {"output", {{"name", out.name}, {"api", out.api}}},
                  {"inCh", inCh},
                  {"buffer", buffer}};
        if (!amp.is_null()) j["amp"] = amp;
        std::ofstream f(path);
        f << j.dump(2);
    } catch (...) {}
}

// Owns the PortAudio stream; can restart it on a different device from the
// control thread while the meter loop and audio callback keep running.
struct AudioIO {
    Engine* engine = nullptr;
    PaStream* stream = nullptr;
    std::atomic<int> inputDevice{-1}, outputDevice{-1};
    fs::path configFile;
    DeviceRef activeIn, activeOut;   // for persistence
    DeviceRef pendingIn, pendingOut; // a saved choice awaiting restart (live switch failed)
    std::atomic<bool> pending{false};
    std::atomic<int> pendingBuffer{0};  // buffer size awaiting restart (0 = none)
    std::mutex mx;

    int bufferToSave() { const int p = pendingBuffer.load(); return p ? p : engine->opt.buffer; }
    void persist() {
        saveConfig(configFile, activeIn, activeOut, engine->inCh.load(), bufferToSave(),
                   engine->ampJson());
    }

    bool openLocked(int inDev, int outDev, std::string* err) {
        // Already on these devices — no-op. Avoids the ASIO "reopen the same
        // driver" failure and needless dropouts (inCh changes don't come here).
        if (stream && inDev == inputDevice.load() && outDev == outputDevice.load()) return true;

        const int sr = engine->opt.sr, buffer = engine->opt.buffer;
        const PaDeviceInfo* diIn = Pa_GetDeviceInfo(inDev);
        const PaDeviceInfo* diOut = Pa_GetDeviceInfo(outDev);
        if (!diIn || !diOut) { *err = "invalid device index"; return false; }
        const int chIn = std::min(diIn->maxInputChannels, 8);
        const int chOut = std::min(diOut->maxOutputChannels, 2);
        if (chIn < 1 || chOut < 1) { *err = "device has no usable channels"; return false; }

        if (stream) {
            Pa_StopStream(stream);
            Pa_CloseStream(stream);
            stream = nullptr;
            std::this_thread::sleep_for(std::chrono::milliseconds(150));  // let ASIO release
        }

        const double lat = static_cast<double>(buffer) / sr;
        PaStreamParameters inP{}, outP{};
        inP.device = inDev; inP.channelCount = chIn; inP.sampleFormat = paFloat32; inP.suggestedLatency = lat;
        outP.device = outDev; outP.channelCount = chOut; outP.sampleFormat = paFloat32; outP.suggestedLatency = lat;
        engine->chIn.store(chIn);
        engine->chOut.store(chOut);
        PaStream* s = nullptr;
        const PaError e = Pa_OpenStream(&s, &inP, &outP, sr, buffer, paNoFlag, audioCallback, engine);
        if (e != paNoError) { *err = Pa_GetErrorText(e); return false; }
        if (const PaStreamInfo* si = Pa_GetStreamInfo(s)) {
            engine->repInMs = si->inputLatency * 1000.0;
            engine->repOutMs = si->outputLatency * 1000.0;
        }
        Pa_StartStream(s);
        stream = s;
        inputDevice.store(inDev);
        outputDevice.store(outDev);
        activeIn = {diIn->name, deviceApiName(inDev)};
        activeOut = {diOut->name, deviceApiName(outDev)};
        return true;
    }

    bool open(int inDev, int outDev, std::string* err) {
        std::lock_guard<std::mutex> lk(mx);
        return openLocked(inDev, outDev, err);
    }

    // User picked new devices. Always persist the choice; attempt a live switch.
    // If the live switch fails (typically ASIO re-init), keep the current device
    // running and mark the choice pending — it applies on the next launch.
    // Returns true if applied live, false if deferred to restart.
    bool requestDevice(int inDev, int outDev, bool* deferred) {
        std::lock_guard<std::mutex> lk(mx);
        const DeviceRef reqIn = {Pa_GetDeviceInfo(inDev) ? Pa_GetDeviceInfo(inDev)->name : "",
                                 deviceApiName(inDev)};
        const DeviceRef reqOut = {Pa_GetDeviceInfo(outDev) ? Pa_GetDeviceInfo(outDev)->name : "",
                                  deviceApiName(outDev)};
        const int prevIn = inputDevice.load(), prevOut = outputDevice.load();
        std::string err;
        if (openLocked(inDev, outDev, &err)) {
            pending.store(false);
            *deferred = false;
            saveConfig(configFile, activeIn, activeOut, engine->inCh.load(), bufferToSave(),
                       engine->ampJson());
            return true;
        }
        std::string ignore;
        openLocked(prevIn, prevOut, &ignore);  // restore audio
        pendingIn = reqIn;
        pendingOut = reqOut;
        pending.store(true);
        *deferred = true;
        saveConfig(configFile, reqIn, reqOut, engine->inCh.load(), bufferToSave(),
                   engine->ampJson());  // apply next launch
        return false;
    }

    void close() {
        std::lock_guard<std::mutex> lk(mx);
        if (stream) { Pa_StopStream(stream); Pa_CloseStream(stream); stream = nullptr; }
    }
};

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
    AudioIO* audio = nullptr;
    fs::path presetsFile;
    json presets = json::array();  // list of preset objects, loaded at startup

    void loadPresetsFromDisk() {
        try {
            std::ifstream f(presetsFile);
            if (!f) return;
            json j; f >> j;
            if (j.is_array()) presets = j;
        } catch (...) { presets = json::array(); }
    }
    void writePresets() {
        try { std::ofstream f(presetsFile); f << presets.dump(2); } catch (...) {}
    }

    // Capture the current tone (amp + cab + knobs + pedals) as a named preset.
    json capturePreset(const std::string& name) {
        json params = {{"gainIn", engine.gainIn.load()}, {"gainOut", engine.gainOut.load()},
                       {"gate", engine.gateDb.load()},   {"bass", engine.bassDb.load()},
                       {"mid", engine.midDb.load()},      {"treble", engine.trebleDb.load()}};
        json peds = json::array();
        for (auto* p : engine.pedals) {
            json pp = json::object();
            for (const auto& kv : p->paramList()) pp[kv.first] = kv.second;
            peds.push_back({{"type", p->type()}, {"enabled", p->enabled.load()},
                            {"placement", p->placement.load() == 0 ? "pre" : "post"},
                            {"order", p->order.load()}, {"params", pp}});
        }
        std::lock_guard<std::mutex> lk(engine.stateMx);
        return {{"name", name}, {"model", engine.modelName}, {"ir", engine.irName},
                {"params", params}, {"pedals", peds}};
    }

    // Apply a preset. Missing model/IR assets are skipped (tone applies partially).
    void applyPreset(const json& p) {
        std::string e;
        if (const fs::path* mp = assets.find(assets.models, p.value("model", std::string())))
            loadModel(*mp, &e);
        if (const fs::path* ip = assets.find(assets.irs, p.value("ir", std::string())))
            loadIr(*ip, &e);
        const json pr = p.value("params", json::object());
        auto num = [&](const char* k, float def) { return pr.contains(k) ? pr[k].get<float>() : def; };
        engine.gainIn.store(std::clamp(num("gainIn", 1.0f), 0.0f, 8.0f));
        engine.gainOut.store(std::clamp(num("gainOut", 1.0f), 0.0f, 4.0f));
        engine.gateDb.store(std::clamp(num("gate", -100.0f), -100.0f, 0.0f));
        engine.bassDb.store(std::clamp(num("bass", 0.0f), -12.0f, 12.0f));
        engine.midDb.store(std::clamp(num("mid", 0.0f), -12.0f, 12.0f));
        engine.trebleDb.store(std::clamp(num("treble", 0.0f), -12.0f, 12.0f));
        engine.toneDirty.store(true);
        for (const auto& pd : p.value("pedals", json::array())) {
            webamp::Pedal* ped = engine.findPedal(pd.value("type", std::string()));
            if (!ped) continue;
            const bool en = pd.value("enabled", false);
            if (en) ped->needReset.store(true);
            ped->enabled.store(en);
            ped->placement.store(pd.value("placement", std::string("pre")) == "post" ? 1 : 0);
            ped->order.store(pd.value("order", 0));
            // Bind to a named object: calling .items() on the temporary from
            // pd.value(...) would dangle (range-for won't extend its lifetime).
            const json pparams = pd.value("params", json::object());
            for (const auto& item : pparams.items())
                if (item.value().is_number()) ped->setParam(item.key(), item.value().get<float>());
        }
    }

    // Debounced rig persistence: handlers mark the config dirty; the meter loop
    // flushes ~400 ms after the last change so knob drags don't thrash the disk.
    std::atomic<bool> cfgDirty{false};
    std::atomic<long long> cfgTouchMs{0};
    void touchConfig() {
        cfgTouchMs.store(nowMs());
        cfgDirty.store(true);
    }

    // Restore a saved rig over the freshly-loaded defaults (once, at startup).
    void applyAmp(const json& a) {
        if (!a.is_object()) return;
        auto num = [&](const char* k, float lo, float hi, float def) {
            return a.contains(k) && a[k].is_number() ? std::clamp(a[k].get<float>(), lo, hi) : def;
        };
        engine.gainIn.store(num("gainIn", 0.0f, 8.0f, engine.gainIn.load()));
        engine.gainOut.store(num("gainOut", 0.0f, 4.0f, engine.gainOut.load()));
        engine.gateDb.store(num("gate", -100.0f, 0.0f, engine.gateDb.load()));
        engine.bassDb.store(num("bass", -12.0f, 12.0f, engine.bassDb.load()));
        engine.midDb.store(num("mid", -12.0f, 12.0f, engine.midDb.load()));
        engine.trebleDb.store(num("treble", -12.0f, 12.0f, engine.trebleDb.load()));
        engine.toneDirty.store(true);
        engine.metroBpm.store(num("metroBpm", 20.0f, 360.0f, engine.metroBpm.load()));
        engine.metroBeats.store(static_cast<int>(
            num("metroBeats", 1.0f, 12.0f, static_cast<float>(engine.metroBeats.load()))));
        engine.metroVol.store(num("metroVol", 0.0f, 2.0f, engine.metroVol.load()));
        if (a.contains("metroAccent") && a["metroAccent"].is_boolean())
            engine.metroAccent.store(a["metroAccent"].get<bool>());

        std::string err;
        if (a.contains("model") && a["model"].is_string())
            if (const fs::path* p = assets.find(assets.models, a["model"].get<std::string>()))
                loadModel(*p, &err);
        if (a.contains("ir") && a["ir"].is_string())
            if (const fs::path* p = assets.find(assets.irs, a["ir"].get<std::string>()))
                loadIr(*p, &err);

        if (a.contains("pedals") && a["pedals"].is_array()) {
            for (const auto& pj : a["pedals"]) {
                webamp::Pedal* p = engine.findPedal(pj.value("type", ""));
                if (!p) continue;
                p->enabled.store(pj.value("enabled", false));
                p->placement.store(std::clamp(pj.value("placement", 0), 0, 1));
                p->order.store(pj.value("order", 0));
                if (pj.contains("params") && pj["params"].is_object())
                    for (const auto& [k, v] : pj["params"].items())
                        if (v.is_number()) p->setParam(k, v.get<float>());
                if (p->enabled.load()) p->needReset.store(true);
            }
        }
    }

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

    json pedalsJson() {
        json arr = json::array();
        for (auto* p : engine.pedals) {
            json params = json::object();
            for (const auto& kv : p->paramList()) params[kv.first] = kv.second;
            arr.push_back({{"type", p->type()},
                           {"enabled", p->enabled.load()},
                           {"placement", p->placement.load() == 0 ? "pre" : "post"},
                           {"order", p->order.load()},
                           {"params", params}});
        }
        return arr;
    }

    json audioJson() {
        json inDevs = json::array(), outDevs = json::array();
        for (const auto& d : enumerateDevices(true))
            inDevs.push_back({{"index", d.index}, {"name", d.name}, {"api", d.api}, {"channels", d.channels}});
        for (const auto& d : enumerateDevices(false))
            outDevs.push_back({{"index", d.index}, {"name", d.name}, {"api", d.api}, {"channels", d.channels}});
        json j = {
            {"inputDevice", audio ? audio->inputDevice.load() : -1},
            {"outputDevice", audio ? audio->outputDevice.load() : -1},
            {"inCh", engine.inCh.load()},
            {"inChannels", engine.chIn.load()},
            {"buffer", engine.opt.buffer},
            {"inputDevices", inDevs},
            {"outputDevices", outDevs},
        };
        json pend = json::object();
        if (audio && audio->pending.load()) {
            pend["input"] = audio->pendingIn.name;
            pend["output"] = audio->pendingOut.name;
        }
        if (audio && audio->pendingBuffer.load()) pend["buffer"] = audio->pendingBuffer.load();
        if (!pend.empty()) j["pending"] = pend;
        return j;
    }

    json stateJson() {
        json models = json::array(), irs = json::array();
        for (const auto& p : assets.models) models.push_back(Assets::displayName(p));
        for (const auto& p : assets.irs) irs.push_back(Assets::displayName(p));
        json pedals = pedalsJson();
        json audio = audioJson();
        json presetNames = json::array();
        for (const auto& pr : presets) presetNames.push_back(pr.value("name", std::string()));
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
              {"mute", engine.mute.load()},
              {"metroOn", engine.metroOn.load()},
              {"metroAccent", engine.metroAccent.load()},
              {"metroBpm", engine.metroBpm.load()},
              {"metroBeats", engine.metroBeats.load()},
              {"metroVol", engine.metroVol.load()},
              {"tunerOn", engine.tunerOn.load()}}},
            {"model", engine.modelName},
            {"ir", engine.irName},
            {"models", models},
            {"irs", irs},
            {"pedals", pedals},
            {"audio", audio},
            {"presets", presetNames},
            {"engine",
             {{"api", engine.opt.api},
              {"sampleRate", engine.opt.sr},
              {"buffer", engine.opt.buffer},
              {"xruns", engine.xruns.load()},
              // Driver-reported estimate. ASIO drivers report honestly; WASAPI
              // wildly overstates. Only a physical loopback measurement is truth.
              {"reportedLatencyMs", engine.repInMs + engine.repOutMs}}},
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
            else if (id == "metroOn") engine.metroOn.store(v != 0.0f);
            else if (id == "metroAccent") engine.metroAccent.store(v != 0.0f);
            else if (id == "metroBpm") engine.metroBpm.store(std::clamp(v, 20.0f, 360.0f));
            else if (id == "metroBeats") engine.metroBeats.store(std::clamp(static_cast<int>(v), 1, 12));
            else if (id == "metroVol") engine.metroVol.store(std::clamp(v, 0.0f, 2.0f));
            else if (id == "tunerOn") engine.tunerOn.store(v != 0.0f);
            else if (id == "inCh") {
                engine.inCh.store(std::clamp(static_cast<int>(v), 1, 8));
                if (audio) audio->persist();
            }
            else return {{"type", "error"}, {"message", "unknown param: " + id}};
            if (id == "gainIn" || id == "gainOut" || id == "gate" || id == "bass" ||
                id == "mid" || id == "treble" || id == "metroAccent" || id == "metroBpm" ||
                id == "metroBeats" || id == "metroVol")
                touchConfig();  // mute / tunerOn / metroOn are deliberately not persisted
            *changed = true;
            return stateJson();
        }
        if (type == "setPedal") {
            const std::string pedalType = msg.value("pedal", "");
            const std::string field = msg.value("field", "");
            const float v = msg.value("value", 0.0f);
            webamp::Pedal* p = engine.findPedal(pedalType);
            if (!p) return {{"type", "error"}, {"message", "unknown pedal: " + pedalType}};
            if (field == "enabled") {
                if (v != 0.0f) p->needReset.store(true);  // clear stale tails on enable
                p->enabled.store(v != 0.0f);
            } else if (field == "placement") {
                p->placement.store(v != 0.0f ? 1 : 0);  // 0 = pre, 1 = post
            } else if (field == "order") {
                p->order.store(static_cast<int>(v));
            } else if (!p->setParam(field, v)) {
                return {{"type", "error"}, {"message", "unknown pedal field: " + field}};
            }
            touchConfig();
            *changed = true;
            return stateJson();
        }
        if (type == "setAudioDevice") {
            if (!audio) return {{"type", "error"}, {"message", "audio not ready"}};
            const int inDev = msg.value("input", static_cast<int>(audio->inputDevice.load()));
            const int outDev = msg.value("output", static_cast<int>(audio->outputDevice.load()));
            bool deferred = false;
            audio->requestDevice(inDev, outDev, &deferred);
            *changed = true;  // broadcast either way (pending flag rides in state)
            return stateJson();
        }
        if (type == "setBuffer") {
            if (!audio) return {{"type", "error"}, {"message", "audio not ready"}};
            const int b = msg.value("value", engine.opt.buffer);
            if (b != 64 && b != 128 && b != 256)
                return {{"type", "error"}, {"message", "buffer must be 64, 128, or 256"}};
            // Buffer resize needs the DSP buffers reallocated → apply at startup.
            audio->pendingBuffer.store(b == engine.opt.buffer ? 0 : b);
            audio->persist();
            *changed = true;
            return stateJson();
        }
        if (type == "savePreset") {
            std::string name = msg.value("name", std::string());
            // trim
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!name.empty() && name.front() == ' ') name.erase(name.begin());
            if (name.empty()) return {{"type", "error"}, {"message", "preset name required"}};
            const json preset = capturePreset(name);
            bool replaced = false;
            for (auto& pr : presets)
                if (pr.value("name", std::string()) == name) { pr = preset; replaced = true; break; }
            if (!replaced) presets.push_back(preset);
            writePresets();
            *changed = true;
            return stateJson();
        }
        if (type == "loadPreset") {
            const std::string name = msg.value("name", std::string());
            for (const auto& pr : presets)
                if (pr.value("name", std::string()) == name) {
                    applyPreset(pr);
                    *changed = true;
                    return stateJson();
                }
            return {{"type", "error"}, {"message", "unknown preset: " + name}};
        }
        if (type == "deletePreset") {
            const std::string name = msg.value("name", std::string());
            const size_t before = presets.size();
            for (auto it = presets.begin(); it != presets.end(); ++it)
                if (it->value("name", std::string()) == name) { presets.erase(it); break; }
            if (presets.size() == before) return {{"type", "error"}, {"message", "unknown preset: " + name}};
            writePresets();
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
            touchConfig();
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
    // Single instance: a second launch just opens the UI of the running one.
    // (Without this, Windows' SO_REUSEADDR port-sharing lets clones pile up.)
    CreateMutexA(nullptr, TRUE, "Local\\webamp-helper-single-instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        ShellExecuteA(nullptr, "open", kUiUrl, nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }

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

    // Load persisted settings before initBuffers so the buffer size takes effect
    // (DSP buffers are sized here). Devices are resolved later (need PortAudio).
    const fs::path configPath = exeDir() / "webamp-config.json";
    const Config cfg = loadConfig(configPath);
    if (cfg.present) {
        if (cfg.buffer == 64 || cfg.buffer == 128 || cfg.buffer == 256) engine.opt.buffer = cfg.buffer;
        engine.opt.inCh = std::clamp(cfg.inCh, 1, 8);
    }
    engine.initBuffers();

    Control control{engine};
    control.presetsFile = exeDir() / "webamp-presets.json";
    control.loadPresetsFromDisk();
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

    // Restore the saved rig (amp params, model/IR, pedals, metronome) over the
    // just-loaded defaults. Engine-side only — no audio device needed yet.
    if (!cfg.amp.is_null()) control.applyAmp(cfg.amp);

    PaError paErr = Pa_Initialize();
    if (paErr != paNoError) {
        std::fprintf(stderr, "Pa_Initialize: %s\n", Pa_GetErrorText(paErr));
        return 1;
    }
    AudioIO audio;
    audio.engine = &engine;
    audio.configFile = configPath;
    control.audio = &audio;

    // Device order of preference: saved config → the interface's ASIO driver →
    // PortAudio defaults. Saved devices open fresh here (reliable even for ASIO).
    int inDev = -1, outDev = -1;
    if (cfg.present) {
        inDev = resolveDevice(cfg.input, true);
        outDev = resolveDevice(cfg.output, false);
        engine.inCh.store(std::clamp(cfg.inCh, 1, 8));
    }
    if (inDev < 0 || outDev < 0) {
        if (!findDevices(engine.opt, &inDev, &outDev)) {
            inDev = Pa_GetDefaultInputDevice();
            outDev = Pa_GetDefaultOutputDevice();
        }
    }
    std::string audioErr;
    if (!audio.open(inDev, outDev, &audioErr)) {
        // Saved device may be gone (unplugged); fall back to defaults.
        if (!findDevices(engine.opt, &inDev, &outDev)) {
            inDev = Pa_GetDefaultInputDevice();
            outDev = Pa_GetDefaultOutputDevice();
        }
        if (!audio.open(inDev, outDev, &audioErr))
            return fatal("Could not open an audio device: " + audioErr +
                         "\n\nPlug in your interface and restart webamp.");
    }
    std::printf("Audio running: %d Hz, buffer %d — %s + %s\n", engine.opt.sr, engine.opt.buffer,
                engine.modelName.c_str(), engine.irName.c_str());

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

    // Meter + beat broadcast loop (~25 Hz) until the tray quits us.
    int tunerTick = 0;
    std::vector<float> tunerWin(4096);
    while (gRunning.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));

        // Flush the rig to disk ~400 ms after the last change (coalesces drags).
        if (control.cfgDirty.load() && nowMs() - control.cfgTouchMs.load() > 400) {
            control.cfgDirty.store(false);
            audio.persist();
        }

        const auto clients = server.getClients();
        if (clients.empty()) continue;

        // Tuner analysis every other tick (~12 Hz) — YIN runs here, never on
        // the audio thread.
        if (engine.tunerOn.load(std::memory_order_relaxed) && (++tunerTick & 1) == 0) {
            const uint32_t pos = engine.tunerPos.load(std::memory_order_acquire);
            for (uint32_t i = 0; i < tunerWin.size(); ++i)
                tunerWin[i] = engine.tunerRing[(pos - tunerWin.size() + i) & (Engine::kRingSize - 1)];
            const float freq =
                webamp::detectPitch(tunerWin.data(), static_cast<int>(tunerWin.size()),
                                    static_cast<float>(engine.opt.sr));
            json t = {{"type", "tuner"}, {"freq", freq}};
            if (freq > 0) {
                const auto info = webamp::describeNote(freq);
                t["note"] = info.name;
                t["cents"] = info.cents;
            }
            const std::string ts = t.dump();
            for (auto&& client : clients) client->send(ts);
        }
        const json m = {{"type", "meters"},
                        {"in", engine.peakIn.exchange(0.0f)},
                        {"out", engine.peakOut.exchange(0.0f)},
                        {"beatCount", engine.beatCount.load(std::memory_order_relaxed)},
                        {"beatInBar", engine.beatInBar.load(std::memory_order_relaxed)}};
        const std::string s = m.dump();
        for (auto&& client : clients) client->send(s);
    }

    if (control.cfgDirty.load()) audio.persist();  // flush any pending rig change

    std::printf("Shutting down.\n");
    ui.stop();
    server.stop();
    audio.close();
    Pa_Terminate();
    tray.join();
    return 0;
}
