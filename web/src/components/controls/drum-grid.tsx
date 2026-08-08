// Step-sequencer grid: voices × steps, click toggles a cell (optimistic —
// the engine echoes state back), playhead column driven imperatively from
// the fast meters stream. Bar/beat boundaries get stronger left borders.

import { useEffect, useRef } from "react"
import { useEngine } from "@/engine/use-engine"
import type { DrumsState } from "@/engine/protocol"

const LABELS = ["KICK", "SNARE", "CRASH", "HIHAT", "RIDE"] // engine voice order
const DISPLAY_ORDER = [2, 4, 3, 1, 0] // top→bottom: crash, ride, hihat, snare, kick

export function DrumGrid({ drums }: { drums: DrumsState }) {
  const engine = useEngine()
  const gridRef = useRef<HTMLDivElement>(null)
  const shownStep = useRef(-1)

  const { stepCount, subdiv, beatsPerBar, pattern } = drums
  const cellsPerBar = beatsPerBar * subdiv
  const voices = pattern.length
  const order =
    DISPLAY_ORDER.filter((v) => v < voices).length === voices
      ? DISPLAY_ORDER
      : Array.from({ length: voices }, (_, i) => i)

  // Playhead: toggle a data attribute per column, styled via CSS below.
  useEffect(
    () =>
      engine.onMeters((m) => {
        if (m.drumStep === shownStep.current || !gridRef.current) return
        shownStep.current = m.drumStep
        for (const cell of gridRef.current.querySelectorAll<HTMLElement>("[data-step]"))
          cell.dataset.playing = String(Number(cell.dataset.step) === m.drumStep)
      }),
    [engine],
  )

  return (
    <div ref={gridRef} className="overflow-x-auto">
      <div className="flex w-max flex-col gap-1">
        {order.map((v) => (
          <div key={v} className="flex items-center gap-2">
            <div className="sticky left-0 w-14 shrink-0 bg-background text-right text-[10px] font-semibold tracking-wider text-muted-foreground">
              {LABELS[v] ?? `V${v}`}
            </div>
            <div className="flex gap-0.5">
              {Array.from({ length: stepCount }, (_, s) => (
                <button
                  key={s}
                  data-step={s}
                  onClick={() =>
                    engine.send({
                      type: "setDrumCell",
                      voice: v,
                      step: s,
                      on: !pattern[v][s],
                    })
                  }
                  className={
                    "size-6 rounded-sm border transition-colors " +
                    (pattern[v][s]
                      ? "border-primary bg-primary/80 "
                      : "border-border bg-muted/40 hover:bg-muted ") +
                    (s > 0 && s % cellsPerBar === 0
                      ? "ml-2 "
                      : s > 0 && s % subdiv === 0
                        ? "ml-1 "
                        : "") +
                    "data-[playing=true]:ring-2 data-[playing=true]:ring-foreground/70"
                  }
                />
              ))}
            </div>
          </div>
        ))}
      </div>
    </div>
  )
}
