// Hooks for the engine's fast streams (~25 Hz). The handler is wrapped in
// useEffectEvent, so it always sees fresh props/state while the subscription
// itself is made exactly once — no dependency arrays, no ref-mirroring.
// Handlers update the DOM imperatively; these streams never enter React state.

import { useEffect, useEffectEvent } from "react"
import type { MetersMessage, TunerMessage } from "./protocol"
import { useEngine } from "./use-engine"

export function useMeters(handler: (m: MetersMessage) => void) {
  const engine = useEngine()
  const onMeters = useEffectEvent(handler)
  // The effect event is invoked inside our own closure (not handed to the
  // external system as a value) per the "call, don't pass" guidance.
  useEffect(() => engine.onMeters((m) => onMeters(m)), [engine])
}

export function useTuner(handler: (t: TunerMessage) => void) {
  const engine = useEngine()
  const onTuner = useEffectEvent(handler)
  useEffect(() => engine.onTuner((t) => onTuner(t)), [engine])
}
