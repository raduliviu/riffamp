// Picking trainer DSP (P5a): onset detection on the clean pre-amp input, plus
// the small stats helpers the control thread uses to turn onset timestamps
// into "notes per beat" and evenness.
//
// Detector: rise detection — an instant-attack peak follower (fast) against a
// lagging copy of itself (pre, ~10 ms smoothing). A pick attack makes `fast`
// jump while `pre` is still low; during sustain/decay `pre` sits at or above
// `fast`, so ringing never retriggers regardless of absolute level (accents
// followed by soft notes both count). A short refractory window guards the
// attack transient itself. Runs on the audio thread: O(1)/sample, no alloc.
//
// The detector listens through a ~1.8 kHz one-pole high-pass (field report:
// palm mutes detected better than open notes). Rise detection alone favors
// short percussive notes — a ringing string holds the lagging envelope up, so
// the next attack never clears the rise ratio. But the pick transient is
// broadband while the ring is fundamental + low harmonics (guitar fundamentals
// top out ~1.3 kHz), so above the filter the ring largely vanishes and every
// attack still punches through — open notes now rise from a low floor exactly
// like palm mutes. Bonus: mains hum can't touch kMinLevel anymore.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace webamp {

struct OnsetDetector {
    float relCoef = 0;  // fast-envelope release (~15 ms)
    float preK = 0;     // lagging-envelope smoothing (~10 ms)
    float hpA = 0;      // one-pole high-pass coefficient (~1.8 kHz)
    int refractorySamples = 0;

    float fast = 0, pre = 0;
    float hpX = 0, hpY = 0;  // high-pass state
    int refr = 0;

    // sens 0..1 (0.5 default): higher = more sensitive (lower rise ratio).
    float ratio = 2.0f;
    static constexpr float kMinLevel = 0.003f;  // ~-50 dBFS noise-floor guard

    void configure(float sr) {
        relCoef = std::exp(-1.0f / (0.015f * sr));
        preK = 1.0f - std::exp(-1.0f / (0.010f * sr));
        const float rc = 1.0f / (2.0f * 3.14159265f * 1800.0f);  // fc ~1.8 kHz
        hpA = rc / (rc + 1.0f / sr);
        refractorySamples = static_cast<int>(0.025f * sr);  // 25 ms
        reset();
    }
    void setSensitivity(float sens) {
        ratio = 3.0f - 2.0f * std::clamp(sens, 0.0f, 1.0f);  // 3.0 (dull) .. 1.0 (hair-trigger)
    }
    void reset() {
        fast = pre = 0;
        hpX = hpY = 0;
        refr = 0;
    }

    // Returns true exactly once per detected attack.
    bool process(float x) {
        // High-pass first: the ring is tonal (low), the attack is broadband.
        hpY = hpA * (hpY + x - hpX);
        hpX = x;
        const float a = std::fabs(hpY);
        fast = a > fast ? a : fast * relCoef;
        pre += preK * (fast - pre);
        if (refr > 0) {
            --refr;
            return false;
        }
        if (fast > kMinLevel && fast > pre * ratio) {
            refr = refractorySamples;
            return true;
        }
        return false;
    }
};

// --- Stats over onset timestamps (control thread) ----------------------------

// Median inter-onset interval in samples; 0 if fewer than 3 onsets.
inline double medianIoi(const std::vector<uint64_t>& ts) {
    if (ts.size() < 3) return 0.0;
    std::vector<double> iois;
    iois.reserve(ts.size() - 1);
    for (size_t i = 1; i < ts.size(); ++i)
        iois.push_back(static_cast<double>(ts[i] - ts[i - 1]));
    std::sort(iois.begin(), iois.end());
    const size_t n = iois.size();
    return n % 2 ? iois[n / 2] : 0.5 * (iois[n / 2 - 1] + iois[n / 2]);
}

// Coefficient of variation of the IOIs (0 = perfectly even); -1 if too few.
inline double ioiCv(const std::vector<uint64_t>& ts) {
    if (ts.size() < 4) return -1.0;
    std::vector<double> iois;
    iois.reserve(ts.size() - 1);
    for (size_t i = 1; i < ts.size(); ++i)
        iois.push_back(static_cast<double>(ts[i] - ts[i - 1]));
    double mean = 0;
    for (double v : iois) mean += v;
    mean /= iois.size();
    if (mean <= 0) return -1.0;
    double var = 0;
    for (double v : iois) var += (v - mean) * (v - mean);
    var /= iois.size();
    return std::sqrt(var) / mean;
}

// Implied subdivision: how many notes per beat the median IOI corresponds to.
inline double notesPerBeat(double beatSamples, double medianIoiSamples) {
    if (medianIoiSamples <= 0 || beatSamples <= 0) return 0.0;
    return beatSamples / medianIoiSamples;
}

}  // namespace webamp
