// Named rigs — a NamedLibrary wired to the preset protocol commands.

import { NamedLibrary } from "@/components/shell/named-library"
import { useEngine } from "@/engine/use-engine"

export function Presets({ presets }: { presets: string[] }) {
  const engine = useEngine()
  return (
    <NamedLibrary
      names={presets}
      selectPlaceholder="— saved rigs —"
      namePlaceholder="Name a new rig…"
      onLoad={(name) => engine.send({ type: "loadPreset", name })}
      onSave={(name) => engine.send({ type: "savePreset", name })}
      onDelete={(name) => engine.send({ type: "deletePreset", name })}
    />
  )
}
