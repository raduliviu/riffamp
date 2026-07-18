// Offline verification of the pedalboard DSP: each pedal must (a) pass signal
// through unchanged when disabled is irrelevant here — we call processBlock
// directly — and (b) alter the signal in its characteristic way. Exit 0 = pass.
#include <cmath>
#include <cstdio>
#include <numeric>
#include <vector>

#include "pedals.h"

namespace {
int failures = 0;
constexpr float kSr = 48000;

double rms(const std::vector<float>& x) {
    double s = 0;
    for (float v : x) s += static_cast<double>(v) * v;
    return std::sqrt(s / x.size());
}
double peak(const std::vector<float>& x) {
    double p = 0;
    for (float v : x) p = std::max(p, static_cast<double>(std::fabs(v)));
    return p;
}
// A short sine burst followed by silence, used to detect echoes/tails.
std::vector<float> burst(int n, int burstLen, float freq, float amp) {
    std::vector<float> x(n, 0.0f);
    for (int i = 0; i < burstLen && i < n; ++i)
        x[i] = amp * std::sin(2 * webamp::kPi * freq * i / kSr);
    return x;
}
void check(const char* label, bool ok, const char* detail = "") {
    std::printf("%s %-40s %s\n", ok ? "PASS" : "FAIL", label, detail);
    if (!ok) ++failures;
}
}  // namespace

int main() {
    // Compressor: reduces dynamic range — a loud signal is attenuated relative
    // to makeup-compensated output; peak of a hot signal drops.
    {
        webamp::CompressorPedal c;
        c.configure(kSr);
        c.setParam("threshold", -24);
        c.setParam("ratio", 8);
        c.setParam("makeup", 0);
        auto loud = burst(4096, 4096, 220, 0.9f);
        const double before = peak(loud);
        c.processBlock(loud.data(), static_cast<int>(loud.size()));
        const double after = peak(loud);
        check("compressor attenuates hot signal", after < before * 0.9,
              (std::to_string(before) + " -> " + std::to_string(after)).c_str());
    }

    // Drive: adds harmonics / clips — a clean sine gains energy at harmonics.
    // Detect by increased zero-crossing "squareness": RMS/peak ratio rises toward
    // that of a square wave (0.707 -> ~1.0) as it saturates.
    {
        webamp::DrivePedal d;
        d.configure(kSr);
        d.setParam("drive", 1.0f);
        d.setParam("tone", 1.0f);
        d.setParam("level", 1.0f);
        auto sine = burst(4096, 4096, 220, 0.5f);
        const double ratioBefore = rms(sine) / peak(sine);   // ~0.707 for a sine
        d.processBlock(sine.data(), static_cast<int>(sine.size()));
        const double ratioAfter = rms(sine) / peak(sine);    // higher = more saturated
        check("drive saturates (rms/peak rises)", ratioAfter > ratioBefore + 0.05,
              (std::to_string(ratioBefore) + " -> " + std::to_string(ratioAfter)).c_str());
    }

    // Delay: produces an echo after the dry burst ends.
    {
        webamp::DelayPedal dl;
        dl.configure(kSr);
        dl.setParam("time", 100);       // 100 ms = 4800 samples
        dl.setParam("feedback", 0.5f);
        dl.setParam("mix", 0.5f);
        auto x = burst(24000, 2400, 300, 0.7f);  // 50 ms burst, 500 ms total
        dl.processBlock(x.data(), static_cast<int>(x.size()));
        // Energy should appear well after the original burst (around 100 ms in).
        double tailEnergy = 0;
        for (int i = 5000; i < 8000; ++i) tailEnergy += std::fabs(x[i]);
        check("delay produces an echo after the burst", tailEnergy > 1.0,
              ("tail=" + std::to_string(tailEnergy)).c_str());
    }

    // Reverb: leaves a decaying tail after a click; input silence -> a wash.
    {
        webamp::ReverbPedal r;
        r.configure(kSr);
        r.setParam("size", 0.8f);
        r.setParam("mix", 1.0f);
        std::vector<float> x(24000, 0.0f);
        for (int i = 0; i < 64; ++i) x[i] = 0.8f;  // impulse-ish click
        r.processBlock(x.data(), static_cast<int>(x.size()));
        double tail = 0;
        for (int i = 8000; i < 16000; ++i) tail += std::fabs(x[i]);  // 160-330 ms
        check("reverb leaves a decaying tail", tail > 0.5,
              ("tail=" + std::to_string(tail)).c_str());
    }

    // Chorus: alters a steady tone (wet != dry) without destroying it.
    {
        webamp::ChorusPedal ch;
        ch.configure(kSr);
        ch.setParam("rate", 0.5f);
        ch.setParam("depth", 1.0f);
        ch.setParam("mix", 0.5f);
        auto dry = burst(8192, 8192, 330, 0.5f);
        auto wet = dry;
        ch.processBlock(wet.data(), static_cast<int>(wet.size()));
        double diff = 0;
        for (size_t i = 1000; i < dry.size(); ++i) diff += std::fabs(wet[i] - dry[i]);
        check("chorus modulates the signal", diff > 10.0 && peak(wet) > 0.1,
              ("diff=" + std::to_string(diff)).c_str());
    }

    // Param naming: unknown params rejected, known accepted.
    {
        webamp::DelayPedal dl;
        check("setParam rejects unknown", !dl.setParam("bogus", 1) && dl.setParam("time", 300));
        auto pl = dl.paramList();
        check("paramList reports 3 params", pl.size() == 3);
    }

    std::printf(failures ? "%d FAILURES\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
