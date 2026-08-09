// Settings: audio devices, input channel, buffer size, and engine info.

import { Section } from "@/components/shell/section"
import { useEngineStore } from "@/engine/store"
import { BufferPicker } from "./settings/buffer-picker"
import { DevicePicker } from "./settings/device-picker"

export function SettingsView() {
  const state = useEngineStore((s) => s.state)
  if (!state) return null
  const { audio, engine } = state

  const pending = audio.pending
  const pendingParts: string[] = []
  if (pending?.input) pendingParts.push(`device → ${pending.input}`)
  if (pending?.buffer) pendingParts.push(`buffer → ${pending.buffer} samples`)

  const latency = engine.reportedLatencyMs

  return (
    <div className="space-y-4">
      <Section title="AUDIO DEVICES">
        <DevicePicker audio={audio} />
      </Section>

      <Section title="BUFFER">
        <BufferPicker audio={audio} />
        {pendingParts.length > 0 && (
          <p className="mt-3 rounded-md border border-amber-500/50 bg-amber-500/10 px-3 py-2 text-xs text-amber-500">
            Restart webamp to apply: {pendingParts.join(", ")}.
          </p>
        )}
      </Section>

      <Section title="ENGINE">
        <dl className="grid grid-cols-2 gap-x-4 gap-y-1 text-sm">
          <dt className="text-muted-foreground">API</dt>
          <dd>{engine.api.toUpperCase()}</dd>
          <dt className="text-muted-foreground">Sample rate</dt>
          <dd>{(engine.sampleRate / 1000).toFixed(0)} kHz</dd>
          <dt className="text-muted-foreground">Buffer</dt>
          <dd>{engine.buffer} samples</dd>
          <dt className="text-muted-foreground">Dropouts (xruns)</dt>
          <dd className="tabular-nums">{engine.xruns}</dd>
        </dl>
        {latency > 0 && (
          <p className="mt-3 text-xs text-muted-foreground">
            ≈{latency.toFixed(1)} ms round-trip (driver-reported estimate — a
            true figure needs a physical loopback measurement).
          </p>
        )}
      </Section>
    </div>
  )
}
