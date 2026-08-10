// Drum machine: transport (play/stop, clear), grid shape (time sig, bars,
// resolution), click volume, the step grid, and the groove library. Grid
// changes send the whole setDrumGrid (the engine clears the pattern on shape
// change, per the legacy note).

import { DrumGrid } from "@/components/controls/drum-grid"
import { Button } from "@/components/ui/button"
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { useEngine } from "@/engine/use-engine"
import type { DrumsState } from "@/engine/protocol"
import { GrooveLibrary } from "./groove-library"

const RES_OPTIONS = [
  { value: 1, label: "Quarter" },
  { value: 2, label: "8th" },
  { value: 4, label: "16th" },
  { value: 8, label: "32nd" },
]

function GridSelect({
  label,
  value,
  options,
  onChange,
}: {
  label: string
  value: number
  options: { value: number; label: string }[]
  onChange: (v: number) => void
}) {
  return (
    <label className="flex items-center gap-1 text-xs text-muted-foreground">
      {label}
      <Select value={String(value)} onValueChange={(v) => onChange(Number(v))}>
        <SelectTrigger size="sm" className="w-28">
          <SelectValue />
        </SelectTrigger>
        <SelectContent>
          {options.map((o) => (
            <SelectItem key={o.value} value={String(o.value)}>
              {o.label}
            </SelectItem>
          ))}
        </SelectContent>
      </Select>
    </label>
  )
}

export function DrumSection({
  drums,
  grooves,
}: {
  drums: DrumsState
  grooves: string[]
}) {
  const engine = useEngine()

  const setGrid = (
    patch: Partial<Pick<DrumsState, "beatsPerBar" | "bars" | "subdiv">>
  ) =>
    engine.send({
      type: "setDrumGrid",
      beatsPerBar: patch.beatsPerBar ?? drums.beatsPerBar,
      bars: patch.bars ?? drums.bars,
      subdiv: patch.subdiv ?? drums.subdiv,
    })

  return (
    <div className="space-y-3">
      <div className="flex flex-wrap items-center gap-3">
        <Button
          variant={drums.on ? "destructive" : "default"}
          onClick={() =>
            engine.send({
              type: "setParam",
              id: "drumOn",
              value: drums.on ? 0 : 1,
            })
          }
        >
          {drums.on ? "■ Stop" : "▶ Play"}
        </Button>
        <Button
          variant="outline"
          onClick={() => engine.send({ type: "clearDrums" })}
        >
          Clear
        </Button>
        <GridSelect
          label="TIME"
          value={drums.beatsPerBar}
          options={[
            { value: 4, label: "4/4" },
            { value: 3, label: "3/4" },
          ]}
          onChange={(v) => setGrid({ beatsPerBar: v })}
        />
        <GridSelect
          label="BARS"
          value={drums.bars}
          options={[1, 2, 3, 4].map((n) => ({ value: n, label: String(n) }))}
          onChange={(v) => setGrid({ bars: v })}
        />
        <GridSelect
          label="GRID"
          value={drums.subdiv}
          options={RES_OPTIONS}
          onChange={(v) => setGrid({ subdiv: v })}
        />
      </div>

      <DrumGrid drums={drums} />
      <div className="text-[10px] text-muted-foreground">
        Follows the tempo above · changing time/bars/grid clears the pattern
      </div>

      <GrooveLibrary grooves={grooves} />
    </div>
  )
}
