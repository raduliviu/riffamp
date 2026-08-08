// The deliberate input-enable control. The engine always starts muted
// (safety against feedback/blasts); this banner is the conscious click that
// un-mutes, and the click-to-mute when live. Ports the legacy #inputGate.

import { useEngine } from "@/engine/use-engine"
import { useEngineStore } from "@/engine/store"

export function InputGate() {
  const engine = useEngine()
  const muted = useEngineStore((s) => s.state?.params.mute ?? true)

  return (
    <button
      onClick={() => engine.send({ type: "setParam", id: "mute", value: muted ? 0 : 1 })}
      className={
        "w-full rounded-lg border px-4 py-3 text-sm font-semibold tracking-wide transition-colors " +
        (muted
          ? "border-amber-500/60 bg-amber-500/10 text-amber-500 hover:bg-amber-500/20"
          : "border-emerald-500/60 bg-emerald-500/10 text-emerald-500 hover:bg-emerald-500/20")
      }
      title="Enable or mute the guitar input"
    >
      {muted ? "▶ CLICK TO ENABLE INPUT" : "● INPUT LIVE — click to mute"}
    </button>
  )
}
