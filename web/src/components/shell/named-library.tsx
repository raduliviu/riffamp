// A save/load/update/delete library over a list of names — shared by presets
// (rigs) and grooves. Selection is local UI state; the protocol only tracks
// the names. Save-as-new selects the new entry once the list echoes back.

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

export interface NamedLibraryProps {
  names: string[]
  selectPlaceholder: string
  namePlaceholder: string
  onLoad: (name: string) => void
  /** Save (or overwrite) under this name. */
  onSave: (name: string) => void
  onDelete: (name: string) => void
}

export function NamedLibrary({
  names,
  selectPlaceholder,
  namePlaceholder,
  onLoad,
  onSave,
  onDelete,
}: NamedLibraryProps) {
  const [selected, setSelected] = useState("")
  const [name, setName] = useState("")

  const load = (n: string) => {
    setSelected(n)
    onLoad(n)
  }
  const saveNew = () => {
    const trimmed = name.trim()
    if (!trimmed) return
    onSave(trimmed)
    setSelected(trimmed)
    setName("")
  }

  return (
    <div className="space-y-2">
      <div className="flex gap-2">
        <Select value={selected} onValueChange={load}>
          <SelectTrigger className="flex-1">
            <SelectValue placeholder={selectPlaceholder} />
          </SelectTrigger>
          <SelectContent>
            {names.map((n) => (
              <SelectItem key={n} value={n}>
                {n}
              </SelectItem>
            ))}
          </SelectContent>
        </Select>
        <Button
          variant="outline"
          disabled={!selected}
          onClick={() => selected && onSave(selected)}
        >
          Update
        </Button>
        <Button
          variant="destructive"
          disabled={!selected}
          onClick={() => {
            if (!selected) return
            onDelete(selected)
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
          placeholder={namePlaceholder}
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
