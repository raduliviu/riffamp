// Buffer size (latency vs stability). A change needs the DSP buffers
// reallocated, so the engine defers it to restart; show the pending choice.

import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { useEngine } from "@/engine/use-engine"
import type { AudioState } from "@/engine/protocol"

const OPTIONS = [
  { value: 64, label: "64 — lowest latency" },
  { value: 128, label: "128 — recommended" },
  { value: 256, label: "256 — most stable" },
]

export function BufferPicker({ audio }: { audio: AudioState }) {
  const engine = useEngine()
  const shown = audio.pending?.buffer ?? audio.buffer
  return (
    <div>
      <div className="mb-1 text-xs text-muted-foreground">
        Buffer size (latency vs stability)
      </div>
      <Select
        items={Object.fromEntries(
          OPTIONS.map((o) => [String(o.value), o.label])
        )}
        value={String(shown)}
        onValueChange={(v) =>
          engine.send({ type: "setBuffer", value: Number(v) })
        }
      >
        <SelectTrigger className="w-full sm:w-64">
          <SelectValue />
        </SelectTrigger>
        <SelectContent>
          {OPTIONS.map((o) => (
            <SelectItem key={o.value} value={String(o.value)}>
              {o.label}
            </SelectItem>
          ))}
        </SelectContent>
      </Select>
    </div>
  )
}
