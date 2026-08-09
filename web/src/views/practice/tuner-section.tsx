// Tuner: enable toggle + the needle display. When off, the needle idles.

import { Button } from "@/components/ui/button"
import { TunerNeedle } from "@/components/controls/tuner-needle"
import { useEngine } from "@/engine/use-engine"
import { useEngineStore } from "@/engine/store"

export function TunerSection() {
  const engine = useEngine()
  const on = useEngineStore((s) => s.state?.params.tunerOn ?? false)
  return (
    <div className="flex flex-wrap items-center gap-6">
      <Button
        variant={on ? "destructive" : "default"}
        onClick={() =>
          engine.send({ type: "setParam", id: "tunerOn", value: on ? 0 : 1 })
        }
      >
        {on ? "Disable" : "Enable"}
      </Button>
      <div className="min-w-56 flex-1">
        <TunerNeedle />
      </div>
    </div>
  )
}
