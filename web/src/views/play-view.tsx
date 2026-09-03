// Play: the amp itself — presets, amp/cab, tone knobs, pedalboard.

import { DemoLocked } from "@/components/shell/demo-locked"
import { Section } from "@/components/shell/section"
import { useEngineStore } from "@/engine/store"
import { AmpControls } from "./play/amp-controls"
import { AmpPicker } from "./play/amp-picker"
import { Pedalboard } from "./play/pedalboard"
import { Presets } from "./play/presets"

export function PlayView() {
  const state = useEngineStore((s) => s.state)
  const isDemo = useEngineStore((s) => s.kind === "demo")
  if (!state) return null

  return (
    <div className="space-y-4">
      <Section title="PRESETS">
        {isDemo ? (
          <DemoLocked feature="Presets" />
        ) : (
          <Presets presets={state.presets} />
        )}
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
        {isDemo ? (
          <DemoLocked feature="Pedalboard" />
        ) : (
          <Pedalboard pedals={state.pedals} />
        )}
      </Section>
    </div>
  )
}
