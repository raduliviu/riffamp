// Amp model + cabinet pickers: a select flanked by prev/next steppers that
// wrap around the list (ports the legacy stepper UX). Server owns the choice;
// we just send setModel / setIr.

import { ChevronLeft, ChevronRight } from "lucide-react"
import { Button } from "@/components/ui/button"
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { useEngine } from "@/engine/use-engine"

interface StepperSelectProps {
  label: string
  value: string
  options: string[]
  onSelect: (name: string) => void
}

function StepperSelect({
  label,
  value,
  options,
  onSelect,
}: StepperSelectProps) {
  const step = (dir: number) => {
    if (!options.length) return
    const cur = Math.max(0, options.indexOf(value))
    onSelect(options[(cur + dir + options.length) % options.length])
  }
  return (
    <div>
      <div className="mb-1 text-xs font-semibold tracking-wider text-muted-foreground">
        {label}
      </div>
      <div className="flex items-center gap-1">
        <Button
          variant="outline"
          size="icon"
          onClick={() => step(-1)}
          aria-label={`Previous ${label}`}
        >
          <ChevronLeft className="size-4" />
        </Button>
        <Select value={value} onValueChange={onSelect}>
          <SelectTrigger className="flex-1">
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            {options.map((o) => (
              <SelectItem key={o} value={o}>
                {o}
              </SelectItem>
            ))}
          </SelectContent>
        </Select>
        <Button
          variant="outline"
          size="icon"
          onClick={() => step(1)}
          aria-label={`Next ${label}`}
        >
          <ChevronRight className="size-4" />
        </Button>
      </div>
    </div>
  )
}

export function AmpPicker({
  model,
  ir,
  models,
  irs,
}: {
  model: string
  ir: string
  models: string[]
  irs: string[]
}) {
  const engine = useEngine()
  return (
    <div className="space-y-3">
      <StepperSelect
        label="AMP MODEL"
        value={model}
        options={models}
        onSelect={(name) => engine.send({ type: "setModel", name })}
      />
      <StepperSelect
        label="CABINET"
        value={ir}
        options={irs}
        onSelect={(name) => engine.send({ type: "setIr", name })}
      />
    </div>
  )
}
