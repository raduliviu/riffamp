// Bar-by-bar breakdown of a completed pick run: a strip of verdict-colored
// bar cells (the "where did it fall apart" view), click one for its numbers
// and a mini onset-vs-grid strip.

import { useState } from "react"
import type { PickRunResultMessage } from "@/engine/protocol"
import { analyzeRun } from "@/lib/pick-analysis"
import type { BarStat } from "@/lib/pick-analysis"

const VERDICT_BG: Record<string, string> = {
  good: "bg-emerald-500/70 hover:bg-emerald-500",
  ok: "bg-amber-500/70 hover:bg-amber-500",
  bad: "bg-red-500/70 hover:bg-red-500",
}

export function PickRunResults({
  result,
  target,
}: {
  result: PickRunResultMessage
  target: number
}) {
  const [sel, setSel] = useState<number | null>(null) // 1-based bar
  const stats = analyzeRun(result, target)
  const clean = stats.filter((s) => s.verdict === "good").length
  const worst = [...stats].sort(
    (a, b) =>
      Math.abs(b.count - b.expected) - Math.abs(a.count - a.expected) ||
      (b.cv ?? 0) - (a.cv ?? 0)
  )[0]
  const detail = sel !== null ? stats[sel - 1] : null

  return (
    <div className="space-y-2">
      <div className="flex gap-1">
        {stats.map((s) => (
          <button
            key={s.bar}
            onClick={() => setSel(sel === s.bar ? null : s.bar)}
            className={
              `h-8 min-w-8 flex-1 rounded text-[10px] font-semibold text-white transition-colors ${VERDICT_BG[s.verdict]} ` +
              (sel === s.bar ? "ring-2 ring-foreground" : "")
            }
            title={`Bar ${s.bar}: ${s.count}/${s.expected} notes`}
          >
            {s.bar}
          </button>
        ))}
      </div>
      <div className="text-xs text-muted-foreground">
        {clean}/{stats.length} bars clean
        {clean < stats.length && worst ? ` · roughest: bar ${worst.bar}` : ""}
        {" · click a bar for detail"}
      </div>
      {detail && <BarDetail result={result} stat={detail} target={target} />}
    </div>
  )
}

function BarDetail({
  result,
  stat,
  target,
}: {
  result: PickRunResultMessage
  stat: BarStat
  target: number
}) {
  const { clicks, onsets, beatsPerBar } = result
  const start = clicks[(stat.bar - 1) * beatsPerBar]
  const end = clicks[stat.bar * beatsPerBar]
  const span = end - start
  const pct = (t: number) => ((t - start) / span) * 100
  const marks = onsets.filter((t) => t >= start && t < end)

  const tendency =
    stat.meanOffsetMs === null
      ? "no notes"
      : Math.abs(stat.meanOffsetMs) < 3
        ? "on the grid"
        : stat.meanOffsetMs < 0
          ? `rushing ${Math.abs(stat.meanOffsetMs).toFixed(0)} ms`
          : `dragging ${stat.meanOffsetMs.toFixed(0)} ms`

  return (
    <div className="space-y-1 rounded-md border p-2">
      <div className="text-xs">
        <span className="font-semibold">Bar {stat.bar}</span>
        <span className="text-muted-foreground">
          {" "}
          · {stat.count}/{stat.expected} notes
          {stat.cv !== null && ` · evenness ±${(stat.cv * 100).toFixed(0)}%`}
          {` · ${tendency}`}
        </span>
      </div>
      <div className="relative h-8 overflow-hidden rounded bg-muted/30">
        {Array.from({ length: beatsPerBar * target }, (_, k) => (
          <div
            key={k}
            className={
              "absolute top-0 h-full w-px " +
              (k % target === 0 ? "bg-foreground/40" : "bg-foreground/15")
            }
            style={{ left: `${(k / (beatsPerBar * target)) * 100}%` }}
          />
        ))}
        {marks.map((t, i) => (
          <div
            key={i}
            className="absolute top-1 h-6 w-0.5 rounded bg-primary"
            style={{ left: `${pct(t)}%` }}
          />
        ))}
      </div>
    </div>
  )
}
