// Play: the amp itself — presets, amp/cab, tone knobs, pedalboard.

import { Section } from "@/components/shell/section"
import { useEngineStore } from "@/engine/store"
import { AmpControls } from "./play/amp-controls"
import { AmpPicker } from "./play/amp-picker"
import { Pedalboard } from "./play/pedalboard"
import { Presets } from "./play/presets"

export function PlayView() {
  const state = useEngineStore((s) => s.state)
  if (!state) return null

  return (
    <div className="space-y-4">
      <Section title="PRESETS">
        <Presets presets={state.presets} />
      </Section>
      <Section title="AMP + CABINET">
        <AmpPicker
          model={state.model}
          ir={state.ir}
          models={state.models}
          irs={state.irs}
        />
      </Section>
      <Section title="CONTROLS">
        <AmpControls params={state.params} />
      </Section>
      <Section title="PEDALBOARD">
        <Pedalboard pedals={state.pedals} />
      </Section>
    </div>
  )
}
