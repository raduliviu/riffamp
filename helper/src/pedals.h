// Pedalboard effects for the webamp chain. Fixed roster (one of each type),
// each placeable pre/post-amp, reorderable, individually bypassable. Everything
// is pre-allocated in configure() so processBlock() is real-time safe (no locks,
// no allocation). The audio thread reads params from atomics every block.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace webamp {

constexpr float kPi = 3.14159265358979323846f;

struct Pedal {
    std::atomic<bool> enabled{false};
    std::atomic<int> placement{0};       // 0 = pre-amp, 1 = post-amp
    std::atomic<int> order{0};            // signal order within its placement group
    std::atomic<bool> needReset{true};    // control thread requests a state flush

    virtual ~Pedal() = default;
    virtual const char* type() const = 0;
    virtual void configure(float sr) = 0;
    virtual void reset() = 0;
    virtual void processBlock(float* buf, int n) = 0;
    // Returns false if the name isn't a known param. Clamps internally.
    virtual bool setParam(const std::string& name, float v) = 0;
    virtual std::vector<std::pair<std::string, float>> paramList() const = 0;
};

// --- Compressor: feed-forward peak, soft-ish knee via ratio ------------------
struct CompressorPedal : Pedal {
    std::atomic<float> threshold{-18.0f}, ratio{4.0f}, makeup{0.0f};
    float env = 0, attCoef = 0, relCoef = 0;

    const char* type() const override { return "comp"; }
    void configure(float sr) override {
        attCoef = std::exp(-1.0f / (0.005f * sr));   // 5 ms attack
        relCoef = std::exp(-1.0f / (0.120f * sr));   // 120 ms release
    }
    void reset() override { env = 0; }
    void processBlock(float* buf, int n) override {
        const float thr = threshold.load(std::memory_order_relaxed);
        const float r = std::max(1.0f, ratio.load(std::memory_order_relaxed));
        const float mk = std::pow(10.0f, makeup.load(std::memory_order_relaxed) / 20.0f);
        for (int i = 0; i < n; ++i) {
            const float a = std::fabs(buf[i]);
            env = a + (a > env ? attCoef : relCoef) * (env - a);
            const float envDb = 20.0f * std::log10(env + 1e-9f);
            const float over = envDb - thr;
            const float grDb = over > 0.0f ? over * (1.0f / r - 1.0f) : 0.0f;  // <= 0
            buf[i] *= std::pow(10.0f, grDb / 20.0f) * mk;
        }
    }
    bool setParam(const std::string& p, float v) override {
        if (p == "threshold") threshold.store(std::clamp(v, -60.0f, 0.0f));
        else if (p == "ratio") ratio.store(std::clamp(v, 1.0f, 20.0f));
        else if (p == "makeup") makeup.store(std::clamp(v, 0.0f, 24.0f));
        else return false;
        return true;
    }
    std::vector<std::pair<std::string, float>> paramList() const override {
        return {{"threshold", threshold.load()}, {"ratio", ratio.load()}, {"makeup", makeup.load()}};
    }
};

// --- Overdrive: tanh soft clip + one-pole tone + output level ----------------
struct DrivePedal : Pedal {
    std::atomic<float> drive{0.5f}, tone{0.6f}, level{0.5f};
    float lp = 0;

    const char* type() const override { return "drive"; }
    void configure(float) override {}
    void reset() override { lp = 0; }
    void processBlock(float* buf, int n) override {
        const float d = 1.0f + drive.load(std::memory_order_relaxed) * 29.0f;   // 1..30
        const float alpha = 0.05f + tone.load(std::memory_order_relaxed) * 0.9f;
        const float lvl = level.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) {
            const float clipped = std::tanh(buf[i] * d);
            lp += alpha * (clipped - lp);
            buf[i] = lp * lvl;
        }
    }
    bool setParam(const std::string& p, float v) override {
        if (p == "drive") drive.store(std::clamp(v, 0.0f, 1.0f));
        else if (p == "tone") tone.store(std::clamp(v, 0.0f, 1.0f));
        else if (p == "level") level.store(std::clamp(v, 0.0f, 1.0f));
        else return false;
        return true;
    }
    std::vector<std::pair<std::string, float>> paramList() const override {
        return {{"drive", drive.load()}, {"tone", tone.load()}, {"level", level.load()}};
    }
};

// --- Chorus: single LFO-modulated delay line, wet/dry mix --------------------
struct ChorusPedal : Pedal {
    std::atomic<float> rate{0.4f}, depth{0.5f}, mix{0.5f};
    std::vector<float> buf;
    int writePos = 0;
    float sr = 48000, lfoPhase = 0;

    const char* type() const override { return "chorus"; }
    void configure(float s) override {
        sr = s;
        buf.assign(static_cast<size_t>(0.05f * sr) + 4, 0.0f);  // 50 ms
    }
    void reset() override { std::fill(buf.begin(), buf.end(), 0.0f); writePos = 0; lfoPhase = 0; }
    void processBlock(float* x, int n) override {
        const int len = static_cast<int>(buf.size());
        const float rHz = 0.1f + rate.load(std::memory_order_relaxed) * 4.9f;
        const float dep = depth.load(std::memory_order_relaxed);
        const float mx = mix.load(std::memory_order_relaxed);
        const float inc = 2.0f * kPi * rHz / sr;
        for (int i = 0; i < n; ++i) {
            buf[writePos] = x[i];
            lfoPhase += inc;
            if (lfoPhase > 2.0f * kPi) lfoPhase -= 2.0f * kPi;
            const float delayMs = 15.0f + 10.0f * dep * (0.5f + 0.5f * std::sin(lfoPhase));
            float readPos = writePos - delayMs * 0.001f * sr;
            while (readPos < 0) readPos += len;
            const int r0 = static_cast<int>(readPos);
            const float frac = readPos - r0;
            const float wet = buf[r0] + frac * (buf[(r0 + 1) % len] - buf[r0]);
            x[i] = x[i] * (1.0f - mx) + wet * mx;
            if (++writePos >= len) writePos = 0;
        }
    }
    bool setParam(const std::string& p, float v) override {
        if (p == "rate") rate.store(std::clamp(v, 0.0f, 1.0f));
        else if (p == "depth") depth.store(std::clamp(v, 0.0f, 1.0f));
        else if (p == "mix") mix.store(std::clamp(v, 0.0f, 1.0f));
        else return false;
        return true;
    }
    std::vector<std::pair<std::string, float>> paramList() const override {
        return {{"rate", rate.load()}, {"depth", depth.load()}, {"mix", mix.load()}};
    }
};

// --- Delay: feedback delay line, up to 2 s -----------------------------------
struct DelayPedal : Pedal {
    std::atomic<float> time{350.0f}, feedback{0.35f}, mix{0.30f};
    std::vector<float> buf;
    int writePos = 0;
    float sr = 48000;

    const char* type() const override { return "delay"; }
    void configure(float s) override {
        sr = s;
        buf.assign(static_cast<size_t>(2.0f * sr) + 4, 0.0f);
    }
    void reset() override { std::fill(buf.begin(), buf.end(), 0.0f); writePos = 0; }
    void processBlock(float* x, int n) override {
        const int len = static_cast<int>(buf.size());
        float dSamp = time.load(std::memory_order_relaxed) * 0.001f * sr;
        dSamp = std::clamp(dSamp, 1.0f, static_cast<float>(len - 2));
        const float fb = feedback.load(std::memory_order_relaxed);
        const float mx = mix.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) {
            float readPos = writePos - dSamp;
            while (readPos < 0) readPos += len;
            const int r0 = static_cast<int>(readPos);
            const float frac = readPos - r0;
            const float wet = buf[r0] + frac * (buf[(r0 + 1) % len] - buf[r0]);
            buf[writePos] = x[i] + wet * fb;
            x[i] = x[i] * (1.0f - mx) + wet * mx;
            if (++writePos >= len) writePos = 0;
        }
    }
    bool setParam(const std::string& p, float v) override {
        if (p == "time") time.store(std::clamp(v, 20.0f, 2000.0f));
        else if (p == "feedback") feedback.store(std::clamp(v, 0.0f, 0.95f));
        else if (p == "mix") mix.store(std::clamp(v, 0.0f, 1.0f));
        else return false;
        return true;
    }
    std::vector<std::pair<std::string, float>> paramList() const override {
        return {{"time", time.load()}, {"feedback", feedback.load()}, {"mix", mix.load()}};
    }
};

// --- Reverb: mono Freeverb (8 combs + 4 allpasses) ---------------------------
struct ReverbPedal : Pedal {
    std::atomic<float> size{0.6f}, damp{0.5f}, mix{0.3f};

    struct Comb {
        std::vector<float> buf;
        int pos = 0;
        float store = 0;
        void set(int n) { buf.assign(std::max(1, n), 0.0f); pos = 0; store = 0; }
        float process(float in, float fb, float dmp) {
            const float y = buf[pos];
            store = y * (1.0f - dmp) + store * dmp;
            buf[pos] = in + store * fb;
            if (++pos >= static_cast<int>(buf.size())) pos = 0;
            return y;
        }
    };
    struct Allpass {
        std::vector<float> buf;
        int pos = 0;
        void set(int n) { buf.assign(std::max(1, n), 0.0f); pos = 0; }
        float process(float in) {
            const float y = buf[pos];
            const float out = -in + y;
            buf[pos] = in + y * 0.5f;
            if (++pos >= static_cast<int>(buf.size())) pos = 0;
            return out;
        }
    };
    Comb combs[8];
    Allpass aps[4];

    const char* type() const override { return "reverb"; }
    void configure(float sr) override {
        static const int kComb[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static const int kAp[4] = {556, 441, 341, 225};
        const float scale = sr / 44100.0f;
        for (int i = 0; i < 8; ++i) combs[i].set(static_cast<int>(kComb[i] * scale));
        for (int i = 0; i < 4; ++i) aps[i].set(static_cast<int>(kAp[i] * scale));
    }
    void reset() override {
        for (auto& c : combs) { std::fill(c.buf.begin(), c.buf.end(), 0.0f); c.store = 0; }
        for (auto& a : aps) std::fill(a.buf.begin(), a.buf.end(), 0.0f);
    }
    void processBlock(float* x, int n) override {
        const float fb = 0.70f + size.load(std::memory_order_relaxed) * 0.28f;   // 0.70..0.98
        const float dmp = damp.load(std::memory_order_relaxed) * 0.4f;
        const float mx = mix.load(std::memory_order_relaxed);
        constexpr float kGain = 0.015f;
        for (int i = 0; i < n; ++i) {
            const float in = x[i] * kGain;
            float out = 0;
            for (auto& c : combs) out += c.process(in, fb, dmp);
            for (auto& a : aps) out = a.process(out);
            x[i] = x[i] * (1.0f - mx) + out * mx;
        }
    }
    bool setParam(const std::string& p, float v) override {
        if (p == "size") size.store(std::clamp(v, 0.0f, 1.0f));
        else if (p == "damp") damp.store(std::clamp(v, 0.0f, 1.0f));
        else if (p == "mix") mix.store(std::clamp(v, 0.0f, 1.0f));
        else return false;
        return true;
    }
    std::vector<std::pair<std::string, float>> paramList() const override {
        return {{"size", size.load()}, {"damp", damp.load()}, {"mix", mix.load()}};
    }
};

}  // namespace webamp
