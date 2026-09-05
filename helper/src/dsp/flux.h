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
//   Hann STFT (window kFft, hop 256 = 5.3 ms) · log-compressed magnitudes ·
//   SuperFlux-style positive difference against a 3-bin max-filtered previous
//   frame (vibrato/beating robust) · adaptive threshold median + k·MAD over
//   the trailing ~0.7 s · local-max peak picking (±3 frames) · min-gap gate
//   driven by the trainer's expected note rate.
//
// P5f findings (real captures + exactly-labeled resynthesis from real plucks;
// see picking_test wav mode). What shipped: level normalizer, raw-level
// silence gate, magFloor 0.4→0.2, peakFrac 0.10→0.05, post-onset mask,
// stronger-wins (ratio 2) refractory. What was tried and
// rejected, so nobody retries it blind:
//  · Window size. 512 (10.7 ms) holds barely ONE period of a low-string note
//    (~80-110 Hz), so the magnitude spectrum oscillates at the hop rate for as
//    long as the note rings (flux alternating X,0,Y,0 for hundreds of ms).
//    But 2048 averages a re-pick's transient into the ring and loses
//    repeated notes wholesale (a real run went 127→6 onsets) — the short
//    window's time concentration is what makes re-picks detectable at all.
//    A 2-frame temporal max cancels the oscillation perfectly and ALSO
//    cancels real low-note re-picks (real run 121→108). Both were removed
//    again; the numbers live here so nobody re-derives them.
//  · Level dependence. A capture 23 dB quieter than the calibration takes
//    lost everything above 500 Hz to the fixed magFloor and sat in the
//    linear region of the log compression: absolute floors made the detector
//    depend on the interface's gain knob. Fixed by the normalizer.
//  · Band weighting. The pick attack's 2-4 kHz content is 30-100x its level
//    during the ring 60 ms later, while 0-500 Hz has ~1x contrast (the ring
//    is often LOUDER than the attack there). magFloor 0.4 sat above the
//    whole high band; 0.2 admits it. Restricting flux to the high band alone
//    (≥1.5 kHz) or an adaptive per-bin noise-floor gate both lost notes
//    (removed again).
//  · Spectral sparsity (NINOS², dsp/sparsity.h) found more re-picks on the
//    ringing take but fired on finger noise and peaked ~40 ms late on low
//    notes; not a replacement. Available via picking_test --det=sparsity.
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
#include <utility>
#include <vector>

namespace webamp {

constexpr int log2c(int n) {
    int l = 0;
    while (n > 1) {
        n >>= 1;
        ++l;
    }
    return l;
}

// Fixed-size real-input FFT (iterative radix-2), magnitudes for bins 0..N/2.
// Self-contained so offline tests need no FFT library.
template <int N_>
struct FftN {
    static constexpr int N = N_;
    static_assert(N >= 16 && (N & (N - 1)) == 0 && N <= 65536, "power of two");
    static constexpr int kLog2 = log2c(N);
    std::array<float, N / 2> tw_re{}, tw_im{};
    std::array<uint16_t, N> rev{};

    void init() {
        for (int k = 0; k < N / 2; ++k) {
            tw_re[k] = std::cos(-2.0f * 3.14159265f * k / N);
            tw_im[k] = std::sin(-2.0f * 3.14159265f * k / N);
        }
        for (int i = 0; i < N; ++i) {
            uint16_t r = 0;
            for (int b = 0; b < kLog2; ++b) r |= ((i >> b) & 1) << (kLog2 - 1 - b);
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
using Fft512 = FftN<512>;

// Peak picker over any onset detection function (ODF), one value per frame:
// local max over ±kLookahead frames · robust adaptive threshold (median +
// k·MAD over the trailing window — mean/std self-poisons on regular playing
// because the note peaks inflate their own threshold; sparse peaks can't
// pollute a median) · scale-free relative floor (a real onset is never tiny
// next to the loudest recent one) · absolute floor for numeric jitter ·
// min-gap refractory. Shared by the flux and sparsity detectors.
struct OdfPeakPicker {
    static constexpr int kLookahead = 3;              // frames each side for peak pick
    static constexpr int kWindow = 130;               // ~0.7 s of frames for the threshold

    // peakFrac — floor vs the loudest recent onset. Deliberately gentle: a
    // first note from silence has outsized novelty (whole-spectrum) and would
    // otherwise mask normal notes for the length of the stats window.
    float peakFrac = 0.10f;
    float absFloor = 0.5f;                            // feature-specific; owner sets it
    float deltaK = 2.0f;                              // sens 0..1 -> 3.0 (dull) .. 1.0 (hair-trigger)
    uint64_t minGapSamples = 1200;                    // 25 ms @48k floor; see setMinGap

    // Refractory rule (P5f, validated on three metronome-locked runs). A peak
    // is HELD for minGap; a later peak inside that window replaces it only if
    // it is >= swRatio x stronger, otherwise it is dropped. Why: real close
    // pairs come in two kinds — a weak precursor (pick touching the string,
    // flux ~2) 60-110 ms BEFORE the attack (flux 10-60, ratio 5-35x), and a
    // ring artefact AFTER it (ratio 0.06-1.3x). Plain first-wins locked onto
    // the precursor and the gate then swallowed the real attack (timestamps
    // 60-100 ms early; on an 80 bpm run ±35 ms hits 40/65 → 50/65 with this
    // rule, ghosts 28 → 12). Ratio 1 ("any stronger peak wins") was rejected
    // earlier: artefacts up to 1.3x the attack replaced it and cascaded into
    // the next note. Emission lags by minGap — the trainer uses timestamps.
    bool strongerWins = true;
    float swRatio = 2.0f;
    // Post-onset mask (P5f): for a short time after an emitted onset the
    // threshold is held at postMaskFrac x that onset's ODF value, decaying
    // with time constant postMaskTau. Ring/parity artefacts 50-110 ms after
    // an attack run at 5-45% of it and are rejected; a real re-pick of
    // comparable strength passes, so the free-play gate can stay loose
    // (0.6x, off-target subdivisions still register) without the doubles a
    // bare relative floor lets through. 0 disables.
    float postMaskFrac = 0.5f;
    float postMaskTau = 0.060f;  // seconds
    float sr = 48000.0f;
    float lastV = 0.0f;          // ODF value of the last emitted onset
    // The mask anchors on min(lastV, 3 x median of recent onsets): a first
    // note from silence carries ~10x a normal re-pick's flux (whole-spectrum
    // novelty) and would otherwise mask the next note at fast tempos.
    static constexpr int kRecent = 8;
    std::array<float, kRecent> recentV{};
    int recentN = 0, recentPos = 0;

    std::array<float, kWindow> hist{};                // recent ODF values (ring, newest last)
    std::array<uint64_t, kWindow> histT{};            // their frame start times
    int histN = 0;
    uint64_t lastOnset = 0;
    bool anyOnset = false;
    uint64_t pendT = 0;                               // held-back candidate (strongerWins)
    float pendV = 0.0f;
    bool havePend = false;

    void reset() {
        histN = 0;
        anyOnset = false;
        havePend = false;
        recentN = 0;
        recentPos = 0;
    }
    void noteEmitted(float v) {
        lastV = v;
        recentV[recentPos] = v;
        recentPos = (recentPos + 1) % kRecent;
        recentN = std::min(recentN + 1, kRecent);
    }
    float maskAnchor() const {
        if (recentN < 2) return 0.0f;  // one note so far (typically from silence, ~10x): no mask yet
        float w[kRecent];
        std::copy(recentV.begin(), recentV.begin() + recentN, w);
        std::nth_element(w, w + recentN / 2, w + recentN);
        return std::min(lastV, 3.0f * w[recentN / 2]);
    }
    // End of stream (offline use): emit a held-back candidate.
    void flush(std::vector<uint64_t>& out) {
        if (!havePend) return;
        havePend = false;
        lastOnset = pendT;
        noteEmitted(pendV);
        anyOnset = true;
        out.push_back(pendT);
    }
    void setSensitivity(float sens) {
        deltaK = 3.0f - 2.0f * std::clamp(sens, 0.0f, 1.0f);
    }
    void setMinGap(float seconds, float sampleRate) {
        sr = sampleRate;
        minGapSamples =
            static_cast<uint64_t>(std::clamp(seconds, 0.025f, 0.150f) * sr);
    }

    // Feed one frame's ODF value with its frame start time; a picked onset
    // (kLookahead frames back) is appended to `out` as that frame's start.
    void push(float v, uint64_t frameStart, std::vector<uint64_t>& out) {
        if (histN == kWindow) {
            std::copy(hist.begin() + 1, hist.end(), hist.begin());
            std::copy(histT.begin() + 1, histT.end(), histT.begin());
            --histN;
        }
        hist[histN] = v;
        histT[histN] = frameStart;
        ++histN;

        // A held-back candidate is emitted once minGap has passed without a
        // stronger rival (checked on every frame, not only on candidates).
        if (havePend && frameStart - pendT >= minGapSamples) {
            havePend = false;
            lastOnset = pendT;
            noteEmitted(pendV);
            anyOnset = true;
            out.push_back(pendT);
        }

        const int c = histN - 1 - kLookahead;
        if (c < kLookahead) return;  // not enough context yet

        for (int j = c - kLookahead; j <= c + kLookahead; ++j)
            if (hist[j] > hist[c]) return;

        float w[kWindow];
        std::copy(hist.begin(), hist.begin() + c + 1, w);
        std::nth_element(w, w + c / 2, w + c + 1);
        const float med = w[c / 2];
        float dev[kWindow];
        float peak = 0.0f;
        for (int j = 0; j <= c; ++j) {
            dev[j] = std::fabs(hist[j] - med);
            peak = std::max(peak, hist[j]);
        }
        std::nth_element(dev, dev + c / 2, dev + c + 1);
        const float mad = dev[c / 2];
        float thr = std::max({med + deltaK * 1.4826f * mad, peakFrac * peak, absFloor});
        const uint64_t t = histT[c];
        if (postMaskFrac > 0.0f && anyOnset && t > lastOnset) {
            const float dt = static_cast<float>(t - lastOnset) / sr;
            thr = std::max(thr, postMaskFrac * maskAnchor() * std::exp(-dt / postMaskTau));
        }
        if (hist[c] < thr) return;

        if (!strongerWins) {
            if (anyOnset && t - lastOnset < minGapSamples) return;
            lastOnset = t;
            noteEmitted(hist[c]);
            anyOnset = true;
            out.push_back(t);
            return;
        }
        if (anyOnset && t - lastOnset < minGapSamples) return;  // too close to an emitted one
        if (havePend) {
            // Within minGap of the held candidate (else it would have been
            // emitted above): the stronger peak survives.
            if (hist[c] > swRatio * pendV) {
                pendT = t;
                pendV = hist[c];
            }
            return;
        }
        pendT = t;
        pendV = hist[c];
        havePend = true;
    }
};

template <int kFft_>
struct FluxDetectorT {
    static constexpr int kFft = kFft_, kHop = 256, kBins = kFft / 2 + 1;
    static_assert(kFft >= kHop, "window must cover a hop");
    // Magnitudes are put on the 512-window scale (a Hann window's gain grows
    // with its length) so magFloor/absFloor calibrations carry across sizes.
    static constexpr float kMagScale = 512.0f / kFft;

    // Tunable. Defaults (P5f) were chosen against exactly-labeled test signals
    // built from REAL plucks cut out of the user's captures (see
    // picking_test wav mode + the scratch make_synth harness) and validated
    // on a real 8-bar pick run: ringing 16ths 93→100%, soft alternate
    // up-picks 51→100%, a 22 dB quieter take 35→99%, palm-muted 98→97%,
    // ghosts ≤3/128 throughout; real run 119→121/146 hits.
    // peakFrac — relative floor vs the loudest recent onset. 0.10 masked
    // soft notes for 0.7 s after every loud one (a first note from silence
    // has whole-spectrum novelty, ~10x a normal re-pick); 0.03 let weak
    // pre-attack scrape through as onsets. 0.05 with the stronger-wins
    // refractory scored best on the real runs.
    float peakFrac = 0.05f;
    // binFloor — per-bin novelty floor; ignores leakage ripple and the
    // low-level spectral churn of real sustained strings.
    float binFloor = 0.6f;
    // magFloor — spectral noise gate on the NORMALIZED magnitudes (a
    // full-scale sine's peak bin is ~217). Bins quieter than this can't
    // contribute novelty: the log compression otherwise turns noise-floor
    // wiggle and finger noise into flux. 0.4 also swallowed the pick
    // attack's 2-4 kHz content (~0.4-0.5, 30-100x its level during the
    // ring); 0.2 keeps it.
    float magFloor = 0.2f;

    // Level normalizer (P5f): the STFT sees the input scaled so that its
    // recent peak sits at normTarget. Attack ~30 ms, release ~2 s; the gain
    // is capped so silence/noise is not lifted into the analysis range.
    // Without it every floor below is relative to the interface's gain knob
    // (a take 22 dB quieter than the calibration ones lost 65% of its notes).
    bool normalize = true;
    float normTarget = 0.5f;   // -6 dBFS
    float normMaxGain = 20.0f; // never lift more than +26 dB
    float normAtkS = 0.030f;   // level tracker attack; release is 2 s
    // Silence gate on the RAW frame (before normalization): a frame quieter
    // than this is not evidence of anything, whatever the normalizer would
    // have lifted it to. Measured guitar-off frames: median -78 dBFS, 1% of
    // them above -60 (handling noise); the quietest real take peaks at
    // -28 dBFS and a -45 dBFS palm-muted line still reads 96% at -60.
    float silenceDb = -60.0f;

    float sr = 48000;
    FftN<kFft> fft;
    std::array<float, kFft> window{};
    std::array<float, kFft> frame{};                  // shift register of latest samples
    int hopFill = 0;
    uint64_t samplePos = 0;                           // absolute time of next sample

    std::array<float, kBins> prevLog{};
    bool havePrev = false;
    float env = 0.0f;                                 // normalizer level tracker
    float envAtk = 0.0f, envRel = 0.0f;               // per-sample one-pole coefficients

    OdfPeakPicker pick;                               // absFloor 0.5: real onsets ~10-40
    // Offline tooling: when set, every frame's (frameStart, flux) is appended.
    std::vector<std::pair<uint64_t, float>>* odfDump = nullptr;

    void configure(float sampleRate) {
        sr = sampleRate;
        fft.init();
        for (int i = 0; i < kFft; ++i)
            window[i] = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * i / (kFft - 1));
        envAtk = 1.0f - std::exp(-1.0f / (normAtkS * sr));
        envRel = 1.0f - std::exp(-1.0f / (2.0f * sr));
        pick.sr = sr;
        reset();
    }
    void setSensitivity(float sens) { pick.setSensitivity(sens); }
    void setMinGap(float seconds) { pick.setMinGap(seconds, sr); }
    void flush(std::vector<uint64_t>& out) { pick.flush(out); }  // offline end-of-stream
    void reset() {
        frame.fill(0.0f);
        hopFill = 0;
        samplePos = 0;
        havePrev = false;
        env = 0.0f;
        pick.reset();
    }

    // Feed any number of samples; detected onsets (absolute sample times of
    // their frame starts) are appended to `out`.
    void push(const float* x, int n, std::vector<uint64_t>& out) {
        for (int i = 0; i < n; ++i) {
            const float a = std::fabs(x[i]);
            env += (a - env) * (a > env ? envAtk : envRel);
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
        float gain = kMagScale;
        if (normalize) gain *= std::min(normMaxGain, normTarget / std::max(env, 1e-6f));
        double e = 0.0;
        for (int i = 0; i < kFft; ++i) {
            buf[i] = frame[i] * window[i] * gain;
            e += static_cast<double>(frame[i]) * frame[i];
        }
        const float rmsDb = 10.0f * std::log10(static_cast<float>(e / kFft) + 1e-20f);
        fft.magnitudes(buf, mag);
        for (int k = 0; k < kBins; ++k) logMag[k] = std::log1p(50.0f * mag[k]);

        float flux = 0.0f;
        if (havePrev && rmsDb >= silenceDb) {
            const float logFloor = std::log1p(50.0f * magFloor);  // spectral noise gate
            for (int k = 0; k < kBins; ++k) {
                if (logMag[k] < logFloor) continue;  // too quiet to be evidence
                float p = prevLog[k];  // 3-bin max filter (SuperFlux): beats/vibrato robust
                if (k > 0) p = std::max(p, prevLog[k - 1]);
                if (k + 1 < kBins) p = std::max(p, prevLog[k + 1]);
                const float d = logMag[k] - p;
                if (d > binFloor) flux += d;
            }
        }
        std::copy(logMag, logMag + kBins, prevLog.begin());
        havePrev = true;

        if (odfDump) odfDump->emplace_back(frameStart, flux);
        pick.peakFrac = peakFrac;
        pick.push(flux, frameStart, out);
    }
};

// The engine's detector (window under evaluation, see header comment).
using FluxDetector = FluxDetectorT<512>;

}  // namespace webamp
