// Temporary P4b-2 smoke-test panel: proves the protocol layer against the
// live helper (status, state fields, fast meter stream via ref — no React
// re-renders). Replaced by the real shell in P4b-3.

import { useRef } from "react"
import { useEngine } from "@/engine/use-engine"
import { useEngineStore } from "@/engine/store"
import { useMeters } from "@/engine/use-streams"

export function ConnectionProbe() {
  const engine = useEngine()
  const { status, state, versionMismatch, lastError } = useEngineStore()
  const metersRef = useRef<HTMLSpanElement>(null)

  useMeters((m) => {
    if (metersRef.current)
      metersRef.current.textContent = `in ${m.in.toFixed(3)} · out ${m.out.toFixed(3)} · beat ${m.beatCount}`
  })

  return (
    <div className="space-y-2 p-8 font-mono text-sm">
      <div>
        status: <b>{status}</b> ({engine.kind})
      </div>
      {state && (
        <>
          <div>
            helper v{state.version}
            {versionMismatch && " ⚠ protocol mismatch"} · {state.engine.api} ·{" "}
            {state.engine.sampleRate} Hz · buffer {state.engine.buffer}
          </div>
          <div>
            model: {state.model} · ir: {state.ir} · {state.models.length} models
            · {state.irs.length} irs · {state.pedals.length} pedals
          </div>
          <div>
            meters: <span ref={metersRef} />
          </div>
          <button
            className="rounded border px-2 py-1"
            onClick={() => engine.send({ type: "panic" })}
          >
            panic (mute)
          </button>{" "}
          mute: {String(state.params.mute)}
        </>
      )}
      {lastError && <div className="text-red-500">error: {lastError}</div>}
    </div>
  )
}
