// P1b: the webamp tone engine — NAM model + cabinet IR through the low-latency
// duplex path proven by P1a (7.62 ms RTL @64 on Komplete Audio ASIO).
//
//   p1b bench --model X.nam --ir Y.wav [--buffer 64] [--seconds 10]
//       Offline: load model+IR, process noise, report CPU per callback vs budget.
//   p1b run   --model X.nam --ir Y.wav [--api asio] [--buffer 64] [--sr 48000]
//             [--in-ch 2] [--gain-in 1.0] [--gain-out 1.0] [--seconds 300]
//       Live: guitar (IN 2 by default) -> NAM -> IR -> both outputs.
//
// Signal chain (mono core, dual-mono out):
//   input gain -> NAM (prewarmed) -> TwoStageFFTConvolver (energy-normalized IR)
//   -> output gain -> clip guard

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "portaudio.h"
#include "pa_win_wasapi.h"

#include "NAM/get_dsp.h"
#include "TwoStageFFTConvolver.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

namespace {

struct Options {
    std::string mode;
    std::string model;
    std::string ir;
    std::string api = "asio";
    bool exclusive = false;
    int buffer = 64;
    int sr = 48000;
    int inCh = 2;  // 1-based physical input (KA1: IN 2 = instrument jack)
    float gainIn = 1.0f;
    float gainOut = 1.0f;
    double seconds = 300.0;
};

constexpr const char* kDeviceMatch = "komplete";
constexpr size_t kTailBlock = 1024;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// --- IR loading ------------------------------------------------------------
std::vector<float> loadIr(const std::string& path, int streamSr) {
    unsigned int ch = 0, sr = 0;
    drwav_uint64 frames = 0;
    float* data = drwav_open_file_and_read_pcm_frames_f32(path.c_str(), &ch, &sr, &frames, nullptr);
    if (!data) {
        std::fprintf(stderr, "Failed to load IR: %s\n", path.c_str());
        std::exit(1);
    }
    std::vector<float> ir(frames);
    for (drwav_uint64 f = 0; f < frames; ++f) ir[f] = data[f * ch];  // first channel
    drwav_free(data, nullptr);
    if (static_cast<int>(sr) != streamSr)
        std::fprintf(stderr, "WARNING: IR sample rate %u != stream %d — cab tone will shift.\n",
                     sr, streamSr);
    // Energy-normalize so the cab neither booms nor vanishes.
    double energy = 0.0;
    for (float v : ir) energy += static_cast<double>(v) * v;
    if (energy > 0.0) {
        const float k = static_cast<float>(1.0 / std::sqrt(energy));
        for (float& v : ir) v *= k;
    }
    std::printf("IR: %s\n    %llu frames @ %u Hz, %u ch (using ch 1), energy-normalized\n",
                path.c_str(), static_cast<unsigned long long>(frames), sr, ch);
    return ir;
}

// --- Engine ----------------------------------------------------------------
struct Engine {
    Options opt;
    std::unique_ptr<nam::DSP> model;
    fftconvolver::TwoStageFFTConvolver convolver;
    std::vector<float> bufIn, bufNam, bufCab;
    std::atomic<float> peakIn{0.0f}, peakOut{0.0f};
    std::atomic<long> xruns{0};
    std::atomic<long long> worstNs{0};
    std::atomic<long long> worstIdx{-1};
    std::atomic<long long> overBudget{0};
    std::atomic<long long> totalNs{0};
    std::atomic<long long> callbacks{0};
    int chIn = 2, chOut = 2;

    void load() {
        std::printf("Loading NAM model: %s\n", opt.model.c_str());
        model = nam::get_dsp(std::filesystem::path(opt.model));
        std::printf("    expected sample rate: %.0f Hz%s\n", model->GetExpectedSampleRate(),
                    model->GetExpectedSampleRate() != opt.sr ? "  (MISMATCH vs stream!)" : "");
        if (model->HasLoudness())
            std::printf("    capture loudness: %.1f dB\n", model->GetLoudness());
        model->ResetAndPrewarm(opt.sr, opt.buffer);

        auto ir = loadIr(opt.ir, opt.sr);
        convolver.init(opt.buffer, kTailBlock, ir.data(), ir.size());

        bufIn.resize(opt.buffer);
        bufNam.resize(opt.buffer);
        bufCab.resize(opt.buffer);
    }

    // One block through the whole chain. in/out are interleaved device buffers
    // (in may be null in bench mode when bufIn is pre-filled).
    void processBlock(const float* in, float* out, unsigned long frames) {
        const auto t0 = std::chrono::steady_clock::now();
        const int inIdx = std::min(opt.inCh - 1, chIn - 1);

        float pIn = 0.0f;
        if (in) {
            for (unsigned long f = 0; f < frames; ++f) {
                const float v = in[f * chIn + inIdx] * opt.gainIn;
                bufIn[f] = v;
                pIn = std::max(pIn, std::fabs(v));
            }
        }

        float* namIn = bufIn.data();
        float* namOut = bufNam.data();
        model->process(&namIn, &namOut, static_cast<int>(frames));
        convolver.process(bufNam.data(), bufCab.data(), frames);

        float pOut = 0.0f;
        for (unsigned long f = 0; f < frames; ++f) {
            const float v = std::clamp(bufCab[f] * opt.gainOut, -1.0f, 1.0f);
            bufCab[f] = v;
            pOut = std::max(pOut, std::fabs(v));
            if (out)
                for (int c = 0; c < chOut; ++c) out[f * chOut + c] = v;
        }

        float cur = peakIn.load(std::memory_order_relaxed);
        while (pIn > cur && !peakIn.compare_exchange_weak(cur, pIn)) {}
        cur = peakOut.load(std::memory_order_relaxed);
        while (pOut > cur && !peakOut.compare_exchange_weak(cur, pOut)) {}

        const long long ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0)
                .count();
        totalNs.fetch_add(ns, std::memory_order_relaxed);
        const long long idx = callbacks.fetch_add(1, std::memory_order_relaxed);
        const long long budgetNs = 1000000000LL * opt.buffer / opt.sr;
        if (ns > budgetNs) overBudget.fetch_add(1, std::memory_order_relaxed);
        long long worst = worstNs.load(std::memory_order_relaxed);
        while (ns > worst && !worstNs.compare_exchange_weak(worst, ns)) {}
        if (ns == worstNs.load(std::memory_order_relaxed))
            worstIdx.store(idx, std::memory_order_relaxed);
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
    e->processBlock(static_cast<const float*>(input), static_cast<float*>(output), frames);
    return paContinue;
}

bool findDevices(const Options& opt, int* inDev, int* outDev) {
    PaHostApiTypeId type = paASIO;
    if (opt.api == "wasapi") type = paWASAPI;
    else if (opt.api == "wdmks") type = paWDMKS;
    const PaHostApiIndex apiIdx = Pa_HostApiTypeIdToHostApiIndex(type);
    if (apiIdx < 0) {
        std::fprintf(stderr, "Host API '%s' not available.\n", opt.api.c_str());
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
        std::fprintf(stderr, "Komplete Audio not found on %s.\n", apiInfo->name);
        return false;
    }
    return true;
}

void printCpuStats(const Engine& e) {
    const long long n = e.callbacks.load();
    if (!n) return;
    const double budgetUs = 1e6 * e.opt.buffer / e.opt.sr;
    const double avgUs = e.totalNs.load() / 1000.0 / n;
    const double worstUs = e.worstNs.load() / 1000.0;
    std::printf("DSP cost per %d-sample block (budget %.0f us): avg %.1f us (%.1f%%), "
                "worst %.1f us (%.1f%%) at block %lld, over-budget blocks %lld/%lld, xruns %ld\n",
                e.opt.buffer, budgetUs, avgUs, 100.0 * avgUs / budgetUs, worstUs,
                100.0 * worstUs / budgetUs, e.worstIdx.load(), e.overBudget.load(), n,
                e.xruns.load());
}

int cmdBench(Engine& e) {
    e.load();
    // Deterministic pseudo-noise input at guitar-ish level.
    unsigned int rng = 0x12345678u;
    const long long blocks =
        static_cast<long long>(e.opt.seconds * e.opt.sr / e.opt.buffer);
    std::printf("Bench: %lld blocks of %d samples (%.1f s of audio)...\n", blocks, e.opt.buffer,
                e.opt.seconds);
    for (long long b = 0; b < blocks; ++b) {
        for (int f = 0; f < e.opt.buffer; ++f) {
            rng = rng * 1664525u + 1013904223u;
            e.bufIn[f] = 0.2f * (static_cast<int>(rng >> 9) / 4194304.0f - 1.0f);
        }
        e.processBlock(nullptr, nullptr, e.opt.buffer);
    }
    printCpuStats(e);
    const double rt = e.opt.seconds /
                      (e.totalNs.load() / 1e9);
    std::printf("Realtime factor: %.1fx\n", rt);
    return 0;
}

int cmdRun(Engine& e) {
    e.load();
    int inDev, outDev;
    if (!findDevices(e.opt, &inDev, &outDev)) return 1;

    PaWasapiStreamInfo wasapiInfo{};
    void* apiInfoPtr = nullptr;
    if (e.opt.api == "wasapi" && e.opt.exclusive) {
        wasapiInfo.size = sizeof(PaWasapiStreamInfo);
        wasapiInfo.hostApiType = paWASAPI;
        wasapiInfo.version = 1;
        wasapiInfo.flags = paWinWasapiExclusive;
        apiInfoPtr = &wasapiInfo;
    }
    const double latencySec = static_cast<double>(e.opt.buffer) / e.opt.sr;
    PaStreamParameters inP{}, outP{};
    inP.device = inDev;
    inP.channelCount = e.chIn;
    inP.sampleFormat = paFloat32;
    inP.suggestedLatency = latencySec;
    inP.hostApiSpecificStreamInfo = apiInfoPtr;
    outP.device = outDev;
    outP.channelCount = e.chOut;
    outP.sampleFormat = paFloat32;
    outP.suggestedLatency = latencySec;
    outP.hostApiSpecificStreamInfo = apiInfoPtr;

    PaStream* stream = nullptr;
    PaError err = Pa_OpenStream(&stream, &inP, &outP, e.opt.sr, e.opt.buffer, paNoFlag,
                                audioCallback, &e);
    if (err != paNoError) {
        std::fprintf(stderr, "Pa_OpenStream failed: %s\n", Pa_GetErrorText(err));
        return 1;
    }
    const PaStreamInfo* si = Pa_GetStreamInfo(stream);
    std::printf("Stream open: %s, buffer %d @ %d Hz (reported in %.1f + out %.1f ms)\n",
                e.opt.api.c_str(), e.opt.buffer, e.opt.sr, si->inputLatency * 1000,
                si->outputLatency * 1000);
    std::printf("PLAY! Guitar on IN %d. Running %.0f s (Ctrl+C to stop)...\n", e.opt.inCh,
                e.opt.seconds);
    Pa_StartStream(stream);
    const auto end =
        std::chrono::steady_clock::now() + std::chrono::duration<double>(e.opt.seconds);
    while (std::chrono::steady_clock::now() < end && Pa_IsStreamActive(stream) == 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const float pi = e.peakIn.exchange(0.0f);
        const float po = e.peakOut.exchange(0.0f);
        std::printf("\rin %5.3f |%-20s| out %5.3f |%-20s| xruns %ld   ", pi,
                    std::string(std::min(20, static_cast<int>(pi * 25)), '#').c_str(), po,
                    std::string(std::min(20, static_cast<int>(po * 25)), '#').c_str(),
                    e.xruns.load());
        std::fflush(stdout);
    }
    std::printf("\n");
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    printCpuStats(e);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: p1b bench|run --model X.nam --ir Y.wav [options]\n");
        return 2;
    }
    Engine e;
    e.opt.mode = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", what); std::exit(2); }
            return argv[++i];
        };
        if (a == "--model") e.opt.model = next("--model");
        else if (a == "--ir") e.opt.ir = next("--ir");
        else if (a == "--api") e.opt.api = next("--api");
        else if (a == "--exclusive") e.opt.exclusive = true;
        else if (a == "--buffer") e.opt.buffer = std::atoi(next("--buffer"));
        else if (a == "--sr") e.opt.sr = std::atoi(next("--sr"));
        else if (a == "--in-ch") e.opt.inCh = std::atoi(next("--in-ch"));
        else if (a == "--gain-in") e.opt.gainIn = static_cast<float>(std::atof(next("--gain-in")));
        else if (a == "--gain-out") e.opt.gainOut = static_cast<float>(std::atof(next("--gain-out")));
        else if (a == "--seconds") e.opt.seconds = std::atof(next("--seconds"));
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    if (e.opt.model.empty() || e.opt.ir.empty()) {
        std::fprintf(stderr, "--model and --ir are required\n");
        return 2;
    }
    if (e.opt.mode == "bench" && e.opt.seconds > 60.0) e.opt.seconds = 10.0;

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::fprintf(stderr, "Pa_Initialize failed: %s\n", Pa_GetErrorText(err));
        return 1;
    }
    int rc = 2;
    try {
        if (e.opt.mode == "bench") rc = cmdBench(e);
        else if (e.opt.mode == "run") rc = cmdRun(e);
        else std::fprintf(stderr, "unknown mode %s\n", e.opt.mode.c_str());
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "ERROR: %s\n", ex.what());
        rc = 1;
    }
    Pa_Terminate();
    return rc;
}
