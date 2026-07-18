// P1a: native round-trip latency measurement + passthrough for the webamp helper.
// C++ port of amp-helper-p0/p0.py — same impulse-loopback method as the browser
// spike, so all numbers in this project are directly comparable.
//
// Usage:
//   p1a list
//   p1a tone        [--api asio|wasapi|wdmks] [--exclusive] [--buffer N] [--sr N] [--seconds S]
//   p1a measure     [--api asio|wasapi|wdmks] [--exclusive] [--buffer N] [--sr N] [--repeats N]
//   p1a passthrough [--api asio|wasapi|wdmks] [--exclusive] [--buffer N] [--sr N] [--gain G] [--seconds S]
//
// Physical setup for measure/tone: 1/4" cable PHONES out -> IN 2, blend at HOST.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "portaudio.h"
#include "pa_win_wasapi.h"

namespace {

constexpr const char* kDeviceMatch = "komplete";
constexpr float kImpulseThreshold = 0.08f;
constexpr int kImpulseLen = 32;

struct Options {
    std::string mode;
    std::string api = "asio";
    bool exclusive = false;
    int buffer = 128;
    int sr = 48000;
    int repeats = 5;
    double seconds = 15.0;
    float gain = 1.0f;
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

PaHostApiTypeId apiTypeFor(const std::string& api) {
    if (api == "asio") return paASIO;
    if (api == "wdmks") return paWDMKS;
    return paWASAPI;
}

// Finds the Komplete Audio input/output device indices on the requested host API.
// ASIO exposes one duplex device; WASAPI/WDM-KS expose separate in/out devices.
bool findDevices(const Options& opt, int* inDev, int* outDev) {
    const PaHostApiIndex apiIdx = Pa_HostApiTypeIdToHostApiIndex(apiTypeFor(opt.api));
    if (apiIdx < 0) {
        std::fprintf(stderr, "Host API '%s' not available in this build.\n", opt.api.c_str());
        return false;
    }
    *inDev = *outDev = -1;
    const PaHostApiInfo* apiInfo = Pa_GetHostApiInfo(apiIdx);
    for (int i = 0; i < apiInfo->deviceCount; ++i) {
        const PaDeviceIndex dev = Pa_HostApiDeviceIndexToDeviceIndex(apiIdx, i);
        const PaDeviceInfo* di = Pa_GetDeviceInfo(dev);
        if (lower(di->name).find(kDeviceMatch) == std::string::npos) continue;
        if (di->maxInputChannels > 0 && *inDev < 0) *inDev = dev;
        if (di->maxOutputChannels > 0 && *outDev < 0) *outDev = dev;
    }
    if (*inDev < 0 || *outDev < 0) {
        std::fprintf(stderr, "Komplete Audio not found on %s (in=%d out=%d).\n",
                     apiInfo->name, *inDev, *outDev);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Round-trip state machine (runs entirely on the audio callback thread).
struct RoundTrip {
    enum State { Armed, Listening, Cooldown };
    int sr;
    int repeats;
    std::atomic<State> state{Armed};
    long long frameCount = 0;
    long long emitPos = 0;
    long long listenDeadline = 0;
    long long cooldownUntil = 0;
    std::vector<long long> results;   // written by callback, read after stream stop
    std::atomic<bool> timedOut{false};
    std::atomic<bool> done{false};

    int process(const float* in, float* out, unsigned long frames, int chIn, int chOut) {
        std::memset(out, 0, frames * chOut * sizeof(float));
        const long long now = frameCount;
        switch (state.load(std::memory_order_relaxed)) {
            case Armed: {
                const unsigned long n = std::min<unsigned long>(kImpulseLen, frames);
                for (unsigned long f = 0; f < n; ++f)
                    for (int c = 0; c < chOut; ++c) out[f * chOut + c] = 1.0f;
                emitPos = now;
                listenDeadline = now + 2LL * sr;
                state.store(Listening, std::memory_order_relaxed);
                break;
            }
            case Listening: {
                for (unsigned long f = 0; f < frames; ++f) {
                    float peak = 0.0f;
                    for (int c = 0; c < chIn; ++c)
                        peak = std::max(peak, std::fabs(in[f * chIn + c]));
                    if (peak > kImpulseThreshold) {
                        results.push_back(now + static_cast<long long>(f) - emitPos);
                        cooldownUntil = now + sr / 2;
                        state.store(Cooldown, std::memory_order_relaxed);
                        break;
                    }
                }
                if (state.load(std::memory_order_relaxed) == Listening && now > listenDeadline) {
                    timedOut.store(true);
                    done.store(true);
                    frameCount += frames;
                    return paComplete;
                }
                break;
            }
            case Cooldown: {
                if (now >= cooldownUntil) {
                    if (static_cast<int>(results.size()) >= repeats) {
                        done.store(true);
                        frameCount += frames;
                        return paComplete;
                    }
                    state.store(Armed, std::memory_order_relaxed);
                }
                break;
            }
        }
        frameCount += frames;
        return paContinue;
    }
};

// ---------------------------------------------------------------------------
// Shared callback context for all modes.
struct Context {
    Options opt;
    RoundTrip* rt = nullptr;          // measure mode
    std::atomic<float> peak{0.0f};    // tone/passthrough meter
    std::atomic<long> xruns{0};
    double tonePhase = 0.0;
    int chIn = 2, chOut = 2;
};

int audioCallback(const void* input, void* output, unsigned long frames,
                  const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags flags,
                  void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    const float* in = static_cast<const float*>(input);
    float* out = static_cast<float*>(output);
    if (flags & (paInputUnderflow | paInputOverflow | paOutputUnderflow | paOutputOverflow))
        ctx->xruns.fetch_add(1, std::memory_order_relaxed);
    if (!in || !out) {  // some hosts pass null during priming
        if (out) std::memset(out, 0, frames * ctx->chOut * sizeof(float));
        return paContinue;
    }

    if (ctx->opt.mode == "measure")
        return ctx->rt->process(in, out, frames, ctx->chIn, ctx->chOut);

    float peak = 0.0f;
    for (unsigned long f = 0; f < frames; ++f)
        for (int c = 0; c < ctx->chIn; ++c)
            peak = std::max(peak, std::fabs(in[f * ctx->chIn + c]));
    float cur = ctx->peak.load(std::memory_order_relaxed);
    while (peak > cur && !ctx->peak.compare_exchange_weak(cur, peak)) {}

    if (ctx->opt.mode == "tone") {
        const double inc = 2.0 * 3.14159265358979323846 * 440.0 / ctx->opt.sr;
        for (unsigned long f = 0; f < frames; ++f) {
            const float s = 0.3f * static_cast<float>(std::sin(ctx->tonePhase));
            ctx->tonePhase += inc;
            for (int c = 0; c < ctx->chOut; ++c) out[f * ctx->chOut + c] = s;
        }
    } else {  // passthrough
        for (unsigned long f = 0; f < frames; ++f)
            for (int c = 0; c < ctx->chOut; ++c) {
                const float v = in[f * ctx->chIn + std::min(c, ctx->chIn - 1)] * ctx->opt.gain;
                out[f * ctx->chOut + c] = std::clamp(v, -1.0f, 1.0f);
            }
    }
    return paContinue;
}

PaStream* openStream(Context* ctx) {
    int inDev, outDev;
    if (!findDevices(ctx->opt, &inDev, &outDev)) return nullptr;

    PaWasapiStreamInfo wasapiInfo{};
    void* apiInfoPtr = nullptr;
    if (ctx->opt.api == "wasapi" && ctx->opt.exclusive) {
        wasapiInfo.size = sizeof(PaWasapiStreamInfo);
        wasapiInfo.hostApiType = paWASAPI;
        wasapiInfo.version = 1;
        wasapiInfo.flags = paWinWasapiExclusive;
        apiInfoPtr = &wasapiInfo;
    }

    const double latencySec = static_cast<double>(ctx->opt.buffer) / ctx->opt.sr;
    PaStreamParameters inParams{}, outParams{};
    inParams.device = inDev;
    inParams.channelCount = ctx->chIn;
    inParams.sampleFormat = paFloat32;
    inParams.suggestedLatency = latencySec;
    inParams.hostApiSpecificStreamInfo = apiInfoPtr;
    outParams.device = outDev;
    outParams.channelCount = ctx->chOut;
    outParams.sampleFormat = paFloat32;
    outParams.suggestedLatency = latencySec;
    outParams.hostApiSpecificStreamInfo = apiInfoPtr;

    PaStream* stream = nullptr;
    PaError err = Pa_OpenStream(&stream, &inParams, &outParams, ctx->opt.sr,
                                ctx->opt.buffer, paNoFlag, audioCallback, ctx);
    if (err != paNoError) {
        std::fprintf(stderr, "Pa_OpenStream failed: %s\n", Pa_GetErrorText(err));
        return nullptr;
    }
    const PaStreamInfo* si = Pa_GetStreamInfo(stream);
    const char* modeStr = (ctx->opt.api == "wasapi")
                              ? (ctx->opt.exclusive ? "exclusive" : "shared")
                              : ctx->opt.api.c_str();
    std::printf("Stream open: %s (%s), in=%d out=%d, sr=%d, buffer=%d\n",
                Pa_GetHostApiInfo(Pa_GetDeviceInfo(inDev)->hostApi)->name, modeStr,
                inDev, outDev, ctx->opt.sr, ctx->opt.buffer);
    std::printf("PortAudio-reported latency: input %.1f ms, output %.1f ms (sum %.1f ms)"
                " -- estimate only; trust the impulse measurement.\n",
                si->inputLatency * 1000, si->outputLatency * 1000,
                (si->inputLatency + si->outputLatency) * 1000);
    return stream;
}

void meterLoop(Context* ctx, double seconds, const char* label) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < end) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const float p = ctx->peak.exchange(0.0f);
        const int bars = std::min(60, static_cast<int>(p * 80));
        std::printf("\r%s input peak %5.3f |%-60s|", label, p, std::string(bars, '#').c_str());
        std::fflush(stdout);
    }
    std::printf("\n");
}

int cmdList() {
    for (PaHostApiIndex a = 0; a < Pa_GetHostApiCount(); ++a)
        std::printf("API %d: %s\n", a, Pa_GetHostApiInfo(a)->name);
    for (PaDeviceIndex d = 0; d < Pa_GetDeviceCount(); ++d) {
        const PaDeviceInfo* di = Pa_GetDeviceInfo(d);
        std::printf("  dev %2d | api %d | in %d out %d | %s\n", d, di->hostApi,
                    di->maxInputChannels, di->maxOutputChannels, di->name);
    }
    return 0;
}

int cmdMeasure(Context* ctx) {
    RoundTrip rt;
    rt.sr = ctx->opt.sr;
    rt.repeats = ctx->opt.repeats;
    rt.results.reserve(ctx->opt.repeats + 4);
    ctx->rt = &rt;

    PaStream* stream = openStream(ctx);
    if (!stream) return 1;
    std::printf("Measuring %d impulses (make no noise; loopback cable required)...\n",
                ctx->opt.repeats);
    Pa_StartStream(stream);
    while (Pa_IsStreamActive(stream) == 1 && !rt.done.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    Pa_StopStream(stream);
    Pa_CloseStream(stream);

    if (rt.results.empty()) {
        std::fprintf(stderr, "NO SIGNAL: impulse never returned. Check the loopback cable, "
                             "IN 2 gain ('tone' mode first), and IN->HOST blend at HOST.\n");
        return 1;
    }
    std::vector<double> ms;
    for (long long f : rt.results) ms.push_back(f * 1000.0 / ctx->opt.sr);
    std::sort(ms.begin(), ms.end());
    std::printf("\nRound-trip results (%zu impulses) @ %d Hz, buffer %d:\n",
                ms.size(), ctx->opt.sr, ctx->opt.buffer);
    for (size_t i = 0; i < ms.size(); ++i) std::printf("  #%zu: %7.2f ms\n", i + 1, ms[i]);
    const double median = ms[ms.size() / 2];
    std::printf("\n  MEDIAN ROUND-TRIP: %.2f ms   (%s)\n", median,
                median < 12.0 ? "PASS: playable" : "above 12 ms target");
    if (rt.timedOut.load()) std::printf("  (run ended on a timeout after some impulses)\n");
    const long x = ctx->xruns.load();
    if (x) std::printf("  xruns during run: %ld\n", x);
    return 0;
}

int cmdToneOrPassthrough(Context* ctx) {
    PaStream* stream = openStream(ctx);
    if (!stream) return 1;
    if (ctx->opt.mode == "tone")
        std::printf("Playing 440 Hz. Raise IN 2 gain until the meter clearly moves (~0.1+).\n");
    else
        std::printf("Passthrough for %.0fs (gain %.2f). Play!\n", ctx->opt.seconds, ctx->opt.gain);
    Pa_StartStream(stream);
    meterLoop(ctx, ctx->opt.seconds, ctx->opt.mode.c_str());
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    std::printf("xruns: %ld\n", ctx->xruns.load());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: p1a list|tone|measure|passthrough [options]\n");
        return 2;
    }
    Context ctx;
    ctx.opt.mode = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", what); std::exit(2); }
            return argv[++i];
        };
        if (a == "--api") ctx.opt.api = next("--api");
        else if (a == "--exclusive") ctx.opt.exclusive = true;
        else if (a == "--buffer") ctx.opt.buffer = std::atoi(next("--buffer"));
        else if (a == "--sr") ctx.opt.sr = std::atoi(next("--sr"));
        else if (a == "--repeats") ctx.opt.repeats = std::atoi(next("--repeats"));
        else if (a == "--seconds") ctx.opt.seconds = std::atof(next("--seconds"));
        else if (a == "--gain") ctx.opt.gain = static_cast<float>(std::atof(next("--gain")));
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::fprintf(stderr, "Pa_Initialize failed: %s\n", Pa_GetErrorText(err));
        return 1;
    }
    int rc = 2;
    if (ctx.opt.mode == "list") rc = cmdList();
    else if (ctx.opt.mode == "measure") rc = cmdMeasure(&ctx);
    else if (ctx.opt.mode == "tone" || ctx.opt.mode == "passthrough") rc = cmdToneOrPassthrough(&ctx);
    else std::fprintf(stderr, "unknown mode %s\n", ctx.opt.mode.c_str());
    Pa_Terminate();
    return rc;
}
