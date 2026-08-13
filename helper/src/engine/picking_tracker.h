// Control-thread side of the picking trainer: drains the engine's onset/click
// timestamp rings, keeps a rolling window, and builds the ~12 Hz "picking"
// message — notes-per-beat, evenness (IOI cv), and recent event ages for the
// UI's timeline strip.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "engine.h"
#include "picking.h"

namespace webamp {

struct PickingTracker {
    uint32_t onsetRd = 0, clickRd = 0;  // ring read positions
    std::deque<uint64_t> onsets, clicks;

    void reset(Engine& e) {
        onsetRd = e.onsetPos.load(std::memory_order_acquire);
        clickRd = e.clickPos.load(std::memory_order_acquire);
        onsets.clear();
        clicks.clear();
    }

    // Drain new events and drop everything older than the window.
    void update(Engine& e, uint64_t now, uint64_t windowSamples) {
        drain(e.onsetTs, e.onsetPos, onsetRd, onsets);
        drain(e.clickTs, e.clickPos, clickRd, clicks);
        const uint64_t cutoff = now > windowSamples ? now - windowSamples : 0;
        while (!onsets.empty() && onsets.front() < cutoff) onsets.pop_front();
        while (!clicks.empty() && clicks.front() < cutoff) clicks.pop_front();
    }

    // Stats over the last bar only (the readout should react bar-by-bar).
    json message(uint64_t now, double sr, double bpm, int beatsPerBar) const {
        const double beatSamples = sr * 60.0 / std::max(20.0, bpm);
        const uint64_t barWindow = static_cast<uint64_t>(beatSamples * beatsPerBar);
        const uint64_t cutoff = now > barWindow ? now - barWindow : 0;

        std::vector<uint64_t> recent;
        for (uint64_t t : onsets)
            if (t >= cutoff) recent.push_back(t);

        const double med = medianIoi(recent);
        const double npb = notesPerBeat(beatSamples, med);
        const double cv = ioiCv(recent);

        auto ages = [&](const std::deque<uint64_t>& ts) {
            json arr = json::array();
            for (uint64_t t : ts)
                arr.push_back(static_cast<int>((now - t) * 1000.0 / sr));  // ms ago
            return arr;
        };
        return {{"type", "picking"},
                {"n", recent.size()},
                {"npb", npb > 0 ? json(npb) : json(nullptr)},
                {"cv", cv >= 0 ? json(cv) : json(nullptr)},
                {"beatMs", 60000.0 / std::max(20.0, bpm)},
                {"onsets", ages(onsets)},
                {"clicks", ages(clicks)}};
    }

private:
    template <size_t N>
    static void drain(const std::array<uint64_t, N>& ring, const std::atomic<uint32_t>& pos,
                      uint32_t& rd, std::deque<uint64_t>& out) {
        const uint32_t wr = pos.load(std::memory_order_acquire);
        // If we fell more than a ring behind, skip to the oldest still valid.
        if (wr - rd > N) rd = wr - static_cast<uint32_t>(N);
        for (; rd != wr; ++rd) out.push_back(ring[rd & (N - 1)]);
        while (out.size() > 2 * N) out.pop_front();
    }
};

}  // namespace webamp
