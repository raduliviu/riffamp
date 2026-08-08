// webamp-helper: the native audio engine, remote-controlled over ws://127.0.0.1.
//
//   webamp-helper --assets <dir> [--port 43717] [--api asio|wasapi|wdmks|coreaudio]
//                 [--buffer 64] [--sr 48000] [--in-ch 2]
//
// This file is the platform-neutral orchestrator: engine + assets + config +
// servers + meter loop. The signal chain lives in engine/engine.h, device and
// stream handling in engine/audio_io.h, the JSON protocol in engine/control.h,
// and everything OS-specific (tray icon / signals, single-instance, fatals)
// behind platform/platform.h.
//
// Security: binds 127.0.0.1 only; Origin allowlist (localhost/127.0.0.1 pages
// or no Origin for native tools). Pairing codes: TODO with the real web UI (P2c).

// Single-header impl must be compiled exactly once, before any engine header
// pulls in dr_wav.h declarations.
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "engine.h"
#include "audio_io.h"
#include "control.h"
#include "platform.h"

#include <ixwebsocket/IXHttpServer.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include "web_ui.h"  // generated: kWebUiHtml[], kWebUiHtml_len

using namespace webamp;
namespace fs = std::filesystem;
using nlohmann::json;

int main(int argc, char** argv) {
    if (!platform::initApp()) return 0;  // another instance runs; its UI was opened

    Engine engine;
    engine.opt.assets = (platform::exeDir() / "assets").string();  // default; --assets overrides
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
        platform::fatalAlert(m);
        return 1;
    };
    if (!fs::is_directory(engine.opt.assets))
        return fatal("Assets folder not found: " + engine.opt.assets +
                     "\n\nPut your .nam models and .wav IRs there, or pass --assets <dir>.");

    // Load persisted settings before initBuffers so the buffer size takes effect
    // (DSP buffers are sized here). Devices are resolved later (need PortAudio).
    const fs::path configPath = platform::exeDir() / "webamp-config.json";
    const Config cfg = loadConfig(configPath);
    if (cfg.present) {
        if (cfg.buffer == 64 || cfg.buffer == 128 || cfg.buffer == 256) engine.opt.buffer = cfg.buffer;
        engine.opt.inCh = std::clamp(cfg.inCh, 1, 8);
    }
    engine.initBuffers();

    Control control{engine};
    control.presetsFile = platform::exeDir() / "webamp-presets.json";
    control.loadPresetsFromDisk();
    control.drumsFile = platform::exeDir() / "webamp-drums.json";
    control.loadDrumsFromDisk();
    control.groovesFile = platform::exeDir() / "webamp-grooves.json";
    control.loadGroovesFromDisk();
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

    // Device order of preference: saved config → the interface's native driver →
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
    ix::HttpServer ui(platform::kUiPort, "127.0.0.1");
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
    std::printf("UI: %s\n", platform::kUiUrl);

    platform::gToggleMute = [&engine, &control, &server]() {
        engine.mute.store(!engine.mute.load());
        const std::string s = control.stateJson().dump();
        for (auto&& client : server.getClients()) client->send(s);
    };
    platform::startShell();

    // Meter + beat broadcast loop (~25 Hz) until the shell quits us.
    int tunerTick = 0;
    std::vector<float> tunerWin(4096);
    while (platform::gRunning.load()) {
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
                        {"beatInBar", engine.beatInBar.load(std::memory_order_relaxed)},
                        {"drumStep", engine.drumOn.load(std::memory_order_relaxed)
                                         ? engine.drums.curStep.load(std::memory_order_relaxed) : -1}};
        const std::string s = m.dump();
        for (auto&& client : clients) client->send(s);
    }

    if (control.cfgDirty.load()) audio.persist();  // flush any pending rig change

    std::printf("Shutting down.\n");
    ui.stop();
    server.stop();
    audio.close();
    Pa_Terminate();
    platform::stopShell();
    return 0;
}
