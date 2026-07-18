// Offline verification of the YIN tuner: synthesized guitar strings, detuned
// cases, harmonics-rich tones, and silence. Exit 0 = all pass.
#include <cmath>
#include <cstdio>
#include <vector>

#include "tuner.h"

namespace {

std::vector<float> synth(float freq, float sr, int n, bool harmonics) {
    std::vector<float> x(n);
    for (int i = 0; i < n; ++i) {
        const double t = i / sr;
        double v = std::sin(2 * 3.14159265358979 * freq * t);
        if (harmonics) {  // pluck-ish spectrum: strong 2nd/3rd harmonics
            v += 0.5 * std::sin(2 * 3.14159265358979 * 2 * freq * t + 0.7);
            v += 0.3 * std::sin(2 * 3.14159265358979 * 3 * freq * t + 1.3);
        }
        x[i] = static_cast<float>(0.3 * v);
    }
    return x;
}

int failures = 0;

void expectPitch(float freq, bool harmonics, const char* label) {
    const float sr = 48000;
    auto x = synth(freq, sr, 4096, harmonics);
    const float got = webamp::detectPitch(x.data(), 4096, sr);
    const float centsErr = got > 0 ? 1200.0f * std::log2(got / freq) : 9999.0f;
    const bool ok = got > 0 && std::fabs(centsErr) < 2.0f;  // within 2 cents
    std::printf("%s %-28s expect %7.2f Hz  got %7.2f Hz  err %+6.2f cents\n",
                ok ? "PASS" : "FAIL", label, freq, got, centsErr);
    if (!ok) ++failures;
}

}  // namespace

int main() {
    // Standard tuning fundamentals.
    expectPitch(82.41f, true, "E2 (low E, harmonics)");
    expectPitch(110.00f, true, "A2");
    expectPitch(146.83f, true, "D3");
    expectPitch(196.00f, true, "G3");
    expectPitch(246.94f, true, "B3");
    expectPitch(329.63f, true, "E4 (high E)");
    // Detuned cases the tuner must resolve precisely.
    expectPitch(84.0f, true, "E2 +33 cents sharp");
    expectPitch(80.0f, true, "E2 -51 cents flat");
    expectPitch(440.0f, false, "A4 pure sine");
    // Drop tunings.
    expectPitch(73.42f, true, "D2 (drop D)");
    expectPitch(65.41f, true, "C2 (drop C)");

    // Silence must not report a pitch.
    std::vector<float> quiet(4096, 0.0f);
    const float silent = webamp::detectPitch(quiet.data(), 4096, 48000);
    std::printf("%s silence -> no pitch (got %.2f)\n", silent < 0 ? "PASS" : "FAIL", silent);
    if (silent >= 0) ++failures;

    // Note naming.
    const auto e2 = webamp::describeNote(82.41f);
    const auto sharp = webamp::describeNote(84.0f);
    const bool namesOk = e2.name == "E2" && std::fabs(e2.cents) < 1.0f &&
                         sharp.name == "E2" && sharp.cents > 25.0f;
    std::printf("%s note naming (E2 %.1fc, sharp %.1fc)\n", namesOk ? "PASS" : "FAIL",
                e2.cents, sharp.cents);
    if (!namesOk) ++failures;

    std::printf(failures ? "%d FAILURES\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
