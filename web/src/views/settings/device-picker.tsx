// Input/output device + input-channel pickers. Device changes run through the
// ASIO-duplex coercion before sending setAudioDevice.

import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { useEngine } from "@/engine/use-engine"
import { coerceDeviceSelection } from "@/lib/audio-devices"
import type { AudioState, DeviceInfo } from "@/engine/protocol"

function DeviceSelect({
  label,
  devices,
  value,
  onChange,
}: {
  label: string
  devices: DeviceInfo[]
  value: number
  onChange: (index: number) => void
}) {
  // Base UI renders SelectValue from `items` (value → label); without it the
  // trigger would show the raw index instead of the device name.
  const items = Object.fromEntries(
    devices.map((d) => [String(d.index), `${d.name} (${d.api})`])
  )
  return (
    <div>
      <div className="mb-1 text-xs text-muted-foreground">{label}</div>
      <Select
        items={items}
        value={String(value)}
        onValueChange={(v) => onChange(Number(v))}
      >
        <SelectTrigger className="w-full">
          <SelectValue />
        </SelectTrigger>
        <SelectContent>
          {devices.map((d) => (
            <SelectItem key={d.index} value={String(d.index)}>
              {d.name} ({d.api})
            </SelectItem>
          ))}
        </SelectContent>
      </Select>
    </div>
  )
}

export function DevicePicker({ audio }: { audio: AudioState }) {
  const engine = useEngine()

  const change = (side: "in" | "out", index: number) => {
    const { input, output } = coerceDeviceSelection(
      audio,
      side,
      side === "in" ? index : audio.inputDevice,
      side === "out" ? index : audio.outputDevice
    )
    engine.send({ type: "setAudioDevice", input, output })
  }

  return (
    <div className="grid gap-3 sm:grid-cols-2">
      <DeviceSelect
        label="Input device"
        devices={audio.inputDevices}
        value={audio.inputDevice}
        onChange={(i) => change("in", i)}
      />
      <div>
        <div className="mb-1 text-xs text-muted-foreground">
          Input channel (guitar)
        </div>
        <Select
          items={Object.fromEntries(
            Array.from({ length: audio.inChannels }, (_, i) => [
              String(i + 1),
              `IN ${i + 1}`,
            ])
          )}
          value={String(audio.inCh)}
          onValueChange={(v) =>
            engine.send({ type: "setParam", id: "inCh", value: Number(v) })
          }
        >
          <SelectTrigger className="w-full">
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            {Array.from({ length: audio.inChannels }, (_, i) => i + 1).map(
              (c) => (
                <SelectItem key={c} value={String(c)}>
                  IN {c}
                </SelectItem>
              )
            )}
          </SelectContent>
        </Select>
      </div>
      <DeviceSelect
        label="Output device"
        devices={audio.outputDevices}
        value={audio.outputDevice}
        onChange={(i) => change("out", i)}
      />
    </div>
  )
}
