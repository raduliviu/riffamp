// Named grooves — a NamedLibrary wired to the groove protocol commands.

import { NamedLibrary } from "@/components/shell/named-library"
import { useEngine } from "@/engine/use-engine"

export function GrooveLibrary({ grooves }: { grooves: string[] }) {
  const engine = useEngine()
  return (
    <NamedLibrary
      names={grooves}
      selectPlaceholder="— saved grooves —"
      namePlaceholder="Name this groove…"
      onLoad={(name) => engine.send({ type: "loadGroove", name })}
      onSave={(name) => engine.send({ type: "saveGroove", name })}
      onDelete={(name) => engine.send({ type: "deleteGroove", name })}
    />
  )
}
