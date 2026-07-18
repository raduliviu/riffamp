// YIN pitch detection (de Cheveigné & Kawahara 2002) for the webamp tuner.
// Runs on the control thread over a snapshot of recent input samples —
// never on the audio callback.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace webamp {

// Returns detected fundamental in Hz, or -1 if no confident pitch.
// Guitar-tuned defaults: ~45 Hz (drop A) .. 1500 Hz.
inline float detectPitch(const float* x, int n, float sr) {
    const int tauMax = std::min(n / 2, static_cast<int>(sr / 45.0f));
    const int tauMin = std::max(2, static_cast<int>(sr / 1500.0f));
    const int W = n - tauMax;  // integration window
    if (W < tauMax) return -1.0f;

    double energy = 0;
    for (int i = 0; i < n; ++i) energy += static_cast<double>(x[i]) * x[i];
    if (energy / n < 1e-7) return -1.0f;  // silence

    thread_local std::vector<float> d, cm;
    d.assign(tauMax + 1, 0.0f);
    cm.assign(tauMax + 1, 1.0f);

    for (int tau = 1; tau <= tauMax; ++tau) {
        double sum = 0;
        for (int i = 0; i < W; ++i) {
            const float diff = x[i] - x[i + tau];
            sum += static_cast<double>(diff) * diff;
        }
        d[tau] = static_cast<float>(sum);
    }
    double running = 0;
    for (int tau = 1; tau <= tauMax; ++tau) {
        running += d[tau];
        cm[tau] = running > 0 ? static_cast<float>(d[tau] * tau / running) : 1.0f;
    }

    constexpr float kThreshold = 0.12f;
    int tau = -1;
    for (int t = tauMin; t <= tauMax; ++t) {
        if (cm[t] < kThreshold) {
            while (t + 1 <= tauMax && cm[t + 1] < cm[t]) ++t;  // walk to the local minimum
            tau = t;
            break;
        }
    }
    if (tau <= tauMin || tau >= tauMax) return -1.0f;

    // Parabolic interpolation around the minimum for sub-sample precision.
    const float a = cm[tau - 1], b = cm[tau], c = cm[tau + 1];
    const float denom = a - 2 * b + c;
    const float shift = denom != 0 ? 0.5f * (a - c) / denom : 0.0f;
    return sr / (tau + shift);
}

struct NoteInfo {
    std::string name;  // e.g. "E2"
    float cents;       // -50..+50 from the nearest note
    float freq;
};

inline NoteInfo describeNote(float freq) {
    static const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B"};
    const float midi = 69.0f + 12.0f * std::log2(freq / 440.0f);
    const int nearest = static_cast<int>(std::lround(midi));
    NoteInfo info;
    info.cents = (midi - nearest) * 100.0f;
    info.freq = freq;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%s%d", kNames[((nearest % 12) + 12) % 12], nearest / 12 - 1);
    info.name = buf;
    return info;
}

}  // namespace webamp
