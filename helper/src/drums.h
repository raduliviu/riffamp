// Drum machine (groove box) for webamp: a 16-step sequencer with 5 synthesized
// voices (kick/snare/crash/hihat/ride), stepped from the audio callback in sync
// with the metronome tempo. Voices are synthesized (no samples) — same approach
// as the metronome click and tuner, so no audio assets or licensing.
//
// Scope fence (deliberate): one bar of 16 sixteenth-notes, on/off per cell only.
// No velocity, swing, kits, multi-bar songs, sample import, or export — those
// would turn a practice groove box into a worse DAW.
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
    float env = 0, envCoef = 0;      // amplitude envelope (exp decay)
    float phase = 0;                 // tonal oscillator phase
    float pitchEnv = 0, pitchCoef = 0;  // kick pitch drop
    float lp = 0;                    // one-pole lowpass state (for HP = x - lp)
    uint32_t rng = 2463534242u;

    void configure(float sampleRate, int k) {
        sr = sampleRate;
        kind = k;
        const float dec = kind == DK_KICK    ? 0.28f
                        : kind == DK_SNARE   ? 0.16f
                        : kind == DK_HIHAT   ? 0.045f
                        : kind == DK_RIDE    ? 0.55f
                                             : 1.30f;  // crash rings longest
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
            const float f = 48.0f + 95.0f * pitchEnv;  // ~143 Hz -> 48 Hz thump
            phase += kTwoPi * f / sr;
            if (phase > kTwoPi) phase -= kTwoPi;
            out = std::sin(phase) * env;
            pitchEnv *= pitchCoef;
        } else if (kind == DK_SNARE) {
            phase += kTwoPi * 185.0f / sr;
            if (phase > kTwoPi) phase -= kTwoPi;
            out = (noise() * 0.7f + std::sin(phase) * 0.45f) * env * 0.85f;
        } else {  // cymbals + hi-hat: highpassed noise
            const float n = noise();
            lp += 0.55f * (n - lp);
            out = (n - lp) * env;
            if (kind == DK_RIDE) {  // add a faint bell "ping"
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
    static constexpr int kSteps = 16;
    static constexpr int kVoices = 5;  // kick, snare, crash, hihat, ride
    DrumVoice voices[kVoices];
    std::atomic<uint16_t> pattern[kVoices];  // one 16-bit step mask per voice
    std::atomic<int> curStep{-1};

    float sr = 48000;
    long long toStep = 0;
    int step = -1;
    bool wasOn = false;

    void configure(float sampleRate) {
        sr = sampleRate;
        const int kinds[kVoices] = {DK_KICK, DK_SNARE, DK_CRASH, DK_HIHAT, DK_RIDE};
        for (int i = 0; i < kVoices; ++i) {
            voices[i].configure(sr, kinds[i]);
            pattern[i].store(0);
        }
    }
    void blockStart(bool on) {
        if (on && !wasOn) { step = -1; toStep = 0; curStep.store(-1); }
        wasOn = on;
    }
    // One sample of the mixed drum output. Voices keep ringing after stop.
    float process(bool on, float bpm) {
        if (on && --toStep <= 0) {
            toStep = static_cast<long long>(sr * 60.0f / std::max(30.0f, bpm) / 4.0f);  // 16ths
            step = (step + 1) & (kSteps - 1);
            curStep.store(step, std::memory_order_relaxed);
            for (int i = 0; i < kVoices; ++i)
                if (pattern[i].load(std::memory_order_relaxed) & (1u << step)) voices[i].trigger();
        }
        float mix = 0.0f;
        for (int i = 0; i < kVoices; ++i) mix += voices[i].process();
        return mix;
    }
    void setCell(int v, int s, bool on) {
        if (v < 0 || v >= kVoices || s < 0 || s >= kSteps) return;
        uint16_t m = pattern[v].load();
        if (on) m |= (1u << s); else m &= static_cast<uint16_t>(~(1u << s));
        pattern[v].store(m);
    }
    void clear() { for (int i = 0; i < kVoices; ++i) pattern[i].store(0); }
};

}  // namespace webamp
