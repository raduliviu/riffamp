// Per-bar scoring for a completed pick run (P5b). The engine ships raw
// timestamps — metronome clicks (the grid) and detected onsets — and all
// judgment happens here, so scoring can iterate without touching C++.
//
// Method: interpolate `target` subdivision gridpoints per beat between the
// actual click times, attribute each onset to its nearest gridpoint, then per
// bar report count vs expected, evenness (IOI cv), and the signed mean offset
// from the grid (negative = rushing, positive = dragging).

import type { PickRunResultMessage } from "@/engine/protocol"

export type Verdict = "good" | "ok" | "bad"

export interface BarStat {
  bar: number // 1-based
  expected: number
  count: number
  cv: number | null // null when too few notes to judge evenness
  meanOffsetMs: number | null // null when the bar has no notes
  verdict: Verdict
}

export function analyzeRun(
  r: PickRunResultMessage,
  target: number
): BarStat[] {
  const { clicks, onsets, bars, beatsPerBar } = r
  const perBar = beatsPerBar * target

  // Subdivision grid from the actual click times (immune to bpm rounding).
  const grid: number[] = []
  for (let b = 0; b + 1 < clicks.length; b++)
    for (let k = 0; k < target; k++)
      grid.push(clicks[b] + ((clicks[b + 1] - clicks[b]) * k) / target)
  grid.push(clicks[clicks.length - 1]) // end boundary, catches a late last note

  const counts = new Array<number>(bars).fill(0)
  const offsets: number[][] = Array.from({ length: bars }, () => [])
  const times: number[][] = Array.from({ length: bars }, () => [])

  for (const t of onsets) {
    // Nearest gridpoint (grid is sorted): binary search for first >= t.
    let lo = 0
    let hi = grid.length - 1
    while (lo < hi) {
      const mid = (lo + hi) >> 1
      if (grid[mid] < t) lo = mid + 1
      else hi = mid
    }
    const i = lo > 0 && t - grid[lo - 1] < grid[lo] - t ? lo - 1 : lo
    const bar = Math.min(bars - 1, Math.floor(i / perBar))
    counts[bar]++
    offsets[bar].push(t - grid[i])
    times[bar].push(t)
  }

  const beatMs =
    clicks.length > 1 ? (clicks[clicks.length - 1] - clicks[0]) / (clicks.length - 1) : 500
  const subdivMs = beatMs / target

  return counts.map((count, b) => {
    const offs = offsets[b]
    const meanOffsetMs = offs.length
      ? offs.reduce((a, v) => a + v, 0) / offs.length
      : null
    const cv = ioiCv(times[b])
    const expected = perBar

    const bad =
      Math.abs(count - expected) >= Math.max(2, expected * 0.25) ||
      (cv !== null && cv > 0.15)
    const ok =
      count !== expected ||
      (cv !== null && cv > 0.07) ||
      (meanOffsetMs !== null && Math.abs(meanOffsetMs) > subdivMs * 0.2)
    return {
      bar: b + 1,
      expected,
      count,
      cv,
      meanOffsetMs,
      verdict: bad ? "bad" : ok ? "ok" : ("good" as Verdict),
    }
  })
}

/** Coefficient of variation of inter-onset intervals; null if under 4 notes. */
function ioiCv(ts: number[]): number | null {
  if (ts.length < 4) return null
  const iois: number[] = []
  for (let i = 1; i < ts.length; i++) iois.push(ts[i] - ts[i - 1])
  const mean = iois.reduce((a, v) => a + v, 0) / iois.length
  if (mean <= 0) return null
  const varc = iois.reduce((a, v) => a + (v - mean) * (v - mean), 0) / iois.length
  return Math.sqrt(varc) / mean
}
