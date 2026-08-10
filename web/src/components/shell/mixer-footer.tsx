// Persistent monitor mix: the levels you balance by ear while playing —
// guitar, metronome, drums — pinned below the tabs so you never switch views
// to adjust them. The guitar is a full channel strip: its enable/mute toggle
// (the engine starts muted for safety) sits next to its fader, mirroring a
// mixer channel. Each channel dims when its source is silent — guitar when
// muted, drums when the machine is stopped.

import { useState } from "react"
import { useEngine } from "@/engine/use-engine"
import { useEngineStore } from "@/engine/store"
import type { ParamId } from "@/engine/protocol"

function Fader({
  label,
  param,
  value,
  min,
  max,
  defaultValue,
  format,
  disabled = false,
  dim = false,
}: {
  label: string
  param: ParamId
  value: number
  min: number
  max: number
  defaultValue: number
  format: (v: number) => string
  /** Disable interaction (source can't be adjusted, e.g. drums stopped). */
  disabled?: boolean
  /** Visually fade but stay interactive (e.g. guitar muted — preset a level). */
  dim?: boolean
}) {
  const engine = useEngine()
  // While dragging, the local value wins over server echoes (same trick as Knob).
  const [drag, setDrag] = useState<number | null>(null)
  const v = drag ?? value

  return (
    <div
      className={`flex flex-1 items-center gap-2 ${disabled || dim ? "opacity-50" : ""}`}
    >
      <span className="w-16 shrink-0 text-[10px] font-semibold tracking-wider text-muted-foreground">
        {label}
      </span>
      <input
        type="range"
        min={min}
        max={max}
        step={(max - min) / 200}
        value={v}
        disabled={disabled}
        onChange={(e) => {
          const n = Number(e.target.value)
          setDrag(n)
          engine.setParam(param, n)
        }}
        onPointerUp={() => setDrag(null)}
        onBlur={() => setDrag(null)}
        onDoubleClick={() => {
          setDrag(null)
          engine.setParam(param, defaultValue)
        }}
        className="h-1.5 w-full min-w-16 flex-1 cursor-pointer accent-primary"
        aria-label={`${label} level`}
        title="Double-click to reset"
      />
      <span className="w-10 shrink-0 text-right text-xs tabular-nums text-muted-foreground">
        {format(v)}
      </span>
    </div>
  )
}

export function MixerFooter() {
  const engine = useEngine()
  const params = useEngineStore((s) => s.state?.params)
  const drumsOn = useEngineStore((s) => s.state?.drums.on ?? false)
  if (!params) return null

  const muted = params.mute
  const num2 = (n: number) => n.toFixed(2)

  return (
    <footer className="sticky bottom-0 z-10 border-t border-border bg-background/95 backdrop-blur">
      <div className="mx-auto flex max-w-4xl flex-col gap-3 px-4 py-2 sm:flex-row sm:items-center sm:gap-6">
        {/* Guitar channel: enable/mute + level. Muted starts every session. */}
        <div className="flex flex-1 items-center gap-2">
          <button
            onClick={() =>
              engine.send({
                type: "setParam",
                id: "mute",
                value: muted ? 0 : 1,
              })
            }
            className={
              "shrink-0 rounded-md border px-2.5 py-1 text-[10px] font-semibold tracking-wide transition-colors " +
              (muted
                ? "border-amber-500/60 bg-amber-500/15 text-amber-500 hover:bg-amber-500/25"
                : "border-emerald-500/60 bg-emerald-500/15 text-emerald-500 hover:bg-emerald-500/25")
            }
            title="Enable or mute the guitar input"
          >
            {muted ? "▶ ENABLE" : "● LIVE"}
          </button>
          <Fader
            label="GUITAR"
            param="gainOut"
            value={params.gainOut}
            min={0}
            max={4}
            defaultValue={1}
            format={num2}
            dim={muted}
          />
        </div>
        <Fader
          label="METRONOME"
          param="metroVol"
          value={params.metroVol}
          min={0}
          max={2}
          defaultValue={0.5}
          format={num2}
        />
        <Fader
          label="DRUMS"
          param="drumVol"
          value={params.drumVol}
          min={0}
          max={2}
          defaultValue={0.6}
          format={num2}
          disabled={!drumsOn}
        />
      </div>
    </footer>
  )
}
