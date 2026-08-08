// Temporary P4b-2 smoke-test panel: proves the protocol layer against the
// live helper (status, state fields, fast meter stream via ref — no React
// re-renders). Replaced by the real shell in P4b-3.

import { useEffect, useRef } from "react"
import { useEngine } from "@/engine/use-engine"
import { useEngineStore } from "@/engine/store"

export function ConnectionProbe() {
  const engine = useEngine()
  const { status, state, versionMismatch, lastError } = useEngineStore()
  const metersRef = useRef<HTMLSpanElement>(null)

  useEffect(
    () =>
      engine.onMeters((m) => {
        if (metersRef.current)
          metersRef.current.textContent = `in ${m.in.toFixed(3)} · out ${m.out.toFixed(3)} · beat ${m.beatCount}`
      }),
    [engine],
  )

  return (
    <div className="p-8 font-mono text-sm space-y-2">
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
            model: {state.model} · ir: {state.ir} · {state.models.length} models ·{" "}
            {state.irs.length} irs · {state.pedals.length} pedals
          </div>
          <div>
            meters: <span ref={metersRef} />
          </div>
          <button
            className="border px-2 py-1 rounded"
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
