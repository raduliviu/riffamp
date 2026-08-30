// Shown instead of the app when the engine isn't connected. Two audiences:
// the native helper (start it, we'll reconnect) and the browser demo (usually a
// blocked mic — offer a retry and the install path).

import { Button } from "@/components/ui/button"
import { useEngine } from "@/engine/use-engine"
import { useEngineStore } from "@/engine/store"

export function OfflineCard() {
  const engine = useEngine()
  const kind = useEngineStore((s) => s.kind)
  const status = useEngineStore((s) => s.status)
  const error = useEngineStore((s) => s.lastError)

  if (kind === "demo") {
    if (status === "connecting") {
      return (
        <div className="mx-auto max-w-md py-16 text-center text-muted-foreground">
          <p className="text-sm">Starting the browser demo…</p>
        </div>
      )
    }
    return (
      <div className="mx-auto max-w-md py-16 text-center">
        <div className="mb-2 text-lg font-semibold text-foreground">
          Can't start the demo
        </div>
        <p className="mb-5 text-sm leading-relaxed text-muted-foreground">
          {error ??
            "The browser demo needs microphone access to hear your guitar."}
        </p>
        <Button onClick={() => engine.start()}>Try again</Button>
        <p className="mt-6 text-xs text-muted-foreground">
          Prefer the real thing?{" "}
          <a className="underline hover:text-foreground" href="/#download">
            Install the helper
          </a>{" "}
          for sub-10&nbsp;ms latency.
        </p>
      </div>
    )
  }

  return (
    <div className="mx-auto max-w-md py-16 text-center text-muted-foreground">
      <div className="mb-2 text-lg font-semibold text-foreground">
        Engine not found
      </div>
      <p className="text-sm leading-relaxed">
        Start the RiffAmp helper on this machine and this page will connect
        automatically.
      </p>
    </div>
  )
}
