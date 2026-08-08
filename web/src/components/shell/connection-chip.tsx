// Status dot + engine summary line: "engine connected · COREAUDIO · 48 kHz ·
// buffer 128 · xruns N" (ports the legacy header).

import { Badge } from "@/components/ui/badge"
import { useEngineStore } from "@/engine/store"

const STATUS_LABEL = {
  connecting: "connecting…",
  connected: "engine connected",
  offline: "engine offline",
} as const

export function ConnectionChip() {
  const status = useEngineStore((s) => s.status)
  const info = useEngineStore((s) => s.state?.engine)
  const versionMismatch = useEngineStore((s) => s.versionMismatch)

  return (
    <div className="flex items-center gap-2 text-xs text-muted-foreground">
      <span
        className={`size-2 rounded-full ${
          status === "connected" ? "bg-emerald-500" : "bg-muted-foreground/40"
        }`}
      />
      <span className={status === "connected" ? "text-foreground" : ""}>
        {STATUS_LABEL[status]}
      </span>
      {status === "connected" && info && (
        <span className="hidden sm:inline">
          {info.api.toUpperCase()} · {(info.sampleRate / 1000).toFixed(0)} kHz ·
          buffer {info.buffer}
          {info.xruns > 0 && ` · xruns ${info.xruns}`}
        </span>
      )}
      {versionMismatch && (
        <Badge variant="destructive">protocol mismatch</Badge>
      )}
    </div>
  )
}
