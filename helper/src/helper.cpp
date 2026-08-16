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
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "engine.h"
#include "audio_io.h"
#include "control.h"
#include "pairing.h"
#include "picking_tracker.h"
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

    // Pairing: local origins are trusted; the hosted app must pair once with a
    // code printed here. Per-connection trust is tracked by socket pointer.
    Pairing pairing;
    pairing.init(platform::exeDir() / "webamp-paired.json");
    constexpr int kMaxPairAttempts = 5;
    struct Conn { bool trusted = false; std::string origin; int attempts = 0; };
    std::map<ix::WebSocket*, Conn> conns;
    std::mutex connsMx;

    ix::initNetSystem();
    ix::WebSocketServer server(engine.opt.port, "127.0.0.1");

    // Send a payload only to paired/local connections — an unpaired origin must
    // never receive state (device names, rig) or meters.
    auto broadcast = [&](const std::string& s) {
        std::lock_guard<std::mutex> lk(connsMx);
        for (auto&& client : server.getClients()) {
            const auto it = conns.find(client.get());
            if (it != conns.end() && it->second.trusted) client->send(s);
        }
    };

    server.setOnClientMessageCallback([&](std::shared_ptr<ix::ConnectionState> state,
                                          ix::WebSocket& ws, const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Open) {
            const auto it = msg->openInfo.headers.find("Origin");
            const std::string origin = it != msg->openInfo.headers.end() ? it->second : "";
            const bool trusted = pairing.trusted(origin);
            {
                std::lock_guard<std::mutex> lk(connsMx);
                conns[&ws] = Conn{trusted, origin, 0};
            }
            if (trusted) {
                std::printf("Client connected (origin: %s)\n",
                            origin.empty() ? "<none>" : origin.c_str());
            } else {
                std::printf("Unpaired origin awaiting pairing: %s\n", origin.c_str());
                ws.send(json({{"type", "needPair"}}).dump());
            }
            return;
        }
        if (msg->type == ix::WebSocketMessageType::Close) {
            std::lock_guard<std::mutex> lk(connsMx);
            conns.erase(&ws);
            return;
        }
        if (msg->type != ix::WebSocketMessageType::Message) return;

        bool trusted;
        std::string origin;
        {
            std::lock_guard<std::mutex> lk(connsMx);
            const auto it = conns.find(&ws);
            trusted = it != conns.end() && it->second.trusted;
            if (it != conns.end()) origin = it->second.origin;
        }

        // Untrusted connections may only pair. Everything else → needPair.
        if (!trusted) {
            json in;
            try {
                in = json::parse(msg->str);
            } catch (...) {
                ws.send(json({{"type", "error"}, {"message", "bad json"}}).dump());
                return;
            }
            if (in.value("type", "") != "pair") {
                ws.send(json({{"type", "needPair"}}).dump());
                return;
            }
            if (pairing.codeMatches(in.value("code", ""))) {
                pairing.approve(origin);
                {
                    std::lock_guard<std::mutex> lk(connsMx);
                    if (auto it = conns.find(&ws); it != conns.end()) it->second.trusted = true;
                }
                std::printf("Paired new origin: %s\n", origin.c_str());
                ws.send(control.stateJson().dump());  // like hello: client is now live
            } else {
                int left = 0;
                bool tooMany = false;
                {
                    std::lock_guard<std::mutex> lk(connsMx);
                    if (auto it = conns.find(&ws); it != conns.end()) {
                        tooMany = ++it->second.attempts >= kMaxPairAttempts;
                        left = kMaxPairAttempts - it->second.attempts;
                    }
                }
                if (tooMany) ws.close(1008, "too many pairing attempts");
                else ws.send(json({{"type", "pairFailed"}, {"attemptsLeft", left}}).dump());
            }
            return;
        }

        json reply;
        bool changed = false;
        try {
            reply = control.handle(json::parse(msg->str), &changed);
        } catch (const std::exception& ex) {
            reply = {{"type", "error"}, {"message", ex.what()}};
        }
        if (changed) broadcast(reply.dump());
        else ws.send(reply.dump());
    });
    auto res = server.listen();
    if (!res.first) return fatal("Control port busy (is webamp already running?): " + res.second);
    server.start();
    std::printf("Control server: ws://127.0.0.1:%d\n", engine.opt.port);
    std::printf("Pairing code (only the hosted app needs this): %s\n", pairing.code.c_str());

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

    platform::gToggleMute = [&engine, &control, &broadcast]() {
        engine.mute.store(!engine.mute.load());
        broadcast(control.stateJson().dump());
    };
    platform::startShell();

    // Meter + beat broadcast loop (~25 Hz) until the shell quits us.
    int tunerTick = 0;
    std::vector<float> tunerWin(4096);
    PickingTracker picking;
    bool pickingWasOn = false;
    // Pick-run feeder: independent ring cursors (the tracker trims its window;
    // a run must keep every event). Start at "now" so nothing replays.
    uint32_t runClickRd = engine.clickPos.load(std::memory_order_acquire);
    uint32_t runOnsetRd = engine.onsetPos.load(std::memory_order_acquire);
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
        // the audio thread. (tunerTick advances once per loop, below; tuner
        // and picking analysis run on alternating phases.)
        if (engine.tunerOn.load(std::memory_order_relaxed) && (tunerTick & 1) == 0) {
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
            broadcast(t.dump());
        }

        // Picking trainer stats (~12 Hz, alternating with the tuner's ticks).
        const bool pickingOn = engine.pickOn.load(std::memory_order_relaxed);
        if (pickingOn && !pickingWasOn) picking.reset(engine);  // fresh session
        pickingWasOn = pickingOn;
        if (pickingOn && (tunerTick & 1) == 1) {
            const uint64_t now = engine.sampleClock.load(std::memory_order_acquire);
            const double sr = engine.opt.sr;
            const int beats = engine.metroBeats.load(std::memory_order_relaxed);
            const double bpm = engine.metroBpm.load(std::memory_order_relaxed);
            // Keep two bars of history for the timeline strip.
            picking.update(engine, now,
                           static_cast<uint64_t>(sr * 60.0 / std::max(20.0, bpm) * beats * 2));
            broadcast(picking.message(now, sr, bpm, beats).dump());
        }

        // Pick run (P5b): feed drained timestamps; the boundary click stops the
        // metronome, poll() yields ~12 Hz progress and then the result once.
        {
            bool ended = false;
            drainRing(engine.clickTs, engine.clickPos, runClickRd,
                      [&](uint64_t ts) { ended |= engine.pickRun.feedClick(ts); });
            drainRing(engine.onsetTs, engine.onsetPos, runOnsetRd,
                      [&](uint64_t ts) { engine.pickRun.feedOnset(ts); });
            if (ended) {
                engine.metroOn.store(false);
                broadcast(control.stateJson().dump());  // metronome button updates now
            }
            const json pr =
                engine.pickRun.poll(engine.sampleClock.load(std::memory_order_acquire));
            if (!pr.is_null() &&
                (pr["type"] == "pickRunResult" || (tunerTick & 1) == 1)) {
                broadcast(pr.dump());
                // Run over: flush the run's capture to disk (block below).
                if (pr["type"] == "pickRunResult" &&
                    engine.captureState.load(std::memory_order_relaxed) == 2)
                    engine.captureState.store(3);
            }
        }

        // Debug capture finished: write the wav beside the exe (detector tuning
        // against real playing — analyze with `picking_test <wav>`).
        if (engine.captureState.load(std::memory_order_acquire) == 3) {
            engine.captureState.store(0);
            const fs::path wavPath = platform::exeDir() / "webamp-capture.wav";
            drwav_data_format fmt{};
            fmt.container = drwav_container_riff;
            fmt.format = DR_WAVE_FORMAT_IEEE_FLOAT;
            fmt.channels = 1;
            fmt.sampleRate = static_cast<drwav_uint32>(engine.opt.sr);
            fmt.bitsPerSample = 32;
            drwav wav;
            if (drwav_init_file_write(&wav, wavPath.string().c_str(), &fmt, nullptr)) {
                drwav_write_pcm_frames(&wav, engine.capturePos.load(), engine.captureBuf.data());
                drwav_uninit(&wav);
                std::printf("Capture written: %s\n", wavPath.string().c_str());
                broadcast(json{{"type", "captureDone"},
                               {"file", wavPath.string()}}.dump());
            }
        }
        ++tunerTick;
        const json m = {{"type", "meters"},
                        {"in", engine.peakIn.exchange(0.0f)},
                        {"out", engine.peakOut.exchange(0.0f)},
                        {"beatCount", engine.beatCount.load(std::memory_order_relaxed)},
                        {"beatInBar", engine.beatInBar.load(std::memory_order_relaxed)},
                        {"drumStep", engine.drumOn.load(std::memory_order_relaxed)
                                         ? engine.drums.curStep.load(std::memory_order_relaxed) : -1}};
        broadcast(m.dump());
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
