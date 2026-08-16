// Picking trainer stats: the small helpers that turn onset timestamps into
// "notes per beat" and evenness.
//
// Onset DETECTION lives in dsp/flux.h (spectral flux, control thread). The
// original level-based OnsetDetector that lived here was removed after four
// field-calibration rounds proved level gating information-starved on a real
// guitar: soft up-picks measure the same amplitude as pick scrape, and real
// polyphonic ring beats through every envelope-rise gate (history in TASKS.md
// P5a/P5c).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace webamp {

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
