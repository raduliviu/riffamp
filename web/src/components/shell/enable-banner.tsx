// Prominent "input is muted" prompt shown across the top of the app whenever the
// guitar is muted (which it is on every launch, for safety — a high-gain amp
// shouldn't blast on load). Framed as an amp standby switch. It escalates when
// it detects you're actually playing — the exact moment someone wonders why
// there's no sound — so the fix is impossible to miss.

import { useEngine } from "@/engine/use-engine"
import { useEngineStore } from "@/engine/store"
import { useInputSignal } from "@/engine/use-input-signal"

export function EnableBanner() {
  const engine = useEngine()
  const muted = useEngineStore((s) => s.state?.params.mute ?? false)
  const playing = useInputSignal(muted)

  if (!muted) return null

  const enable = () => engine.send({ type: "setParam", id: "mute", value: 0 })

  return (
    <div
      className={
        "border-b border-amber-500/40 bg-amber-500/10 px-4 py-2 " +
        (playing ? "animate-pulse" : "")
      }
    >
      <div className="mx-auto flex max-w-4xl flex-wrap items-center justify-center gap-x-3 gap-y-1 text-center text-sm text-amber-500">
        <span>
          {playing
            ? "🎸 We can hear your guitar — enable input to play through the amp."
            : "🔇 Guitar input is muted for safety. Enable it to start playing."}
        </span>
        <button
          onClick={enable}
          className="shrink-0 rounded-md border border-amber-500/70 bg-amber-500/20 px-3 py-1 text-xs font-bold tracking-wide text-amber-500 hover:bg-amber-500/30"
        >
          ▶ ENABLE INPUT
        </button>
      </div>
    </div>
  )
}
