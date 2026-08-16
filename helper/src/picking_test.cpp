// Offline test for the picking-trainer DSP: does the onset detector count
// synthesized plucks correctly at practice tempos, and do the stats tell
// 16ths from triplets at 180 BPM (the feature's whole reason to exist)?
//
// Build: picking_test target; run with no args, exits 1 on failure.

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "picking.h"
#include "flux.h"
#include "pick_run.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

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

// A fret/string squeak: a quiet tonal chirp (~2.8→3.4 kHz over ~25 ms). This
// is what real finger/fret noise looks like spectrally — narrowband and
// gliding — unlike a white-noise burst, which is broadband novelty that any
// spectral detector (and the ear) legitimately reads as a percussive event.
void addSqueak(std::vector<float>& buf, size_t at, float amp) {
    const size_t len = static_cast<size_t>(0.025f * kSr);
    double phase = 0.0;
    for (size_t i = at; i < at + len && i < buf.size(); ++i) {
        const float u = static_cast<float>(i - at) / len;
        const float freq = 2800.0f + 600.0f * u;
        phase += 2.0 * 3.14159265 * freq / kSr;
        const float env = std::sin(3.14159265f * u);  // smooth in/out
        buf[i] += amp * env * static_cast<float>(std::sin(phase));
    }
}

// A messy real-world pluck (the over-triggering field report): a squeak just
// BEFORE the attack (finger repositioning), the attack itself, another squeak
// after it, and a long ring.
void addMessyPluck(std::vector<float>& buf, size_t at, float amp, float freq = 330.0f) {
    const size_t pre = static_cast<size_t>(0.030f * kSr);
    addSqueak(buf, at > pre ? at - pre : 0, 0.15f * amp);  // reposition squeak
    addRingingPluck(buf, at, amp, freq);                   // the real note
    addSqueak(buf, at + static_cast<size_t>(0.045f * kSr), 0.12f * amp);
}

// Run the detector over a buffer, returning onset timestamps (sample index).
std::vector<uint64_t> detect(const std::vector<float>& buf, float sens = 0.5f,
                             float minGapSec = 0.0f) {
    webamp::FluxDetector det;
    det.configure(kSr);
    det.setSensitivity(sens);
    if (minGapSec > 0) det.setMinGap(minGapSec);
    std::vector<uint64_t> ts;
    det.push(buf.data(), static_cast<int>(buf.size()), ts);
    return ts;
}

// Offline analysis of a real capture (webamp-capture.wav from the helper's
// captureInput command): run the actual detector over actual playing and
// print every onset with levels, so constants get tuned against reality
// instead of synthetic plucks.
//   picking_test <wav> [sens=0.5] [minGapMs=0] [bpm target — prints npb]
int analyzeWav(const char* path, float sens, float minGapMs, double bpm, int target,
               float binFloor, float peakFrac, bool quiet) {
    unsigned int ch = 0, sr = 0;
    drwav_uint64 frames = 0;
    float* data = drwav_open_file_and_read_pcm_frames_f32(path, &ch, &sr, &frames, nullptr);
    if (!data) {
        std::printf("cannot read %s\n", path);
        return 2;
    }
    std::vector<float> mono(frames);
    for (drwav_uint64 f = 0; f < frames; ++f) mono[f] = data[f * ch];
    drwav_free(data, nullptr);

    webamp::FluxDetector det;
    det.configure(static_cast<float>(sr));
    det.setSensitivity(sens);
    if (binFloor > 0) det.binFloor = binFloor;
    if (peakFrac >= 0) det.peakFrac = peakFrac;
    if (minGapMs > 0)
        det.setMinGap(minGapMs / 1000.0f);  // explicit gap wins over the bpm-derived default
    else if (bpm > 0 && target > 0)
        det.setMinGap(0.45f * 60.0f / static_cast<float>(bpm * target));

    std::vector<uint64_t> ts;
    det.push(mono.data(), static_cast<int>(mono.size()), ts);
    if (!quiet) {
        std::printf("file: %s  (%.2f s @ %u Hz)  sens=%.2f binFloor=%.2f peakFrac=%.2f\n",
                    path, static_cast<double>(frames) / sr, sr, sens, det.binFloor,
                    det.peakFrac);
        for (size_t i = 0; i < ts.size(); ++i) {
            const double ms = 1000.0 * static_cast<double>(ts[i]) / sr;
            const double ioi =
                i == 0 ? 0.0 : 1000.0 * static_cast<double>(ts[i] - ts[i - 1]) / sr;
            std::printf("onset %3zu  t=%8.1f ms  ioi=%7.1f ms\n", i + 1, ms, ioi);
        }
    }
    const double beat = bpm > 0 ? sr * 60.0 / bpm : 0.0;
    std::printf("total onsets: %zu", ts.size());
    if (ts.size() >= 4) {
        std::printf("   median IOI %.1f ms   cv %.3f",
                    1000.0 * webamp::medianIoi(ts) / sr, webamp::ioiCv(ts));
        if (beat > 0)
            std::printf("   npb %.2f", webamp::notesPerBeat(beat, webamp::medianIoi(ts)));
    }
    std::printf("\n");

    // Grid fit vs the expected subdivision (best phase; ±40 ms tolerance):
    // distinct gridpoints hit vs ghosts (off-grid or duplicate detections).
    if (bpm > 0 && target > 0 && !ts.empty()) {
        const double sub = sr * 60.0 / (bpm * target);
        int bestHits = -1;
        int bestGhosts = 0;
        double bestErr = 0;
        for (double phase = 0; phase < sub; phase += sub / 64.0) {
            std::vector<long long> pts;
            int offGrid = 0;
            double errSum = 0;
            for (uint64_t t : ts) {
                const double e = static_cast<double>(t) - phase;
                const long long k = std::llround(e / sub);
                const double err = e - k * sub;
                if (std::fabs(err) <= 0.040 * sr) {
                    pts.push_back(k);
                    errSum += std::fabs(err);
                } else {
                    ++offGrid;
                }
            }
            std::sort(pts.begin(), pts.end());
            const int hits =
                static_cast<int>(std::unique(pts.begin(), pts.end()) - pts.begin());
            const int ghosts = offGrid + (static_cast<int>(pts.size()) - hits);
            if (hits > bestHits) {
                bestHits = hits;
                bestGhosts = ghosts;
                bestErr = pts.empty() ? 0 : errSum / static_cast<double>(pts.size());
            }
        }
        std::printf("grid fit: %d gridpoints hit, %d ghosts, mean |err| %.1f ms\n",
                    bestHits, bestGhosts, 1000.0 * bestErr / sr);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    {   // FFT self-test: magnitudes must be phase-invariant for a steady sine.
        webamp::Fft512 fft;
        fft.init();
        float a[512], b[512], ma[257], mb[257];
        for (int i = 0; i < 512; ++i) {
            a[i] = std::sin(2.0f * 3.14159265f * 220.0f * i / kSr);
            b[i] = std::sin(2.0f * 3.14159265f * 220.0f * (i + 137) / kSr);  // shifted phase
        }
        fft.magnitudes(a, ma);
        fft.magnitudes(b, mb);
        float maxDiff = 0, maxMag = 0;
        for (int k = 0; k < 257; ++k) {
            maxDiff = std::max(maxDiff, std::fabs(ma[k] - mb[k]));
            maxMag = std::max(maxMag, ma[k]);
        }
        std::printf("FFT self-test: peak mag %.2f, phase-shift mag diff %.4f (%s)\n",
                    maxMag, maxDiff, maxDiff < 0.05f * maxMag ? "ok" : "BROKEN");
    }
    // wav mode: picking_test <wav> [sens] [minGapMs] [bpm] [target] [binFloor] [peakFrac] [quiet]
    if (argc > 1)
        return analyzeWav(argv[1], argc > 2 ? static_cast<float>(std::atof(argv[2])) : 0.5f,
                          argc > 3 ? static_cast<float>(std::atof(argv[3])) : 0.0f,
                          argc > 4 ? std::atof(argv[4]) : 0.0,
                          argc > 5 ? std::atoi(argv[5]) : 4,
                          argc > 6 ? static_cast<float>(std::atof(argv[6])) : 0.0f,
                          argc > 7 ? static_cast<float>(std::atof(argv[7])) : -1.0f,
                          argc > 8);
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
        if (ts.size() > 1) {  // debug: where do the ghosts land?
            std::printf("   ghost times ms:");
            for (auto t : ts) std::printf(" %.0f", 1000.0 * t / kSr);
            std::printf("\n");
        }
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

    // 8d. Messy real-world plucks (scrape + buzz + ring): exactly one onset per
    // note — the spurious HF transients must not count. The expected-rate gate
    // (~45% of a 16th at 160) mirrors what the engine sets during a run.
    {
        const double beat160 = kSr * 60.0 / 160.0;
        const double ioi = beat160 / 4.0;
        std::vector<float> buf(static_cast<size_t>(beat160 * 6), 0.0f);
        for (int n = 0; n < 16; ++n)
            addMessyPluck(buf, static_cast<size_t>(2000 + n * ioi), 0.45f,
                          n % 2 ? 247.0f : 330.0f);
        const auto ts = detect(buf, 0.5f, 0.45f * static_cast<float>(ioi / kSr));
        check("messy 16ths @160: exactly 16 onsets", ts.size() == 16,
              "got " + std::to_string(ts.size()));
    }

    // 8f. Notes over a chord bed (field report 3): several strings already
    // ringing loudly under the run — the bed holds the full-band envelope up,
    // so a rise gate on the full band silently vetoes real notes (the
    // palm-mute bias reborn). Every note on top must still count.
    {
        const double beat160 = kSr * 60.0 / 160.0;
        const double ioi = beat160 / 4.0;
        std::vector<float> buf(static_cast<size_t>(beat160 * 6), 0.0f);
        addRingingPluck(buf, 100, 0.40f, 196.0f);  // the bed: three low strings
        addRingingPluck(buf, 300, 0.40f, 147.0f);
        addRingingPluck(buf, 500, 0.40f, 110.0f);
        const size_t runStart = static_cast<size_t>(0.1 * kSr);
        for (int n = 0; n < 16; ++n)
            addRingingPluck(buf, runStart + static_cast<size_t>(n * ioi), 0.35f,
                            n % 2 ? 247.0f : 330.0f);
        const auto all = detect(buf, 0.5f, 0.45f * static_cast<float>(ioi / kSr));
        int inRun = 0;
        for (uint64_t t : all)
            if (t >= runStart - 200) ++inRun;
        check("chord bed: all 16 notes on top detected", inRun == 16,
              "got " + std::to_string(inRun));
    }

    // 8e. Fret squeak alone on top of a ring (no new note) -> no onset.
    {
        std::vector<float> buf(static_cast<size_t>(kSr * 2), 0.0f);
        addRingingPluck(buf, 4800, 0.5f);
        addSqueak(buf, 4800 + static_cast<size_t>(0.150f * kSr), 0.06f);  // mid-ring
        const auto ts = detect(buf);
        check("squeak on a ring -> no extra onset", ts.size() == 1,
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
    // Four-pitch cycle: at this speed a real run crosses strings, and a
    // same-pitch re-pick would damp the old ring at pick contact (additive
    // synthesis can't model that, so same-pitch repicks over an undamped ring
    // are an impossible signal, not a hard case).
    {
        const double beat240 = kSr * 60.0 / 240.0;
        const double ioi = beat240 / 4.0;
        const float cycle[4] = {330.0f, 247.0f, 392.0f, 294.0f};
        std::vector<float> buf(static_cast<size_t>(beat240 * 5), 0.0f);
        for (int n = 0; n < 16; ++n)
            addPluck(buf, static_cast<size_t>(1000 + n * ioi), 0.45f, cycle[n % 4]);
        const auto ts = detect(buf);
        check("16ths @240: all 16 onsets", ts.size() == 16, "got " + std::to_string(ts.size()));
        if (ts.size() != 16) {
            std::printf("   expected every %.0f smp from 1000:", ioi);
            for (auto t : ts) std::printf(" %llu", static_cast<unsigned long long>(t));
            std::printf("\n   flux frames 5500-9000 smp:\n");
            webamp::FluxDetector dbg;
            dbg.configure(kSr);
            std::vector<uint64_t> sink;
            uint64_t lastT = ~0ull;
            for (size_t i = 0; i < buf.size(); i += 256) {
                dbg.push(buf.data() + i, 256, sink);
                const int n = dbg.histN;
                if (n > 0 && dbg.histT[n - 1] != lastT) {
                    lastT = dbg.histT[n - 1];
                    const uint64_t t = lastT - webamp::FluxDetector::kFft;
                    if (t >= 5500 && t <= 9000)
                        std::printf("     t=%5llu flux=%7.3f\n",
                                    static_cast<unsigned long long>(t), dbg.hist[n - 1]);
                }
            }
        }
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
