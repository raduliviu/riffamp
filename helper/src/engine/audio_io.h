// Audio device enumeration, persisted device/rig config, and the PortAudio
// stream owner (AudioIO). Extracted verbatim from helper.cpp.
//
// Host APIs are the one platform-variant concern here: Windows exposes
// ASIO/WASAPI/WDM-KS (MME/DirectSound legacy skipped), macOS exposes CoreAudio
// (low-latency by default — no ASIO equivalent needed). PortAudio defines the
// full PaHostApiTypeId enum on every platform, so the mapping needs no #ifdefs;
// absent APIs simply never enumerate.
#pragma once

#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "engine.h"

namespace webamp {

struct DeviceInfo {
    int index;
    std::string name;
    std::string api;
    int channels;
};

// Low-latency devices only: ASIO, WASAPI, WDM-KS on Windows; CoreAudio on mac.
inline std::vector<DeviceInfo> enumerateDevices(bool wantInput) {
    std::vector<DeviceInfo> out;
    for (PaHostApiIndex a = 0; a < Pa_GetHostApiCount(); ++a) {
        const PaHostApiInfo* ai = Pa_GetHostApiInfo(a);
        std::string apiName;
        if (ai->type == paASIO) apiName = "ASIO";
        else if (ai->type == paWASAPI) apiName = "WASAPI";
        else if (ai->type == paWDMKS) apiName = "WDM-KS";
        else if (ai->type == paCoreAudio) apiName = "CoreAudio";
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

inline std::string deviceApiName(int dev) {
    const PaDeviceInfo* di = Pa_GetDeviceInfo(dev);
    if (!di) return "";
    switch (Pa_GetHostApiInfo(di->hostApi)->type) {
        case paASIO: return "ASIO";
        case paWASAPI: return "WASAPI";
        case paWDMKS: return "WDM-KS";
        case paCoreAudio: return "CoreAudio";
        default: return "?";
    }
}

// Device identity that survives across launches (indices can shift).
struct DeviceRef {
    std::string name, api;
    bool operator==(const DeviceRef& o) const { return name == o.name && api == o.api; }
    bool empty() const { return name.empty(); }
};

inline int resolveDevice(const DeviceRef& ref, bool wantInput) {
    if (ref.empty()) return -1;
    for (const auto& d : enumerateDevices(wantInput))
        if (d.name == ref.name && d.api == ref.api) return d.index;
    return -1;
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

inline Config loadConfig(const fs::path& path) {
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

inline void saveConfig(const fs::path& path, const DeviceRef& in, const DeviceRef& out, int inCh,
                       int buffer, const json& amp) {
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

// Preferred-device scan: match the user's interface by name within the
// requested host API (ASIO by default on Windows, CoreAudio on mac).
inline constexpr const char* kDeviceMatch = "komplete";

inline bool findDevices(const Options& opt, int* inDev, int* outDev) {
#ifdef __APPLE__
    PaHostApiTypeId type = paCoreAudio;
    (void)opt;
#else
    PaHostApiTypeId type = paASIO;
    if (opt.api == "wasapi") type = paWASAPI;
    else if (opt.api == "wdmks") type = paWDMKS;
#endif
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

}  // namespace webamp
