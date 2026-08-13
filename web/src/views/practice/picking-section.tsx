// Picking trainer: enable, pick a target subdivision, play against the
// metronome — the readout shows measured notes-per-beat (the "am I doing
// 16ths or triplets?" number), evenness, and the timeline strip.

import { useRef, useState } from "react"
import { Button } from "@/components/ui/button"
import { Knob } from "@/components/controls/knob"
import { PickingTimeline } from "@/components/controls/picking-timeline"
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { useEngine } from "@/engine/use-engine"
import { useEngineStore } from "@/engine/store"
import { usePicking } from "@/engine/use-streams"

const TARGETS = [
  { value: 2, label: "8ths (2/beat)" },
  { value: 3, label: "Triplets (3/beat)" },
  { value: 4, label: "16ths (4/beat)" },
  { value: 6, label: "Sextuplets (6/beat)" },
]
const TARGET_KEY = "webamp:pickTarget"

const npbClass = (color: string) => `text-5xl font-bold tabular-nums ${color}`

export function PickingSection() {
  const engine = useEngine()
  const on = useEngineStore((s) => s.state?.params.pickOn ?? false)
  const sens = useEngineStore((s) => s.state?.params.pickSens ?? 0.5)
  const metroOn = useEngineStore((s) => s.state?.params.metroOn ?? false)
  const [target, setTarget] = useState(
    () => Number(localStorage.getItem(TARGET_KEY)) || 4
  )
  const npbRef = useRef<HTMLDivElement>(null)
  const cvRef = useRef<HTMLDivElement>(null)

  // Readout colors: green on target, amber drifting, red off (e.g. triplets
  // when 16ths were asked for). Imperative — 12 Hz stream.
  usePicking((p) => {
    const npbEl = npbRef.current
    const cvEl = cvRef.current
    if (!npbEl || !cvEl) return
    if (p.npb === null) {
      npbEl.textContent = "—"
      npbEl.className = npbClass("text-muted-foreground")
    } else {
      npbEl.textContent = p.npb.toFixed(1)
      const off = Math.abs(p.npb - target)
      npbEl.className = npbClass(
        off < 0.25
          ? "text-emerald-500"
          : off < 0.6
            ? "text-amber-500"
            : "text-red-500"
      )
    }
    if (p.cv === null) {
      cvEl.textContent = ""
    } else {
      const pct = p.cv * 100
      cvEl.textContent = `evenness ±${pct.toFixed(0)}%`
      cvEl.className =
        "text-xs tabular-nums " +
        (pct < 5
          ? "text-emerald-500"
          : pct < 10
            ? "text-amber-500"
            : "text-red-500")
    }
  })

  const selectTarget = (v: number) => {
    setTarget(v)
    localStorage.setItem(TARGET_KEY, String(v))
  }

  return (
    <div className="space-y-3">
      <div className="flex flex-wrap items-center gap-3">
        <Button
          variant={on ? "destructive" : "default"}
          onClick={() =>
            engine.send({ type: "setParam", id: "pickOn", value: on ? 0 : 1 })
          }
        >
          {on ? "Disable" : "Enable"}
        </Button>
        <Select
          items={Object.fromEntries(
            TARGETS.map((t) => [String(t.value), t.label])
          )}
          value={String(target)}
          onValueChange={(v) => v && selectTarget(Number(v))}
        >
          <SelectTrigger className="w-44">
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            {TARGETS.map((t) => (
              <SelectItem key={t.value} value={String(t.value)}>
                {t.label}
              </SelectItem>
            ))}
          </SelectContent>
        </Select>
        <div className="ml-auto">
          <Knob
            label="SENS"
            value={sens}
            min={0}
            max={1}
            defaultValue={0.5}
            format={(v) => (v * 100).toFixed(0)}
            onChange={(v) => engine.setParam("pickSens", v)}
            size={50}
          />
        </div>
      </div>

      {on && (
        <>
          <div className="flex items-baseline justify-center gap-3">
            <div
              ref={npbRef}
              className="text-5xl font-bold text-muted-foreground tabular-nums"
            >
              —
            </div>
            <div className="text-sm text-muted-foreground">notes / beat</div>
            <div ref={cvRef} className="text-xs tabular-nums" />
          </div>
          <PickingTimeline />
          {!metroOn && (
            <p className="text-xs text-amber-500">
              Start the metronome — the click is the reference grid.
            </p>
          )}
        </>
      )}
    </div>
  )
}
