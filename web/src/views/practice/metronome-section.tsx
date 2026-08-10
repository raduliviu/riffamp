// Metronome: beat dots, tempo (typed or ±step), tap tempo, start/stop, beats
// per bar, accent toggle, click volume. Keyboard: Space start/stop, ↑/↓ nudge,
// T tap (ignored while typing in a field). Ports the legacy metronome UX.

import { useEffect, useEffectEvent, useState } from "react"
import { Minus, Plus } from "lucide-react"
import { BeatDots } from "@/components/controls/beat-dots"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { useEngine } from "@/engine/use-engine"
import { useEngineStore } from "@/engine/store"
import { useTapTempo } from "@/lib/use-tap-tempo"

const MIN_BPM = 20
const MAX_BPM = 360
const STEP_KEY = "webamp:metroStep"

export function MetronomeSection() {
  const engine = useEngine()
  const p = useEngineStore((s) => s.state?.params)
  const [step, setStep] = useState(
    () => Number(localStorage.getItem(STEP_KEY)) || 5
  )
  const [bpmText, setBpmText] = useState("")
  const [editingBpm, setEditingBpm] = useState(false)

  const bpm = p?.metroBpm ?? 120
  const setBpm = (value: number) => {
    const clamped = Math.min(MAX_BPM, Math.max(MIN_BPM, Math.round(value)))
    engine.setParam("metroBpm", clamped)
  }
  const nudge = (dir: number) => setBpm(bpm + dir * step)
  const tap = useTapTempo(setBpm)

  // Global shortcuts — effect event so the once-bound listener sees fresh state.
  const onKey = useEffectEvent((e: KeyboardEvent) => {
    const el = e.target as HTMLElement | null
    const typing = el?.tagName === "INPUT" || el?.tagName === "SELECT"
    if (!typing && (e.code === "ArrowUp" || e.code === "ArrowDown")) {
      e.preventDefault()
      nudge(e.code === "ArrowUp" ? 1 : -1)
    } else if (typing) {
      return
    } else if (e.code === "Space") {
      e.preventDefault()
      engine.send({
        type: "setParam",
        id: "metroOn",
        value: p?.metroOn ? 0 : 1,
      })
    } else if (e.code === "KeyT") {
      e.preventDefault()
      tap()
    }
  })
  useEffect(() => {
    const handler = (e: KeyboardEvent) => onKey(e)
    window.addEventListener("keydown", handler)
    return () => window.removeEventListener("keydown", handler)
  }, [])

  const changeStep = (raw: string) => {
    const v = Math.min(30, Math.max(1, parseInt(raw, 10) || step))
    setStep(v)
    localStorage.setItem(STEP_KEY, String(v))
  }
  const commitBpm = () => {
    const parsed = parseInt(bpmText, 10)
    if (Number.isFinite(parsed)) setBpm(parsed)
    setEditingBpm(false)
  }

  if (!p) return null

  return (
    <div className="flex flex-wrap items-center gap-6">
      <div className="flex-1 space-y-3 text-center">
        <BeatDots
          beats={p.metroBeats}
          accentFirst={p.metroAccent}
          enabled={p.metroOn}
        />

        <div className="flex items-center justify-center gap-3">
          <Button
            variant="secondary"
            size="icon"
            onClick={() => nudge(-1)}
            aria-label="Decrease tempo"
          >
            <Minus className="size-4" />
          </Button>
          <div className="flex items-baseline gap-1">
            <Input
              className="w-20 text-center text-3xl font-bold tabular-nums"
              inputMode="numeric"
              maxLength={3}
              value={editingBpm ? bpmText : Math.round(bpm)}
              onFocus={() => {
                setEditingBpm(true)
                setBpmText(String(Math.round(bpm)))
              }}
              onChange={(e) =>
                setBpmText(e.target.value.replace(/[^0-9]/g, ""))
              }
              onBlur={commitBpm}
              onKeyDown={(e) => {
                if (e.key === "Enter") {
                  e.preventDefault()
                  commitBpm()
                  e.currentTarget.blur()
                } else if (e.key === "Escape") {
                  setEditingBpm(false)
                  e.currentTarget.blur()
                }
              }}
              aria-label="Tempo in BPM"
            />
            <span className="text-xs text-muted-foreground">BPM</span>
          </div>
          <Button
            variant="secondary"
            size="icon"
            onClick={() => nudge(1)}
            aria-label="Increase tempo"
          >
            <Plus className="size-4" />
          </Button>
        </div>

        <Button
          className="w-40"
          variant={p.metroOn ? "destructive" : "default"}
          onClick={() =>
            engine.send({
              type: "setParam",
              id: "metroOn",
              value: p.metroOn ? 0 : 1,
            })
          }
        >
          {p.metroOn ? "■ Stop" : "▶ Start"}
        </Button>

        <div className="flex flex-wrap items-center justify-center gap-4 text-xs text-muted-foreground">
          <label className="flex items-center gap-1">
            STEP
            <Input
              type="number"
              min={1}
              max={30}
              value={step}
              onChange={(e) => changeStep(e.target.value)}
              className="h-7 w-14"
            />
          </label>
          <label className="flex items-center gap-1">
            BEATS
            <Input
              type="number"
              min={1}
              max={12}
              value={p.metroBeats}
              onChange={(e) =>
                engine.send({
                  type: "setParam",
                  id: "metroBeats",
                  value: Math.min(
                    12,
                    Math.max(1, parseInt(e.target.value, 10) || 4)
                  ),
                })
              }
              className="h-7 w-14"
            />
          </label>
          <Button variant="outline" size="sm" onClick={tap}>
            Tap tempo
          </Button>
          <label className="flex items-center gap-1">
            <input
              type="checkbox"
              checked={p.metroAccent}
              onChange={(e) =>
                engine.send({
                  type: "setParam",
                  id: "metroAccent",
                  value: e.target.checked ? 1 : 0,
                })
              }
            />
            Accent 1st
          </label>
        </div>
        <div className="text-[10px] text-muted-foreground">
          Space start/stop · ↑ faster · ↓ slower · T tap · click the number to
          type
        </div>
      </div>
    </div>
  )
}
