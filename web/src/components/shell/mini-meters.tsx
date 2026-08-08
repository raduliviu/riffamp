// Header in/out meters, fed by the fast meter stream via refs (never React
// state — 25 Hz). Sqrt scaling and the amber-while-muted input bar match the
// legacy UI.

import { useEffect, useRef } from "react"
import { useEngine } from "@/engine/use-engine"
import { useEngineStore } from "@/engine/store"

const pct = (v: number) => `${Math.min(100, Math.sqrt(v) * 110)}%`

export function MiniMeters() {
  const engine = useEngine()
  const muted = useEngineStore((s) => s.state?.params.mute ?? true)
  const inRef = useRef<HTMLDivElement>(null)
  const outRef = useRef<HTMLDivElement>(null)

  useEffect(
    () =>
      engine.onMeters((m) => {
        if (inRef.current) inRef.current.style.width = pct(m.in)
        if (outRef.current) outRef.current.style.width = pct(m.out)
      }),
    [engine],
  )

  return (
    <div className="flex flex-col gap-1" title="input / output level">
      <Bar refEl={inRef} className={muted ? "bg-amber-500" : "bg-emerald-500"} />
      <Bar refEl={outRef} className="bg-emerald-500" />
    </div>
  )
}

function Bar({
  refEl,
  className,
}: {
  refEl: React.RefObject<HTMLDivElement | null>
  className: string
}) {
  return (
    <div className="h-1.5 w-24 overflow-hidden rounded-full bg-muted">
      <div ref={refEl} className={`h-full w-0 rounded-full ${className}`} />
    </div>
  )
}
