// Gate and tone-stack stages for the webamp helper chain:
//   input gain -> gate -> NAM -> tone stack -> cab IR -> output gain
#pragma once

#include <algorithm>
#include <cmath>

namespace webamp {

// RBJ-cookbook biquad, transposed direct form II.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    float process(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }

    void lowShelf(float sr, float f0, float dB) {
        const float A = std::pow(10.0f, dB / 40.0f);
        const float w = 2.0f * 3.14159265f * f0 / sr;
        const float cw = std::cos(w), sw = std::sin(w);
        const float alpha = sw / 2.0f * std::sqrt(2.0f);
        const float sqA = 2.0f * std::sqrt(A) * alpha;
        const float a0 = (A + 1) + (A - 1) * cw + sqA;
        b0 = A * ((A + 1) - (A - 1) * cw + sqA) / a0;
        b1 = 2 * A * ((A - 1) - (A + 1) * cw) / a0;
        b2 = A * ((A + 1) - (A - 1) * cw - sqA) / a0;
        a1 = -2 * ((A - 1) + (A + 1) * cw) / a0;
        a2 = ((A + 1) + (A - 1) * cw - sqA) / a0;
    }
    void highShelf(float sr, float f0, float dB) {
        const float A = std::pow(10.0f, dB / 40.0f);
        const float w = 2.0f * 3.14159265f * f0 / sr;
        const float cw = std::cos(w), sw = std::sin(w);
        const float alpha = sw / 2.0f * std::sqrt(2.0f);
        const float sqA = 2.0f * std::sqrt(A) * alpha;
        const float a0 = (A + 1) - (A - 1) * cw + sqA;
        b0 = A * ((A + 1) + (A - 1) * cw + sqA) / a0;
        b1 = -2 * A * ((A - 1) + (A + 1) * cw) / a0;
        b2 = A * ((A + 1) + (A - 1) * cw - sqA) / a0;
        a1 = 2 * ((A - 1) - (A + 1) * cw) / a0;
        a2 = ((A + 1) - (A - 1) * cw - sqA) / a0;
    }
    void peaking(float sr, float f0, float dB, float Q = 0.9f) {
        const float A = std::pow(10.0f, dB / 40.0f);
        const float w = 2.0f * 3.14159265f * f0 / sr;
        const float cw = std::cos(w), sw = std::sin(w);
        const float alpha = sw / (2.0f * Q);
        const float a0 = 1 + alpha / A;
        b0 = (1 + alpha * A) / a0;
        b1 = -2 * cw / a0;
        b2 = (1 - alpha * A) / a0;
        a1 = -2 * cw / a0;
        a2 = (1 - alpha / A) / a0;
    }
};

// Bass/mid/treble in dB (-12..+12), classic post-amp voicing points.
struct ToneStack {
    Biquad bass, mid, treble;
    void configure(float sr, float bassDb, float midDb, float trebleDb) {
        bass.lowShelf(sr, 120.0f, bassDb);
        mid.peaking(sr, 800.0f, midDb);
        treble.highShelf(sr, 3000.0f, trebleDb);
    }
    float process(float x) { return treble.process(mid.process(bass.process(x))); }
    void reset() { bass.reset(); mid.reset(); treble.reset(); }
};

// Downward noise gate: fast-attack/slow-release envelope follower driving a
// smoothed gain. thresholdDb <= -90 disables it.
struct NoiseGate {
    float env = 0, gain = 1;
    float attCoef = 0, relCoef = 0, gainCoef = 0;
    void configure(float sr) {
        attCoef = std::exp(-1.0f / (0.001f * sr));   // 1 ms envelope attack
        relCoef = std::exp(-1.0f / (0.050f * sr));   // 50 ms envelope release
        gainCoef = std::exp(-1.0f / (0.005f * sr));  // 5 ms gain smoothing
    }
    float process(float x, float thresholdLin) {
        const float ax = std::fabs(x);
        env = ax + (ax > env ? attCoef : relCoef) * (env - ax);
        const float target = (thresholdLin <= 0.0f || env > thresholdLin) ? 1.0f : 0.0f;
        gain = target + gainCoef * (gain - target);
        return x * gain;
    }
    void reset() { env = 0; gain = 1; }
};

// Sample-accurate metronome click: decaying sine, accented beat 1.
// Runs entirely on the audio thread; caller feeds params each block.
struct Metronome {
    float sr = 48000;
    double phase = 0, phaseInc = 0;
    float env = 0, decay = 0;
    long long samplesToNext = 0;
    int beat = -1;
    bool wasOn = false;

    void configure(float sampleRate) {
        sr = sampleRate;
        decay = std::exp(-1.0f / (0.025f * sr));  // ~25 ms click tail
    }
    // Call once per block before the sample loop.
    void blockStart(bool on) {
        if (on && !wasOn) { beat = -1; samplesToNext = 0; env = 0; }  // restart on accent
        wasOn = on;
    }
    float process(bool on, float bpm, int beatsPerBar) {
        if (!on) { env = 0; return 0.0f; }
        if (--samplesToNext <= 0) {
            samplesToNext = static_cast<long long>(sr * 60.0f / std::max(30.0f, bpm));
            beat = (beat + 1) % std::max(1, beatsPerBar);
            const float freq = (beat == 0) ? 1600.0f : 1100.0f;
            phaseInc = 2.0 * 3.14159265358979 * freq / sr;
            phase = 0;
            env = (beat == 0) ? 1.0f : 0.7f;
        }
        if (env < 1e-4f) return 0.0f;
        const float s = static_cast<float>(std::sin(phase)) * env;
        phase += phaseInc;
        env *= decay;
        return s;
    }
};

}  // namespace webamp
