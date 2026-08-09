// The six amp knobs (gain, gate, bass, mid, treble, volume) driven from the
// spec table. Values come from engine state; changes go out throttled.

import { Knob } from "@/components/controls/knob"
import { useEngine } from "@/engine/use-engine"
import { AMP_KNOBS } from "@/lib/rig-specs"
import type { AmpParams } from "@/engine/protocol"

export function AmpControls({ params }: { params: AmpParams }) {
  const engine = useEngine()
  return (
    <div className="flex flex-wrap justify-center gap-2">
      {AMP_KNOBS.map((k) => (
        <Knob
          key={k.id}
          label={k.label}
          value={params[k.id]}
          min={k.min}
          max={k.max}
          defaultValue={k.defaultValue}
          format={k.format}
          onChange={(v) => engine.setParam(k.id, v)}
        />
      ))}
    </div>
  )
}
