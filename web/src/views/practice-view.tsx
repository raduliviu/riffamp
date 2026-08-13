// Practice: tuner, metronome, drum machine + groove library.

import { Section } from "@/components/shell/section"
import { useEngineStore } from "@/engine/store"
import { DrumSection } from "./practice/drum-section"
import { MetronomeSection } from "./practice/metronome-section"
import { PickingSection } from "./practice/picking-section"
import { TunerSection } from "./practice/tuner-section"

export function PracticeView() {
  const state = useEngineStore((s) => s.state)
  if (!state) return null

  return (
    <div className="space-y-4">
      <Section title="TUNER">
        <TunerSection />
      </Section>
      <Section title="METRONOME">
        <MetronomeSection />
      </Section>
      <Section title="PICKING TRAINER">
        <PickingSection />
      </Section>
      <Section title="DRUM MACHINE">
        <DrumSection drums={state.drums} grooves={state.grooves} />
      </Section>
    </div>
  )
}
