// Spectral-flux onset detector (P5c) — replaces the level-based OnsetDetector
// after four field rounds proved level gating information-starved on a real
// guitar: soft up-picks measure the same amplitude as pick scrape (~20% of a
// down-pick), and real polyphonic ring BEATS, so every envelope-rise gate
// either misses notes or fires on the wobble. A pick attack is the one event
// that adds NEW energy across many frequency bands at once; ring beating and
// scrape don't. Flux measures exactly that.
//
// Method (validated offline against labeled captures of the user's guitar —
// 26/32 grid hits vs the level detector's 17/32 on the same take):
//   512-pt Hann STFT, hop 256 (5.3 ms) · log-compressed magnitudes ·
//   SuperFlux-style positive difference against a 3-bin max-filtered previous
//   frame (vibrato/beating robust) · adaptive threshold mean + k·std over the
//   trailing ~0.7 s · local-max peak picking (±3 frames) · min-gap gate driven
//   by the trainer's expected note rate.
//
// Runs on the CONTROL thread (fed from the raw-input ring, ~8 FFTs per 40 ms
// tick — microscopic). Onsets carry sample timestamps; emission lags ~16 ms
// (3-frame lookahead) which the trainer never notices since timestamps are
// exact. NOT audio-thread code: allocation-free after configure(), but O(N)
// per hop.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace webamp {

// Fixed 512-point real-input FFT (iterative radix-2), magnitudes for bins
// 0..256. Self-contained so offline tests need no FFT library.
struct Fft512 {
    static constexpr int N = 512;
    std::array<float, N / 2> tw_re{}, tw_im{};
    std::array<uint16_t, N> rev{};

    void init() {
        for (int k = 0; k < N / 2; ++k) {
            tw_re[k] = std::cos(-2.0f * 3.14159265f * k / N);
            tw_im[k] = std::sin(-2.0f * 3.14159265f * k / N);
        }
        for (int i = 0; i < N; ++i) {
            uint16_t r = 0;
            for (int b = 0; b < 9; ++b) r |= ((i >> b) & 1) << (8 - b);
            rev[i] = r;
        }
    }

    // in: N windowed samples; mag: N/2+1 magnitudes.
    void magnitudes(const float* in, float* mag) const {
        float re[N], im[N];
        for (int i = 0; i < N; ++i) {
            re[i] = in[rev[i]];
            im[i] = 0.0f;
        }
        for (int len = 2; len <= N; len <<= 1) {
            const int step = N / len;
            for (int start = 0; start < N; start += len) {
                for (int k = 0; k < len / 2; ++k) {
                    const float wr = tw_re[k * step], wi = tw_im[k * step];
                    const int a = start + k, b = start + k + len / 2;
                    const float xr = re[b] * wr - im[b] * wi;
                    const float xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr;
                    im[b] = im[a] - xi;
                    re[a] += xr;
                    im[a] += xi;
                }
            }
        }
        for (int k = 0; k <= N / 2; ++k) mag[k] = std::sqrt(re[k] * re[k] + im[k] * im[k]);
    }
};

struct FluxDetector {
    static constexpr int kFft = 512, kHop = 256, kBins = kFft / 2 + 1;
    static constexpr int kLookahead = 3;              // frames each side for peak pick
    static constexpr int kThreshWindow = 130;         // ~0.7 s of frames for the threshold
    // Tunable (defaults chosen against the synthetic suite AND labeled real
    // captures; the picking_test wav mode can sweep them):
    // peakFrac — floor vs the loudest recent onset. Deliberately gentle: a
    // first note from silence has outsized flux (whole-spectrum novelty) and
    // would otherwise mask normal notes for the length of the stats window.
    // binFloor — per-bin novelty floor; ignores leakage ripple and the
    // low-level spectral churn of real sustained strings.
    float peakFrac = 0.10f;
    float binFloor = 0.1f;
    static constexpr float kAbsFlux = 0.5f;           // numeric-jitter floor (real onsets ~10-40)

    float sr = 48000;
    Fft512 fft;
    std::array<float, kFft> window{};
    std::array<float, kFft> frame{};                  // shift register of latest samples
    int hopFill = 0;
    uint64_t samplePos = 0;                           // absolute time of next sample

    std::array<float, kBins> prevLog{};
    bool havePrev = false;

    // Recent flux values + their frame start times (ring, newest last).
    std::array<float, kThreshWindow> hist{};
    std::array<uint64_t, kThreshWindow> histT{};
    int histN = 0;
    uint64_t lastOnset = 0;
    bool anyOnset = false;

    float deltaK = 2.5f;                              // sens 0..1 -> 3.5 (dull) .. 1.5 (hair)
    uint64_t minGapSamples = 1200;                    // 25 ms @48k floor; see setMinGap

    void configure(float sampleRate) {
        sr = sampleRate;
        fft.init();
        for (int i = 0; i < kFft; ++i)
            window[i] = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * i / (kFft - 1));
        reset();
    }
    void setSensitivity(float sens) {
        deltaK = 3.5f - 2.0f * std::clamp(sens, 0.0f, 1.0f);
    }
    void setMinGap(float seconds) {
        minGapSamples =
            static_cast<uint64_t>(std::clamp(seconds, 0.025f, 0.150f) * sr);
    }
    void reset() {
        frame.fill(0.0f);
        hopFill = 0;
        samplePos = 0;
        havePrev = false;
        histN = 0;
        anyOnset = false;
    }

    // Feed any number of samples; detected onsets (absolute sample times of
    // their frame starts) are appended to `out`.
    void push(const float* x, int n, std::vector<uint64_t>& out) {
        for (int i = 0; i < n; ++i) {
            // Shift register advances one hop at a time.
            frame[kFft - kHop + hopFill] = x[i];
            ++hopFill;
            ++samplePos;
            if (hopFill < kHop) continue;
            hopFill = 0;
            // Frame covers [pos-kFft, pos); clamp the ramp-in frames to 0.
            processFrame(samplePos >= kFft ? samplePos - kFft : 0, out);
            std::copy(frame.begin() + kHop, frame.end(), frame.begin());
        }
    }

private:
    void processFrame(uint64_t frameStart, std::vector<uint64_t>& out) {
        float buf[kFft], mag[kBins], logMag[kBins];
        for (int i = 0; i < kFft; ++i) buf[i] = frame[i] * window[i];
        fft.magnitudes(buf, mag);
        for (int k = 0; k < kBins; ++k) logMag[k] = std::log1p(50.0f * mag[k]);

        float flux = 0.0f;
        if (havePrev) {
            for (int k = 0; k < kBins; ++k) {
                float p = prevLog[k];  // 3-bin max filter (SuperFlux): beats/vibrato robust
                if (k > 0) p = std::max(p, prevLog[k - 1]);
                if (k + 1 < kBins) p = std::max(p, prevLog[k + 1]);
                const float d = logMag[k] - p;
                if (d > binFloor) flux += d;
            }
        }
        std::copy(logMag, logMag + kBins, prevLog.begin());
        havePrev = true;

        // Slide the history; the peak-pick candidate is kLookahead frames back.
        if (histN == kThreshWindow) {
            std::copy(hist.begin() + 1, hist.end(), hist.begin());
            std::copy(histT.begin() + 1, histT.end(), histT.begin());
            --histN;
        }
        hist[histN] = flux;
        histT[histN] = frameStart + kFft;  // onset time ~ frame end (attack enters here)
        ++histN;

        const int c = histN - 1 - kLookahead;
        if (c < kLookahead) return;  // not enough context yet

        // Local max over ±kLookahead frames.
        for (int j = c - kLookahead; j <= c + kLookahead; ++j)
            if (hist[j] > hist[c]) return;

        // Robust adaptive threshold. Mean+std self-poisons on regular playing
        // (the note peaks inflate their own threshold), so use median + MAD —
        // sparse peaks cannot pollute either — plus a scale-free relative
        // floor: a real onset is never tiny next to the loudest recent one.
        float w[kThreshWindow];
        std::copy(hist.begin(), hist.begin() + c + 1, w);
        std::nth_element(w, w + c / 2, w + c + 1);
        const float med = w[c / 2];
        float dev[kThreshWindow];
        float peak = 0.0f;
        for (int j = 0; j <= c; ++j) {
            dev[j] = std::fabs(hist[j] - med);
            peak = std::max(peak, hist[j]);
        }
        std::nth_element(dev, dev + c / 2, dev + c + 1);
        const float mad = dev[c / 2];
        const float thr = std::max(
            {med + deltaK * 1.4826f * mad, peakFrac * peak, kAbsFlux});
        if (hist[c] < thr) return;

        const uint64_t t = histT[c] >= kFft ? histT[c] - kFft : 0;  // frame start
        if (anyOnset && t - lastOnset < minGapSamples) return;
        lastOnset = t;
        anyOnset = true;
        out.push_back(t);
    }
};

}  // namespace webamp
