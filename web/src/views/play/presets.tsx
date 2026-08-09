// Named rigs: select to load, Update to overwrite the selected rig with the
// current tone, Save-as-new to add one. Selection is local UI state (the
// protocol only tracks the list of names).

import { useState } from "react"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { useEngine } from "@/engine/use-engine"

export function Presets({ presets }: { presets: string[] }) {
  const engine = useEngine()
  const [selected, setSelected] = useState("")
  const [name, setName] = useState("")

  const load = (n: string) => {
    setSelected(n)
    engine.send({ type: "loadPreset", name: n })
  }
  const saveNew = () => {
    const trimmed = name.trim()
    if (!trimmed) return
    engine.send({ type: "savePreset", name: trimmed })
    setSelected(trimmed)
    setName("")
  }

  return (
    <div className="space-y-2">
      <div className="flex gap-2">
        <Select value={selected} onValueChange={load}>
          <SelectTrigger className="flex-1">
            <SelectValue placeholder="— saved rigs —" />
          </SelectTrigger>
          <SelectContent>
            {presets.map((n) => (
              <SelectItem key={n} value={n}>
                {n}
              </SelectItem>
            ))}
          </SelectContent>
        </Select>
        <Button
          variant="outline"
          disabled={!selected}
          onClick={() =>
            selected && engine.send({ type: "savePreset", name: selected })
          }
        >
          Update
        </Button>
        <Button
          variant="destructive"
          disabled={!selected}
          onClick={() => {
            if (!selected) return
            engine.send({ type: "deletePreset", name: selected })
            setSelected("")
          }}
        >
          Delete
        </Button>
      </div>
      <div className="flex gap-2">
        <Input
          value={name}
          maxLength={40}
          placeholder="Name a new rig…"
          onChange={(e) => setName(e.target.value)}
          onKeyDown={(e) => e.key === "Enter" && saveNew()}
        />
        <Button disabled={!name.trim()} onClick={saveNew}>
          Save as new
        </Button>
      </div>
    </div>
  )
}
