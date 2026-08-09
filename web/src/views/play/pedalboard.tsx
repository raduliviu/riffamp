// Pedalboard: pre-amp and post-amp zones. Reorder swaps `order` with the
// adjacent pedal in the same zone (ports the legacy reorderPedal).

import { useEngine } from "@/engine/use-engine"
import { PedalCard } from "./pedal-card"
import type { PedalState } from "@/engine/protocol"

function Zone({ label, pedals }: { label: string; pedals: PedalState[] }) {
  const engine = useEngine()

  const move = (group: PedalState[], index: number, dir: -1 | 1) => {
    const a = group[index]
    const b = group[index + dir]
    if (!b) return
    engine.send({
      type: "setPedal",
      pedal: a.type,
      field: "order",
      value: b.order,
    })
    engine.send({
      type: "setPedal",
      pedal: b.type,
      field: "order",
      value: a.order,
    })
  }

  return (
    <div>
      <div className="mb-1 text-[10px] font-semibold tracking-widest text-muted-foreground">
        {label}
      </div>
      {pedals.length === 0 ? (
        <div className="rounded-lg border border-dashed py-4 text-center text-xs text-muted-foreground">
          no pedals here
        </div>
      ) : (
        <div className="grid grid-cols-1 gap-2 sm:grid-cols-2">
          {pedals.map((p, i) => (
            <PedalCard
              key={p.type}
              pedal={p}
              index={i}
              groupLength={pedals.length}
              onMove={(dir) => move(pedals, i, dir)}
            />
          ))}
        </div>
      )}
    </div>
  )
}

export function Pedalboard({ pedals }: { pedals: PedalState[] }) {
  const byOrder = (a: PedalState, b: PedalState) => a.order - b.order
  const pre = pedals.filter((p) => p.placement === "pre").sort(byOrder)
  const post = pedals.filter((p) => p.placement === "post").sort(byOrder)

  return (
    <div className="space-y-3">
      <Zone label="BEFORE AMP" pedals={pre} />
      <div className="text-center text-xs font-semibold tracking-widest text-muted-foreground">
        ▼ AMP + CAB ▼
      </div>
      <Zone label="AFTER AMP" pedals={post} />
    </div>
  )
}
