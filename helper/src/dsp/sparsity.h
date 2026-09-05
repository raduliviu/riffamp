// Spectral-sparsity onset detector (P5f experiment) — NINOS² after Mounir,
// Karsmakers & van Waterschoot (EURASIP JASMP 2021), which was built for
// exactly the case spectral flux loses: sustained strings and REPEATED notes.
//
// The idea: a pick attack is spectrally DENSE (broadband click smeared over
// many bins) while a ringing string is SPARSE (a few harmonic lines). Flux
// asks "which bins got louder than last frame?" — and a note re-picked under
// its own full-level ring barely moves any bin. Sparsity asks the per-frame
// question "how much broadband stuff is here right now?", so the ring that
// was already present is irrelevant.
//
// Feature per frame: sort the magnitude bins, KEEP only the quietest γ
// fraction (drop the loud harmonic peaks — the opposite of flux's noise
// gate), then
//     NINOS² = ‖x‖₂ · ( ‖x‖₂ / (J^¼ · ‖x‖₄) )
// energy of the quiet bins × normalized inverse sparsity (1 when all J bins
// are equal, →0 when one bin holds everything). Causal, one frame, cheaper
// than flux. Same Hann STFT / hop 256 and the shared OdfPeakPicker, so it
// drops into the same control-thread slot as FluxDetector.
//
// Status: offline harness only (picking_test --det=sparsity) until it beats
// flux on the ringing captures; absFloor/γ are being calibrated there.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "flux.h"  // FftN, OdfPeakPicker

namespace webamp {

template <int kFft_>
struct SparsityDetectorT {
    static constexpr int kFft = kFft_, kHop = 256, kBins = kFft / 2 + 1;
    static constexpr int kUsed = kBins - 1;           // bins 1..N/2 (DC skipped)
    static constexpr float kMagScale = 512.0f / kFft;  // see FluxDetectorT

    float gamma = 0.95f;     // fraction of quietest bins kept (paper: ~0.955)
    float peakFrac = 0.10f;  // relative floor, see OdfPeakPicker

    float sr = 48000;
    FftN<kFft> fft;
    std::array<float, kFft> window{};
    std::array<float, kFft> frame{};
    int hopFill = 0;
    uint64_t samplePos = 0;

    OdfPeakPicker pick;
    std::vector<std::pair<uint64_t, float>>* odfDump = nullptr;

    void configure(float sampleRate) {
        sr = sampleRate;
        fft.init();
        for (int i = 0; i < kFft; ++i)
            window[i] = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * i / (kFft - 1));
        pick.absFloor = 0.0f;  // scale differs from flux; calibrated by the harness
        reset();
    }
    void setSensitivity(float sens) { pick.setSensitivity(sens); }
    void setMinGap(float seconds) { pick.setMinGap(seconds, sr); }
    void flush(std::vector<uint64_t>& out) { pick.flush(out); }
    void reset() {
        frame.fill(0.0f);
        hopFill = 0;
        samplePos = 0;
        pick.reset();
    }

    void push(const float* x, int n, std::vector<uint64_t>& out) {
        for (int i = 0; i < n; ++i) {
            frame[kFft - kHop + hopFill] = x[i];
            ++hopFill;
            ++samplePos;
            if (hopFill < kHop) continue;
            hopFill = 0;
            processFrame(samplePos >= kFft ? samplePos - kFft : 0, out);
            std::copy(frame.begin() + kHop, frame.end(), frame.begin());
        }
    }

    // The raw feature for one frame's magnitudes — exposed so the harness can
    // experiment with combinations.
    float feature(const float* mag) const {
        float q[kUsed];
        for (int k = 0; k < kUsed; ++k) q[k] = mag[k + 1] * kMagScale;
        const int J = std::clamp(static_cast<int>(std::lround(gamma * kUsed)), 8, kUsed);
        std::nth_element(q, q + J, q + kUsed);  // q[0..J) = the J quietest bins
        double s2 = 0.0, s4 = 0.0;
        for (int k = 0; k < J; ++k) {
            const double v = q[k];
            const double v2 = v * v;
            s2 += v2;
            s4 += v2 * v2;
        }
        if (s4 <= 0.0) return 0.0f;
        const double e2 = std::sqrt(s2);
        const double e4 = std::pow(s4, 0.25);
        const double nis = e2 / (e4 * std::pow(static_cast<double>(J), 0.25));
        return static_cast<float>(e2 * nis);
    }

private:
    void processFrame(uint64_t frameStart, std::vector<uint64_t>& out) {
        float buf[kFft], mag[kBins];
        for (int i = 0; i < kFft; ++i) buf[i] = frame[i] * window[i];
        fft.magnitudes(buf, mag);
        const float odf = feature(mag);
        if (odfDump) odfDump->emplace_back(frameStart, odf);
        pick.peakFrac = peakFrac;
        pick.push(odf, frameStart, out);
    }
};

using SparsityDetector = SparsityDetectorT<512>;

}  // namespace webamp
