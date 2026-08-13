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
// Two field reports shaped this into a dual-band detector:
//  1. "Palm mutes detect better than open notes" — rise detection alone favors
//     percussive notes; a ringing string holds the lagging envelope up so the
//     next attack never clears the ratio. Fix: detect attacks in a ~1.8 kHz
//     high-passed band — the ring is fundamental + low harmonics (guitar
//     fundamentals top out ~1.3 kHz) and vanishes above the filter, while the
//     broadband pick transient punches through. (Also immune to mains hum.)
//  2. "Now it picks up way too many notes" — pick scrape, fret buzz, and
//     string noise live in that same high band. Two vetoes (the rhythm-game
//     trick: use your priors): (a) a trigger also requires the FULL-band
//     envelope to rise — a real pluck adds energy to the whole signal, a
//     scrape on top of a ringing note barely moves it; (b) the refractory
//     window stretches to ~45% of the expected subdivision (setMinGap, driven
//     by the trainer's target × tempo) — two real notes can't be closer.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace webamp {

struct OnsetDetector {
    float relCoef = 0;  // fast-envelope release (~15 ms), both bands
    float preK = 0;     // lagging-envelope smoothing (~10 ms), both bands
    float hpA = 0;      // one-pole high-pass coefficient (~1.8 kHz)
    float sr = 48000;
    int refractorySamples = 0;

    float hfFast = 0, hfPre = 0;  // attack band (high-passed): detects
    float fbFast = 0, fbPre = 0;  // full band: vetoes scrape/buzz ghosts
    float hpX = 0, hpY = 0;       // high-pass state
    int refr = 0;

    // sens 0..1 (0.5 default): higher = more sensitive (lower rise ratio).
    float ratio = 2.0f;
    static constexpr float kMinLevel = 0.004f;    // HF transient floor
    static constexpr float kFullSupport = 1.15f;  // full band must rise ~1.2 dB too

    void configure(float sampleRate) {
        sr = sampleRate;
        relCoef = std::exp(-1.0f / (0.015f * sr));
        preK = 1.0f - std::exp(-1.0f / (0.010f * sr));
        const float rc = 1.0f / (2.0f * 3.14159265f * 1800.0f);  // fc ~1.8 kHz
        hpA = rc / (rc + 1.0f / sr);
        refractorySamples = static_cast<int>(0.025f * sr);  // floor; see setMinGap
        reset();
    }
    void setSensitivity(float sens) {
        ratio = 3.0f - 2.0f * std::clamp(sens, 0.0f, 1.0f);  // 3.0 (dull) .. 1.0 (hair-trigger)
    }
    // Expected-rate gate: no two real notes land closer than ~45% of the
    // target subdivision (engine derives it from tempo × pickTarget), so buzz
    // and double-triggers inside that window are structurally impossible.
    void setMinGap(float seconds) {
        refractorySamples = static_cast<int>(std::clamp(seconds, 0.025f, 0.090f) * sr);
    }
    void reset() {
        hfFast = hfPre = fbFast = fbPre = 0;
        hpX = hpY = 0;
        refr = 0;
    }

    // Returns true exactly once per detected attack.
    bool process(float x) {
        // Attack band: the ring is tonal (low), the pick transient is broadband.
        hpY = hpA * (hpY + x - hpX);
        hpX = x;
        const float ah = std::fabs(hpY);
        const float af = std::fabs(x);
        hfFast = ah > hfFast ? ah : hfFast * relCoef;
        hfPre += preK * (hfFast - hfPre);
        fbFast = af > fbFast ? af : fbFast * relCoef;
        fbPre += preK * (fbFast - fbPre);
        if (refr > 0) {
            --refr;
            return false;
        }
        if (hfFast > kMinLevel && hfFast > hfPre * ratio && fbFast > fbPre * kFullSupport) {
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
