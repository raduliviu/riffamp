// Drum machine (groove box) for webamp: a step sequencer with 5 synthesized
// voices (kick/snare/crash/hihat/ride), stepped from the audio callback in sync
// with the metronome tempo. Voices are synthesized (no samples).
//
// Grid: selectable time signature (3/4 or 4/4), 1-4 bars, and resolution
// (subdivisions per beat: 1=quarter, 2=8th, 4=16th, 8=32nd). Total steps =
// beatsPerBar * bars * subdiv, capped at 128 (4 bars of 4/4 at 32nds). Drums are
// one-shots, so coarser note values are just hits with empty cells after.
//
// Scope fence (deliberate): on/off per cell only. No velocity, swing, kits,
// multi-pattern songs, sample import, or export.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace webamp {

enum DrumKind { DK_KICK = 0, DK_SNARE, DK_CRASH, DK_HIHAT, DK_RIDE };

// One monophonic, retriggerable synthesized drum voice.
struct DrumVoice {
    float sr = 48000;
    int kind = DK_KICK;
    float env = 0, envCoef = 0;
    float phase = 0;
    float pitchEnv = 0, pitchCoef = 0;
    float lp = 0;
    uint32_t rng = 2463534242u;

    void configure(float sampleRate, int k) {
        sr = sampleRate;
        kind = k;
        const float dec = kind == DK_KICK    ? 0.28f
                        : kind == DK_SNARE   ? 0.16f
                        : kind == DK_HIHAT   ? 0.045f
                        : kind == DK_RIDE    ? 0.55f
                                             : 1.30f;
        envCoef = std::exp(-1.0f / (dec * sr));
        pitchCoef = std::exp(-1.0f / (0.03f * sr));
    }
    float noise() {
        rng = rng * 1664525u + 1013904223u;
        return static_cast<int>(rng >> 9) / 4194304.0f - 1.0f;
    }
    void trigger() { env = 1.0f; pitchEnv = 1.0f; phase = 0.0f; }

    float process() {
        if (env < 1e-4f) return 0.0f;
        constexpr float kTwoPi = 6.2831853071795864f;
        float out = 0.0f;
        if (kind == DK_KICK) {
            const float f = 48.0f + 95.0f * pitchEnv;
            phase += kTwoPi * f / sr;
            if (phase > kTwoPi) phase -= kTwoPi;
            out = std::sin(phase) * env;
            pitchEnv *= pitchCoef;
        } else if (kind == DK_SNARE) {
            phase += kTwoPi * 185.0f / sr;
            if (phase > kTwoPi) phase -= kTwoPi;
            out = (noise() * 0.7f + std::sin(phase) * 0.45f) * env * 0.85f;
        } else {
            const float n = noise();
            lp += 0.55f * (n - lp);
            out = (n - lp) * env;
            if (kind == DK_RIDE) {
                phase += kTwoPi * 3400.0f / sr;
                if (phase > kTwoPi) phase -= kTwoPi;
                out += std::sin(phase) * env * 0.12f;
            }
            out *= (kind == DK_HIHAT ? 0.5f : 0.45f);
        }
        env *= envCoef;
        return out;
    }
};

struct DrumMachine {
    static constexpr int kVoices = 5;    // kick, snare, crash, hihat, ride
    static constexpr int kMaxSteps = 128;
    DrumVoice voices[kVoices];
    std::atomic<uint64_t> pattern[kVoices][2];  // 128 step bits per voice
    std::atomic<int> beatsPerBar{4};            // 3 (3/4) or 4 (4/4)
    std::atomic<int> bars{1};                   // 1..4
    std::atomic<int> subdiv{4};                 // 1=quarter, 2=8th, 4=16th, 8=32nd
    std::atomic<int> curStep{-1};

    float sr = 48000;
    long long toStep = 0;
    int step = -1;
    bool wasOn = false;

    int stepCount() const {
        return std::min(kMaxSteps, beatsPerBar.load() * bars.load() * subdiv.load());
    }
    void configure(float sampleRate) {
        sr = sampleRate;
        const int kinds[kVoices] = {DK_KICK, DK_SNARE, DK_CRASH, DK_HIHAT, DK_RIDE};
        for (int i = 0; i < kVoices; ++i) {
            voices[i].configure(sr, kinds[i]);
            pattern[i][0].store(0);
            pattern[i][1].store(0);
        }
    }
    void blockStart(bool on) {
        if (on && !wasOn) { step = -1; toStep = 0; curStep.store(-1); }
        wasOn = on;
    }
    float process(bool on, float bpm) {
        const int sc = stepCount();
        const int sd = std::max(1, subdiv.load(std::memory_order_relaxed));
        if (on && --toStep <= 0) {
            toStep = static_cast<long long>(sr * 60.0f / std::max(30.0f, bpm) / sd);
            step = sc > 0 ? (step + 1) % sc : 0;
            curStep.store(step, std::memory_order_relaxed);
            for (int i = 0; i < kVoices; ++i) {
                const int w = step >> 6, b = step & 63;
                if (pattern[i][w].load(std::memory_order_relaxed) & (1ull << b)) voices[i].trigger();
            }
        }
        float mix = 0.0f;
        for (int i = 0; i < kVoices; ++i) mix += voices[i].process();
        return mix;
    }
    void setCell(int v, int s, bool on) {
        if (v < 0 || v >= kVoices || s < 0 || s >= kMaxSteps) return;
        const int w = s >> 6;
        const uint64_t bit = 1ull << (s & 63);
        if (on) pattern[v][w].fetch_or(bit);
        else pattern[v][w].fetch_and(~bit);
    }
    // Changing the grid layout clears the pattern (cells no longer line up).
    void setGrid(int bpb, int br, int sd) {
        beatsPerBar.store(bpb == 3 ? 3 : 4);
        bars.store(std::clamp(br, 1, 4));
        subdiv.store(sd == 1 || sd == 2 || sd == 4 || sd == 8 ? sd : 4);
        clear();
    }
    void clear() {
        for (int i = 0; i < kVoices; ++i) { pattern[i][0].store(0); pattern[i][1].store(0); }
    }
    bool cell(int v, int s) const {
        return (pattern[v][s >> 6].load() >> (s & 63)) & 1;
    }
};

}  // namespace webamp
