// Dev playground (open with ?dev): every custom control wired to the live
// engine so feel and fast-path behavior can be verified in isolation before
// the real views use them. Not part of the shipped UI flow.

import { Button } from "@/components/ui/button"
import { BeatDots } from "@/components/controls/beat-dots"
import { DrumGrid } from "@/components/controls/drum-grid"
import { Knob } from "@/components/controls/knob"
import { TunerNeedle } from "@/components/controls/tuner-needle"
import { useEngine } from "@/engine/use-engine"
import { useEngineStore } from "@/engine/store"

export function Playground() {
  const engine = useEngine()
  const state = useEngineStore((s) => s.state)
  if (!state) return <div className="p-8 text-muted-foreground">waiting for engine…</div>
  const p = state.params

  const toggle = (id: "metroOn" | "tunerOn" | "drumOn", on: boolean) =>
    engine.send({ type: "setParam", id, value: on ? 0 : 1 })

  return (
    <div className="mx-auto max-w-3xl space-y-8 p-6">
      <section>
        <h2 className="mb-2 text-xs font-semibold tracking-widest text-muted-foreground">
          KNOBS (live params)
        </h2>
        <div className="flex flex-wrap gap-2">
          <Knob label="GAIN" value={p.gainIn} min={0} max={8} defaultValue={1}
            format={(v) => v.toFixed(2)} onChange={(v) => engine.setParam("gainIn", v)} />
          <Knob label="GATE" value={p.gate} min={-100} max={-20} defaultValue={-100}
            format={(v) => (v <= -99 ? "off" : `${v.toFixed(0)} dB`)}
            onChange={(v) => engine.setParam("gate", v)} />
          <Knob label="BASS" value={p.bass} min={-12} max={12} defaultValue={0}
            format={(v) => (v > 0 ? "+" : "") + v.toFixed(1)}
            onChange={(v) => engine.setParam("bass", v)} />
          <Knob label="VOLUME" value={p.gainOut} min={0} max={4} defaultValue={1}
            format={(v) => v.toFixed(2)} onChange={(v) => engine.setParam("gainOut", v)} />
        </div>
      </section>

      <section>
        <h2 className="mb-2 text-xs font-semibold tracking-widest text-muted-foreground">
          BEAT DOTS · {p.metroBpm.toFixed(0)} BPM
        </h2>
        <div className="flex items-center gap-4">
          <BeatDots beats={p.metroBeats} accentFirst={p.metroAccent} enabled={p.metroOn} />
          <Button size="sm" variant={p.metroOn ? "destructive" : "default"}
            onClick={() => toggle("metroOn", p.metroOn)}>
            {p.metroOn ? "stop" : "start"} metronome
          </Button>
        </div>
      </section>

      <section>
        <h2 className="mb-2 text-xs font-semibold tracking-widest text-muted-foreground">
          TUNER
        </h2>
        <TunerNeedle />
        <Button size="sm" variant={p.tunerOn ? "destructive" : "default"}
          onClick={() => toggle("tunerOn", p.tunerOn)}>
          {p.tunerOn ? "disable" : "enable"} tuner
        </Button>
      </section>

      <section>
        <h2 className="mb-2 text-xs font-semibold tracking-widest text-muted-foreground">
          DRUM GRID
        </h2>
        <DrumGrid drums={state.drums} />
        <Button className="mt-2" size="sm" variant={state.drums.on ? "destructive" : "default"}
          onClick={() => toggle("drumOn", state.drums.on)}>
          {state.drums.on ? "■ stop" : "▶ play"} drums
        </Button>
      </section>
    </div>
  )
}
