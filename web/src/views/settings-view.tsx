// Settings: audio devices, input channel, buffer size, and engine info.

import { Section } from "@/components/shell/section"
import { useEngineStore } from "@/engine/store"
import { BufferPicker } from "./settings/buffer-picker"
import { DevicePicker } from "./settings/device-picker"

export function SettingsView() {
  const state = useEngineStore((s) => s.state)
  const isDemo = useEngineStore((s) => s.kind === "demo")
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

      <Section title={isDemo ? "LATENCY" : "BUFFER"}>
        {isDemo ? (
          <div className="space-y-2 text-sm">
            <p className="text-muted-foreground">
              The browser demo runs at a fixed latency the browser controls —
              enough to play, but you'll feel it. Buffer size and true low
              latency (sub-10 ms) need the native app.
            </p>
            <a
              href="https://riffamp.app/#download"
              className="inline-block rounded-md border border-emerald-500/60 bg-emerald-500/15 px-3 py-1.5 text-xs font-semibold text-emerald-500 transition-colors hover:bg-emerald-500/25"
            >
              Install the app for tight, latency-free playing →
            </a>
          </div>
        ) : (
          <>
            <BufferPicker audio={audio} />
            {pendingParts.length > 0 && (
              <p className="mt-3 rounded-md border border-amber-500/50 bg-amber-500/10 px-3 py-2 text-xs text-amber-500">
                Restart RiffAmp to apply: {pendingParts.join(", ")}.
              </p>
            )}
          </>
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
