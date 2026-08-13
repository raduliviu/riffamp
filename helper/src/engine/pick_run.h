// Pick-run recorder (P5b): a fixed-length practice run for the picking trainer.
// Control-thread state machine — never touched by the audio callback. The grid
// is the metronome's actual click timestamps (sample-accurate, drained from the
// engine's click ring), so the analysis is immune to bpm rounding or drift.
//
// Lifecycle: start() arms it (the caller restarts the metronome and enables the
// onset detector); clicks stream in via feedClick — the first countIn*bpb are
// the count-in, the next bars*bpb are the recorded phrase, and the boundary
// click after the last bar ends it (feedClick returns true so the caller stops
// the metronome). The result is not built at the boundary: rings drain ~25 Hz
// and a just-late last pickstroke still belongs to the phrase, so poll(now)
// finalizes half a beat after the boundary and returns the result exactly once.
// The result is raw timestamps (ms); all scoring happens in the UI.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "json.hpp"

namespace webamp {

using nlohmann::json;

struct PickRun {
    // Arm a run. startClock (engine sample time at the moment of arming) fences
    // off clicks already in flight from before the metronome restart.
    void start(int bars_, int countIn_, int beatsPerBar_, double sr_, uint64_t startClock_) {
        std::lock_guard<std::mutex> lk(mx);
        bars = std::clamp(bars_, 1, 16);
        countIn = std::clamp(countIn_, 1, 2);
        beatsPerBar = std::max(1, beatsPerBar_);
        sr = sr_ > 0 ? sr_ : 48000.0;
        startClock = startClock_;
        clicks.clear();
        onsets.clear();
        phase = Phase::run;
    }
    void cancel() {
        std::lock_guard<std::mutex> lk(mx);
        phase = Phase::idle;
    }
    bool active() {
        std::lock_guard<std::mutex> lk(mx);
        return phase != Phase::idle;
    }

    // A metronome click fired at sample time ts. Returns true exactly once, at
    // the boundary click after the last bar — stop the metronome then and keep
    // polling for the result.
    bool feedClick(uint64_t ts) {
        std::lock_guard<std::mutex> lk(mx);
        if (phase != Phase::run || ts < startClock) return false;  // stale/foreign click
        clicks.push_back(ts);
        if (static_cast<int>(clicks.size()) > (countIn + bars) * beatsPerBar) {
            phase = Phase::ending;  // stragglers may still drain; finalize in poll()
            return true;
        }
        return false;
    }
    void feedOnset(uint64_t ts) {
        std::lock_guard<std::mutex> lk(mx);
        if (phase == Phase::idle || ts < startClock) return;
        onsets.push_back(ts);
    }

    // Progress json while running, the result json once (half a beat after the
    // boundary), null otherwise. `now` is the engine sample clock.
    json poll(uint64_t now) {
        std::lock_guard<std::mutex> lk(mx);
        if (phase == Phase::idle) return json();
        if (phase == Phase::ending) {
            const double halfBeat =
                0.5 * static_cast<double>(clicks.back() - clicks[clicks.size() - 2]);
            if (static_cast<double>(now) < static_cast<double>(clicks.back()) + halfBeat)
                return json();
            json r = result(halfBeat);
            phase = Phase::idle;
            return r;
        }
        return status();
    }

private:
    enum class Phase { idle, run, ending };
    std::mutex mx;
    Phase phase = Phase::idle;
    int bars = 8, countIn = 1, beatsPerBar = 4;
    double sr = 48000.0;
    uint64_t startClock = 0;
    std::vector<uint64_t> clicks, onsets;

    json status() const {  // mx held; phase == run
        const int ciBeats = countIn * beatsPerBar;
        const int k = static_cast<int>(clicks.size());  // clicks fired so far
        json s = {{"type", "pickRun"}, {"bars", bars}, {"countIn", countIn}};
        if (k <= ciBeats) {  // k == 0: armed, first click pending — show count-in
            s["phase"] = "countIn";
            s["bar"] = k > 0 ? (k - 1) / beatsPerBar + 1 : 1;
            s["beat"] = k > 0 ? (k - 1) % beatsPerBar + 1 : 0;
        } else {
            s["phase"] = "recording";
            s["bar"] = (k - 1 - ciBeats) / beatsPerBar + 1;
            s["beat"] = (k - 1 - ciBeats) % beatsPerBar + 1;
        }
        return s;
    }

    json result(double halfBeat) const {  // mx held; phase == ending
        const int ciBeats = countIn * beatsPerBar;
        const uint64_t t0 = clicks[ciBeats];       // bar 1 beat 1
        const uint64_t tEnd = clicks.back();       // boundary after the last bar
        const double msPer = 1000.0 / sr;
        auto ms = [&](uint64_t t) {
            return (static_cast<double>(t) - static_cast<double>(t0)) * msPer;
        };
        json cl = json::array(), on = json::array();
        for (size_t i = ciBeats; i < clicks.size(); ++i) cl.push_back(ms(clicks[i]));
        // Half-beat margin either side: an anticipated first note or a just-late
        // last one still belongs to the phrase — the UI attributes onsets to
        // gridpoints and decides.
        const double lo = static_cast<double>(t0) - halfBeat;
        const double hi = static_cast<double>(tEnd) + halfBeat;
        for (uint64_t t : onsets) {
            const double td = static_cast<double>(t);
            if (td >= lo && td <= hi) on.push_back(ms(t));
        }
        return {{"type", "pickRunResult"}, {"bars", bars},          {"countIn", countIn},
                {"beatsPerBar", beatsPerBar}, {"clicks", cl},       {"onsets", on}};
    }
};

// Drain a timestamp ring (audio thread writes value-then-position, release)
// into a callback — same pattern as PickingTracker's private drain, hoisted
// for the run feeder's independent cursors.
template <size_t N, typename F>
inline void drainRing(const std::array<uint64_t, N>& ring, const std::atomic<uint32_t>& pos,
                      uint32_t& rd, F&& fn) {
    const uint32_t wr = pos.load(std::memory_order_acquire);
    if (wr - rd > N) rd = wr - static_cast<uint32_t>(N);  // fell behind: skip to oldest valid
    for (; rd != wr; ++rd) fn(ring[rd & (N - 1)]);
}

}  // namespace webamp
