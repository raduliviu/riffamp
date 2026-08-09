// One pedal: on/off LED, name, chain-move controls, and its param knobs.
// Param knobs are throttled; on/off, reorder and cross-amp moves send now.

import { ArrowDown, ArrowUp, ArrowUpDown } from "lucide-react"
import { Knob } from "@/components/controls/knob"
import { Button } from "@/components/ui/button"
import { useEngine } from "@/engine/use-engine"
import { PEDAL_SPEC_BY_TYPE } from "@/lib/rig-specs"
import type { PedalState } from "@/engine/protocol"

export function PedalCard({
  pedal,
  index,
  groupLength,
  onMove,
}: {
  pedal: PedalState
  index: number
  groupLength: number
  onMove: (dir: -1 | 1) => void
}) {
  const engine = useEngine()
  const spec = PEDAL_SPEC_BY_TYPE[pedal.type]

  const setField = (field: string, value: number) =>
    engine.send({ type: "setPedal", pedal: pedal.type, field, value })

  return (
    <div
      className={
        "rounded-lg border p-3 transition-colors " +
        (pedal.enabled
          ? "border-primary/50 bg-primary/5"
          : "border-border bg-muted/20")
      }
    >
      <div className="mb-2 flex items-center gap-2">
        <button
          onClick={() => setField("enabled", pedal.enabled ? 0 : 1)}
          className={
            "size-3 rounded-full border transition-colors " +
            (pedal.enabled
              ? "border-primary bg-primary"
              : "border-muted-foreground/50 bg-transparent")
          }
          title="On / off"
          aria-label={`${spec.label} on/off`}
        />
        <span className="flex-1 text-xs font-semibold tracking-wider">
          {spec.label}
        </span>
        <div className="flex gap-0.5">
          <Button
            variant="ghost"
            size="icon"
            className="size-6"
            disabled={index === 0}
            onClick={() => onMove(-1)}
            title="Earlier in chain"
          >
            <ArrowUp className="size-3" />
          </Button>
          <Button
            variant="ghost"
            size="icon"
            className="size-6"
            disabled={index === groupLength - 1}
            onClick={() => onMove(1)}
            title="Later in chain"
          >
            <ArrowDown className="size-3" />
          </Button>
          <Button
            variant="ghost"
            size="icon"
            className="size-6"
            onClick={() =>
              setField("placement", pedal.placement === "pre" ? 1 : 0)
            }
            title={
              pedal.placement === "pre" ? "Move after amp" : "Move before amp"
            }
          >
            <ArrowUpDown className="size-3" />
          </Button>
        </div>
      </div>
      <div className="flex justify-center gap-1">
        {spec.params.map((ps) => (
          <Knob
            key={ps.id}
            label={ps.label}
            value={pedal.params[ps.id]}
            min={ps.min}
            max={ps.max}
            format={ps.format}
            onChange={(v) => engine.setPedalParam(pedal.type, ps.id, v)}
            size={50}
          />
        ))}
      </div>
    </div>
  )
}
