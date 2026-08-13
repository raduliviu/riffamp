// Offline test for the picking-trainer DSP: does the onset detector count
// synthesized plucks correctly at practice tempos, and do the stats tell
// 16ths from triplets at 180 BPM (the feature's whole reason to exist)?
//
// Build: picking_test target; run with no args, exits 1 on failure.

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "picking.h"
#include "pick_run.h"

namespace {

constexpr float kSr = 48000.0f;
int failures = 0;

void check(const std::string& name, bool ok, const std::string& detail = "") {
    std::printf("%s %s%s\n", ok ? "PASS" : "FAIL", name.c_str(),
                detail.empty() ? "" : ("  [" + detail + "]").c_str());
    if (!ok) ++failures;
}

// A pluck: sharp attack, exponentially decaying tone + a little noise —
// crude but has the right envelope shape for the detector.
void addPluck(std::vector<float>& buf, size_t at, float amp, float freq = 330.0f) {
    std::mt19937 gen(static_cast<unsigned>(at));  // deterministic
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    const float decay = std::exp(-1.0f / (0.040f * kSr));  // ~40 ms tail
    float env = amp;
    for (size_t i = at; i < buf.size() && env > 1e-4f; ++i) {
        const float t = static_cast<float>(i - at) / kSr;
        buf[i] += env * (0.8f * std::sin(2.0f * 3.14159265f * freq * t) + 0.2f * noise(gen));
        env *= decay;
    }
}

// A ringing (open-string) pluck: a brief broadband attack transient (the pick
// scrape) followed by a long tonal ring (~500 ms) that overlaps the next
// notes. This is the case the field exposed: palm mutes (short tails, big
// level rises) detected fine while ringing notes buried each other — the
// sustain holds the lagging envelope up, so a new attack never clears the
// rise ratio without spectral separation.
void addRingingPluck(std::vector<float>& buf, size_t at, float amp, float freq = 330.0f) {
    std::mt19937 gen(static_cast<unsigned>(at));
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    const size_t burst = static_cast<size_t>(0.004f * kSr);     // 4 ms scrape
    const float decay = std::exp(-1.0f / (0.500f * kSr));        // ~500 ms ring
    float env = amp;
    for (size_t i = at; i < buf.size() && env > 1e-4f; ++i) {
        const float t = static_cast<float>(i - at) / kSr;
        const float scrape = (i - at) < burst ? 0.6f * noise(gen) : 0.0f;
        buf[i] += env * (0.9f * std::sin(2.0f * 3.14159265f * freq * t) +
                         0.03f * noise(gen) + scrape);
        env *= decay;
    }
}

// Run the detector over a buffer, returning onset timestamps (sample index).
std::vector<uint64_t> detect(const std::vector<float>& buf, float sens = 0.5f) {
    webamp::OnsetDetector det;
    det.configure(kSr);
    det.setSensitivity(sens);
    std::vector<uint64_t> ts;
    for (size_t i = 0; i < buf.size(); ++i)
        if (det.process(buf[i])) ts.push_back(i);
    return ts;
}

}  // namespace

int main() {
    const double bpm = 180.0;
    const double beat = kSr * 60.0 / bpm;  // 16000 samples per beat @180

    // 1. Single pluck -> exactly one onset (no double trigger on one attack).
    {
        std::vector<float> buf(static_cast<size_t>(kSr), 0.0f);
        addPluck(buf, 4800, 0.5f);
        const auto ts = detect(buf);
        check("single pluck -> 1 onset", ts.size() == 1, "got " + std::to_string(ts.size()));
    }

    // 2. Silence -> nothing.
    {
        std::vector<float> buf(static_cast<size_t>(kSr), 0.0f);
        check("silence -> 0 onsets", detect(buf).empty());
    }

    // 3. Sustained tone (no attacks) -> at most the initial onset.
    {
        std::vector<float> buf(static_cast<size_t>(kSr * 2), 0.0f);
        for (size_t i = 0; i < buf.size(); ++i)
            buf[i] = 0.4f * std::sin(2.0f * 3.14159265f * 220.0f * i / kSr);
        const auto ts = detect(buf);
        check("sustained tone -> <=1 onset", ts.size() <= 1, "got " + std::to_string(ts.size()));
    }

    // 4. 16ths at 180 BPM (IOI 83 ms): 4 beats * 4 notes = 16 plucks, all found.
    {
        const double ioi = beat / 4.0;
        std::vector<float> buf(static_cast<size_t>(beat * 5), 0.0f);
        for (int n = 0; n < 16; ++n)
            addPluck(buf, static_cast<size_t>(1000 + n * ioi), 0.45f);
        const auto ts = detect(buf);
        check("16ths @180: all 16 onsets", ts.size() == 16, "got " + std::to_string(ts.size()));
        const double npb = webamp::notesPerBeat(beat, webamp::medianIoi(ts));
        check("16ths @180: notesPerBeat ~ 4", std::fabs(npb - 4.0) < 0.15,
              "npb=" + std::to_string(npb));
        const double cv = webamp::ioiCv(ts);
        check("16ths @180: even (cv < 0.05)", cv >= 0 && cv < 0.05, "cv=" + std::to_string(cv));
    }

    // 5. Triplets at 180 BPM (IOI 111 ms) — the failure mode to expose.
    {
        const double ioi = beat / 3.0;
        std::vector<float> buf(static_cast<size_t>(beat * 5), 0.0f);
        for (int n = 0; n < 12; ++n)
            addPluck(buf, static_cast<size_t>(1000 + n * ioi), 0.45f);
        const auto ts = detect(buf);
        check("triplets @180: all 12 onsets", ts.size() == 12, "got " + std::to_string(ts.size()));
        const double npb = webamp::notesPerBeat(beat, webamp::medianIoi(ts));
        check("triplets @180: notesPerBeat ~ 3", std::fabs(npb - 3.0) < 0.15,
              "npb=" + std::to_string(npb));
    }

    // 6. Uneven "gallop" (alternating long-short IOIs) -> high cv.
    {
        std::vector<float> buf(static_cast<size_t>(beat * 5), 0.0f);
        double pos = 1000;
        for (int n = 0; n < 16; ++n) {
            addPluck(buf, static_cast<size_t>(pos), 0.45f);
            pos += (n % 2 ? 0.65 : 0.35) * (beat / 2.0);  // swingy 8ths
        }
        const double cv = webamp::ioiCv(detect(buf));
        check("gallop -> uneven (cv > 0.15)", cv > 0.15, "cv=" + std::to_string(cv));
    }

    // 7. Varying pick strength (accents) still counts every note.
    {
        const double ioi = beat / 4.0;
        std::vector<float> buf(static_cast<size_t>(beat * 5), 0.0f);
        for (int n = 0; n < 16; ++n)
            addPluck(buf, static_cast<size_t>(1000 + n * ioi), n % 4 == 0 ? 0.6f : 0.3f);
        const auto ts = detect(buf);
        check("accented 16ths: all 16 onsets", ts.size() == 16, "got " + std::to_string(ts.size()));
    }

    // 8b. Ringing open-string notes (the field report): 16ths at 160 BPM with
    // ~500 ms ring — every note overlaps the next several. All must be found,
    // same as palm mutes; alternating string pitches ring over each other too.
    {
        const double beat160 = kSr * 60.0 / 160.0;
        const double ioi = beat160 / 4.0;  // ~94 ms — well inside the ring
        std::vector<float> buf(static_cast<size_t>(beat160 * 6), 0.0f);
        for (int n = 0; n < 16; ++n)
            addRingingPluck(buf, static_cast<size_t>(1000 + n * ioi), 0.45f,
                            n % 2 ? 247.0f : 330.0f);
        const auto ts = detect(buf);
        check("ringing 16ths @160: all 16 onsets", ts.size() == 16,
              "got " + std::to_string(ts.size()));
    }

    // 8c. One ringing pluck must not retrigger during its own long sustain.
    {
        std::vector<float> buf(static_cast<size_t>(kSr * 2), 0.0f);
        addRingingPluck(buf, 4800, 0.5f);
        const auto ts = detect(buf);
        check("ringing pluck -> 1 onset", ts.size() == 1,
              "got " + std::to_string(ts.size()));
    }

    // 8. Fast extreme: 16ths at 240 BPM (IOI 62 ms) — above the refractory.
    {
        const double beat240 = kSr * 60.0 / 240.0;
        const double ioi = beat240 / 4.0;
        std::vector<float> buf(static_cast<size_t>(beat240 * 5), 0.0f);
        for (int n = 0; n < 16; ++n)
            addPluck(buf, static_cast<size_t>(1000 + n * ioi), 0.45f);
        const auto ts = detect(buf);
        check("16ths @240: all 16 onsets", ts.size() == 16, "got " + std::to_string(ts.size()));
    }

    // --- PickRun (P5b): count-in gating, boundary end, result contents -------
    {
        // 120 bpm at 48k: click every 24000 samples. countIn=1, bars=2, bpb=4
        // -> 12 run beats; the 13th click is the boundary.
        const uint64_t step = 24000, first = 10000;
        webamp::PickRun run;
        run.start(2, 1, 4, kSr, 5000);

        check("stale click ignored", !run.feedClick(1000) && run.active());

        bool ended = false;
        int endedAt = -1;
        for (int k = 0; k < 13; ++k) {
            // Onsets: perfect 16ths within each run beat (skip count-in beats
            // 0-3); plus one count-in onset that must be excluded.
            if (k == 1) run.feedOnset(first + step);  // during count-in
            if (ended) break;
            if (run.feedClick(first + k * step)) { ended = true; endedAt = k; }
            if (k >= 4 && k < 12)
                for (int s = 0; s < 4; ++s)
                    run.feedOnset(first + k * step + s * step / 4);
        }
        check("boundary click ends run", ended && endedAt == 12,
              "endedAt=" + std::to_string(endedAt));

        const uint64_t tEnd = first + 12 * step;
        check("poll before margin -> null", run.poll(tEnd + 1000).is_null());
        const auto r = run.poll(tEnd + step);  // half a beat = 12000 < step
        check("poll after margin -> result", !r.is_null() && r["type"] == "pickRunResult");
        check("result grid: bars*bpb+1 clicks", r["clicks"].size() == 9,
              "n=" + std::to_string(r["clicks"].size()));
        check("result grid starts at 0 ms", std::fabs(r["clicks"][0].get<double>()) < 1e-6);
        check("result grid spacing 500 ms",
              std::fabs(r["clicks"][1].get<double>() - 500.0) < 1e-6);
        check("count-in onsets excluded, run onsets kept", r["onsets"].size() == 32,
              "n=" + std::to_string(r["onsets"].size()));
        check("first onset at 0 ms", std::fabs(r["onsets"][0].get<double>()) < 1e-6);
        check("run idle after result", !run.active());
        check("second poll -> null", run.poll(tEnd + 2 * step).is_null());
    }

    // PickRun: status phases + early-margin onset + cancel.
    {
        const uint64_t step = 24000, first = 10000;
        webamp::PickRun run;
        run.start(1, 1, 4, kSr, 0);
        auto s = run.poll(0);
        check("armed status is countIn", s["type"] == "pickRun" && s["phase"] == "countIn");
        run.feedClick(first);
        run.feedClick(first + step);
        s = run.poll(first + step);
        check("countIn beat 2", s["phase"] == "countIn" && s["beat"] == 2, s.dump());
        for (int k = 2; k < 8; ++k) run.feedClick(first + k * step);
        s = run.poll(first + 7 * step);
        check("recording bar 1 beat 4", s["phase"] == "recording" && s["beat"] == 4, s.dump());
        // Anticipated first note: 100 ms early is inside the half-beat margin.
        run.feedOnset(first + 4 * step - 4800);
        const bool endedHere = run.feedClick(first + 8 * step);
        check("1-bar run ends at 9th click", endedHere);
        const auto r = run.poll(first + 9 * step);
        check("early onset kept (negative ms)",
              r["onsets"].size() == 1 && r["onsets"][0].get<double>() < 0.0,
              r["onsets"].dump());

        run.start(1, 1, 4, kSr, 0);
        run.feedClick(first);
        run.cancel();
        check("cancel -> inactive, poll null", !run.active() && run.poll(first + step).is_null());
    }

    std::printf("\n%s\n", failures ? "FAILURES" : "ALL PASS");
    return failures ? 1 : 0;
}
