// The control plane: asset registry, preset/groove libraries, persisted rig
// state, and the JSON message dispatcher behind the WebSocket protocol.
// Extracted verbatim from helper.cpp.
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
#pragma once

#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "audio_io.h"

namespace webamp {

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
    fs::path drumsFile;
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

    json drumsJson() {
        const int sc = engine.drums.stepCount();
        json rows = json::array();
        for (int v = 0; v < webamp::DrumMachine::kVoices; ++v) {
            json steps = json::array();
            for (int s = 0; s < sc; ++s) steps.push_back(engine.drums.cell(v, s) ? 1 : 0);
            rows.push_back(steps);
        }
        return {{"on", engine.drumOn.load()},
                {"vol", engine.drumVol.load()},
                {"step", engine.drums.curStep.load()},
                {"voices", {"kick", "snare", "crash", "hihat", "ride"}},
                {"beatsPerBar", engine.drums.beatsPerBar.load()},
                {"bars", engine.drums.bars.load()},
                {"subdiv", engine.drums.subdiv.load()},
                {"stepCount", sc},
                {"pattern", rows}};
    }

    void writeDrums() {
        try {
            const json d = {{"beatsPerBar", engine.drums.beatsPerBar.load()},
                            {"bars", engine.drums.bars.load()},
                            {"subdiv", engine.drums.subdiv.load()},
                            {"vol", engine.drumVol.load()},
                            {"pattern", drumsJson()["pattern"]}};
            std::ofstream f(drumsFile);
            f << d.dump();
        } catch (...) {}
    }
    void loadDrumsFromDisk() {
        try {
            std::ifstream f(drumsFile);
            if (!f) return;
            json d; f >> d;
            engine.drums.setGrid(d.value("beatsPerBar", 4), d.value("bars", 1), d.value("subdiv", 4));
            engine.drumVol.store(std::clamp(d.value("vol", 0.6f), 0.0f, 2.0f));
            const json rows = d.value("pattern", json::array());
            for (int v = 0; v < static_cast<int>(rows.size()); ++v)
                for (int s = 0; s < static_cast<int>(rows[v].size()); ++s)
                    if (rows[v][s].get<int>()) engine.drums.setCell(v, s, true);
        } catch (...) {}
    }

    // Named groove library: the working pattern is a scratch pad; grooves are
    // named snapshots of grid + pattern the user can save and switch between
    // (the drum analogue of presets). Stored in webamp-grooves.json.
    fs::path groovesFile;
    json grooves = json::array();

    void loadGroovesFromDisk() {
        try {
            std::ifstream f(groovesFile);
            if (!f) return;
            json j; f >> j;
            if (j.is_array()) grooves = j;
        } catch (...) { grooves = json::array(); }
    }
    void writeGrooves() {
        try { std::ofstream f(groovesFile); f << grooves.dump(2); } catch (...) {}
    }
    json captureGroove(const std::string& name) {
        return {{"name", name},
                {"beatsPerBar", engine.drums.beatsPerBar.load()},
                {"bars", engine.drums.bars.load()},
                {"subdiv", engine.drums.subdiv.load()},
                {"pattern", drumsJson()["pattern"]}};
    }
    void applyGroove(const json& g) {
        engine.drums.setGrid(g.value("beatsPerBar", 4), g.value("bars", 1), g.value("subdiv", 4));
        const json rows = g.value("pattern", json::array());
        for (int v = 0; v < static_cast<int>(rows.size()); ++v)
            for (int s = 0; s < static_cast<int>(rows[v].size()); ++s)
                if (rows[v][s].get<int>()) engine.drums.setCell(v, s, true);
        writeDrums();  // the loaded groove becomes the working pattern
    }

    json stateJson() {
        json models = json::array(), irs = json::array();
        for (const auto& p : assets.models) models.push_back(Assets::displayName(p));
        for (const auto& p : assets.irs) irs.push_back(Assets::displayName(p));
        json pedals = pedalsJson();
        json audio = audioJson();
        json drums = drumsJson();
        json presetNames = json::array();
        for (const auto& pr : presets) presetNames.push_back(pr.value("name", std::string()));
        json grooveNames = json::array();
        for (const auto& gr : grooves) grooveNames.push_back(gr.value("name", std::string()));
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
              {"drumVol", engine.drumVol.load()},
              {"tunerOn", engine.tunerOn.load()}}},
            {"model", engine.modelName},
            {"ir", engine.irName},
            {"models", models},
            {"irs", irs},
            {"pedals", pedals},
            {"audio", audio},
            {"drums", drums},
            {"presets", presetNames},
            {"grooves", grooveNames},
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
            else if (id == "drumOn") engine.drumOn.store(v != 0.0f);
            else if (id == "drumVol") { engine.drumVol.store(std::clamp(v, 0.0f, 2.0f)); writeDrums(); }
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
        if (type == "setDrumCell") {
            engine.drums.setCell(msg.value("voice", -1), msg.value("step", -1), msg.value("on", false));
            writeDrums();
            *changed = true;
            return stateJson();
        }
        if (type == "setDrumGrid") {
            engine.drums.setGrid(msg.value("beatsPerBar", 4), msg.value("bars", 1),
                                 msg.value("subdiv", 4));
            writeDrums();
            *changed = true;
            return stateJson();
        }
        if (type == "clearDrums") {
            engine.drums.clear();
            writeDrums();
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
        if (type == "saveGroove") {
            std::string name = msg.value("name", std::string());
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!name.empty() && name.front() == ' ') name.erase(name.begin());
            if (name.empty()) return {{"type", "error"}, {"message", "groove name required"}};
            const json groove = captureGroove(name);
            bool replaced = false;
            for (auto& gr : grooves)
                if (gr.value("name", std::string()) == name) { gr = groove; replaced = true; break; }
            if (!replaced) grooves.push_back(groove);
            writeGrooves();
            *changed = true;
            return stateJson();
        }
        if (type == "loadGroove") {
            const std::string name = msg.value("name", std::string());
            for (const auto& gr : grooves)
                if (gr.value("name", std::string()) == name) {
                    applyGroove(gr);
                    *changed = true;
                    return stateJson();
                }
            return {{"type", "error"}, {"message", "unknown groove: " + name}};
        }
        if (type == "deleteGroove") {
            const std::string name = msg.value("name", std::string());
            const size_t before = grooves.size();
            for (auto it = grooves.begin(); it != grooves.end(); ++it)
                if (it->value("name", std::string()) == name) { grooves.erase(it); break; }
            if (grooves.size() == before) return {{"type", "error"}, {"message", "unknown groove: " + name}};
            writeGrooves();
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

}  // namespace webamp
