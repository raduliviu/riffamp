// The platform-neutral audio engine: signal chain state + the real-time
// process() that the PortAudio callback drives. Extracted verbatim from
// helper.cpp; no Windows dependencies.
//
// Chain: input gain -> gate -> NAM -> tone stack -> cab IR -> output gain.
// All parameters are atomics read by the audio callback; model/IR swaps happen
// on the control thread with an atomic pointer exchange + grace delete.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "portaudio.h"
#include "NAM/get_dsp.h"
#include "TwoStageFFTConvolver.h"
#include "json.hpp"
#include "dsp_extra.h"
#include "tuner.h"
#include "pedals.h"
#include "drums.h"
#include "picking.h"
#include "pick_run.h"
#include "dr_wav.h"

namespace webamp {

namespace fs = std::filesystem;
using nlohmann::json;

inline constexpr const char* kVersion = "0.2.0";
inline constexpr size_t kTailBlock = 1024;

inline std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Monotonic milliseconds, used to debounce config writes.
inline long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct Options {
    std::string assets;
#ifdef __APPLE__
    std::string api = "coreaudio";
#else
    std::string api = "asio";
#endif
    int port = 43717;
    int buffer = 128;  // safe default (~11 ms); 64 available for lowest latency
    int sr = 48000;
    int inCh = 2;
};

inline std::vector<float> loadIrFile(const fs::path& path, int streamSr, std::string* err) {
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
    // One-shot: audio thread consumes it and restarts the metronome phase so
    // the next click is beat 1 — a pick run's count-in starts clean even if
    // the metronome was already running.
    std::atomic<bool> metroRestart{false};
    std::atomic<float> metroBpm{120.0f}, metroVol{0.5f};
    std::atomic<int> metroBeats{4};
    std::atomic<long long> beatCount{0};  // clicks fired; UI flashes on change
    std::atomic<int> beatInBar{0};

    // Drum machine (groove box) — shares the metronome tempo.
    webamp::DrumMachine drums;
    std::atomic<bool> drumOn{false};
    std::atomic<float> drumVol{0.6f};

    // Tuner: audio thread writes pre-gate input into a ring; control thread
    // snapshots the last window and runs YIN on it.
    std::atomic<bool> tunerOn{false};
    static constexpr uint32_t kRingSize = 16384;  // power of two
    std::vector<float> tunerRing = std::vector<float>(kRingSize, 0.0f);
    std::atomic<uint32_t> tunerPos{0};

    // Picking trainer (P5a): onset detector on the same clean input, plus
    // sample-time rings of onset and metronome-click timestamps. Audio thread
    // writes value-then-position (release); control thread drains (acquire)
    // and turns them into notes-per-beat / evenness stats.
    webamp::OnsetDetector pick;
    std::atomic<bool> pickOn{false};
    std::atomic<float> pickSens{0.5f};
    std::atomic<int> pickTarget{4};  // notes/beat the user is practicing (drives the min-gap gate)
    bool pickWasOn = false;  // audio thread only: reset detector on enable
    std::atomic<uint64_t> sampleClock{0};  // audio-stream time, frames
    static constexpr uint32_t kEvtRing = 256;  // power of two
    std::array<uint64_t, kEvtRing> onsetTs{};
    std::atomic<uint32_t> onsetPos{0};
    std::array<uint64_t, kEvtRing> clickTs{};
    std::atomic<uint32_t> clickPos{0};

    // Pick-run recorder (P5b): control-thread only (WS handlers start/cancel,
    // the meter loop feeds it drained click/onset timestamps and polls).
    webamp::PickRun pickRun;

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
        drums.configure(sr);
        pick.configure(sr);
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

        // Picking trainer: detector runs on the clean input (pre-gate, pre-amp)
        // and works while muted, like the tuner. Reset on enable.
        const bool pOn = pickOn.load(std::memory_order_relaxed);
        if (pOn && !pickWasOn) pick.reset();
        pickWasOn = pOn;
        if (pOn) {
            pick.setSensitivity(pickSens.load(std::memory_order_relaxed));
            // Expected-rate gate: notes can't be closer than ~45% of the
            // target subdivision at the current tempo.
            pick.setMinGap(0.45f * 60.0f /
                           (std::max(30.0f, metroBpm.load(std::memory_order_relaxed)) *
                            static_cast<float>(std::max(1, pickTarget.load(std::memory_order_relaxed)))));
        }
        const uint64_t clockBase = sampleClock.load(std::memory_order_relaxed);

        float pIn = 0.0f;
        const uint32_t ringBase = tunerPos.load(std::memory_order_relaxed);
        for (unsigned long f = 0; f < frames; ++f) {
            const float raw = in[f * chIn_ + inIdx] * gIn;
            pIn = std::max(pIn, std::fabs(raw));               // meter shows the real input
            tunerRing[(ringBase + f) & (kRingSize - 1)] = raw;  // tuner works even while muted
            if (pOn && pick.process(raw)) {
                const uint32_t p = onsetPos.load(std::memory_order_relaxed);
                onsetTs[p & (kEvtRing - 1)] = clockBase + f;
                onsetPos.store(p + 1, std::memory_order_release);
            }
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
        const bool dOn = drumOn.load(std::memory_order_relaxed);
        const float dVol = drumVol.load(std::memory_order_relaxed);
        // One-shot restart (pick-run count-in): fake an off edge so blockStart
        // resets the phase and the very next sample fires beat 1.
        if (metroRestart.exchange(false, std::memory_order_relaxed)) metro.wasOn = false;
        metro.blockStart(mOn);
        drums.blockStart(dOn);

        float pOut = 0.0f;
        for (unsigned long f = 0; f < frames; ++f) {
            const long long clicksBefore = metro.clickCount;
            const float click = metro.process(mOn, mBpm, mBeats, mAccent) * mVol;
            if (metro.clickCount != clicksBefore) {  // a click fired this sample
                const uint32_t p = clickPos.load(std::memory_order_relaxed);
                clickTs[p & (kEvtRing - 1)] = clockBase + f;
                clickPos.store(p + 1, std::memory_order_release);
            }
            const float drum = drums.process(dOn, mBpm) * dVol;
            const float v = std::clamp(bufA[f] * gOut + click + drum, -1.0f, 1.0f);
            pOut = std::max(pOut, std::fabs(v));
            for (int c = 0; c < chOut_; ++c) out[f * chOut_ + c] = v;
        }
        sampleClock.store(clockBase + frames, std::memory_order_release);
        beatCount.store(metro.clickCount, std::memory_order_relaxed);
        beatInBar.store(metro.beat, std::memory_order_relaxed);
        float cur = peakIn.load(std::memory_order_relaxed);
        while (pIn > cur && !peakIn.compare_exchange_weak(cur, pIn)) {}
        cur = peakOut.load(std::memory_order_relaxed);
        while (pOut > cur && !peakOut.compare_exchange_weak(cur, pOut)) {}
    }
};

inline int audioCallback(const void* input, void* output, unsigned long frames,
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

}  // namespace webamp
