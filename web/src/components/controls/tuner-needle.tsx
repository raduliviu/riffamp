// Tuner display: note name, ±50-cent needle track, cents readout. Green
// within ±5 cents. Updated imperatively from the fast tuner stream (~12 Hz).

import { useRef } from "react"
import { useTuner } from "@/engine/use-streams"

const TICKS = [-40, -30, -25, -20, -10, 10, 20, 25, 30, 40]

export function TunerNeedle() {
  const noteRef = useRef<HTMLDivElement>(null)
  const centsRef = useRef<HTMLDivElement>(null)
  const needleRef = useRef<HTMLDivElement>(null)

  useTuner((t) => {
    const note = noteRef.current
    const cents = centsRef.current
    const needle = needleRef.current
    if (!note || !cents || !needle) return
    if (t.freq > 0 && t.cents !== undefined) {
      const clamped = Math.max(-50, Math.min(50, t.cents))
      const inTune = Math.abs(t.cents) < 5
      note.textContent = t.note ?? "?"
      note.className = `text-4xl font-bold ${inTune ? "text-emerald-500" : "text-foreground"}`
      needle.style.left = `${50 + clamped}%`
      needle.className = `absolute top-0 h-full w-0.5 -translate-x-1/2 ${
        inTune ? "bg-emerald-500" : "bg-foreground"
      }`
      const rounded = Math.round(t.cents)
      cents.textContent = `${rounded > 0 ? "+" : ""}${rounded} ¢`
      cents.className = `text-sm tabular-nums ${inTune ? "text-emerald-500" : "text-muted-foreground"}`
    } else {
      note.textContent = "—"
      note.className = "text-4xl font-bold text-muted-foreground"
      needle.style.left = "50%"
      needle.className =
        "absolute top-0 h-full w-0.5 -translate-x-1/2 bg-muted-foreground/40"
      cents.textContent = ""
    }
  })

  return (
    <div className="flex flex-col items-center gap-2">
      <div ref={noteRef} className="text-4xl font-bold text-muted-foreground">
        —
      </div>
      <div className="relative h-6 w-full max-w-md rounded bg-muted/40">
        {TICKS.map((c) => (
          <div
            key={c}
            className={`absolute top-1/2 w-px -translate-y-1/2 bg-border ${
              Math.abs(c) === 25 ? "h-4" : "h-2"
            }`}
            style={{ left: `${50 + c}%` }}
          />
        ))}
        <div className="absolute top-0 left-1/2 h-full w-px bg-muted-foreground/60" />
        <div
          ref={needleRef}
          className="absolute top-0 h-full w-0.5 -translate-x-1/2 bg-muted-foreground/40"
          style={{ left: "50%" }}
        />
      </div>
      <div className="flex w-full max-w-md justify-between text-[10px] text-muted-foreground">
        <span>−50</span>
        <span>−25</span>
        <span>0</span>
        <span>+25</span>
        <span>+50</span>
      </div>
      <div
        ref={centsRef}
        className="h-5 text-sm text-muted-foreground tabular-nums"
      />
    </div>
  )
}
