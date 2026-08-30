// Shown when a hosted origin connects to a helper that doesn't trust it yet.
// The helper prints a 6-digit code on the local machine; enter it once and the
// helper remembers this origin. Wrong codes report the attempts remaining.

import { useState } from "react"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { useEngine } from "@/engine/use-engine"
import { useEngineStore } from "@/engine/store"

export function PairingCard() {
  const engine = useEngine()
  const attemptsLeft = useEngineStore((s) => s.pairing.attemptsLeft)
  const [code, setCode] = useState("")

  const submit = () => {
    if (code.length === 6) engine.pair(code)
  }

  return (
    <div className="mx-auto max-w-sm py-16 text-center">
      <div className="mb-2 text-lg font-semibold text-foreground">
        Pair with your helper
      </div>
      <p className="mb-5 text-sm leading-relaxed text-muted-foreground">
        This page wants to control the RiffAmp helper on your machine. Enter the
        6-digit code the helper printed on startup to allow it.
      </p>
      <div className="flex items-center justify-center gap-2">
        <Input
          className="w-32 text-center text-lg tracking-[0.3em] tabular-nums"
          inputMode="numeric"
          maxLength={6}
          placeholder="000000"
          value={code}
          onChange={(e) => setCode(e.target.value.replace(/[^0-9]/g, ""))}
          onKeyDown={(e) => e.key === "Enter" && submit()}
          aria-label="Pairing code"
        />
        <Button onClick={submit} disabled={code.length !== 6}>
          Pair
        </Button>
      </div>
      {attemptsLeft !== null && (
        <p className="mt-3 text-sm text-red-500">
          Wrong code — {attemptsLeft}{" "}
          {attemptsLeft === 1 ? "attempt" : "attempts"} left.
        </p>
      )}
    </div>
  )
}
