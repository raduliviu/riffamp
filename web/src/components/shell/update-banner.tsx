// "A new version is available" bar (P6a, phase 1: notify only). Shown when the
// helper's startup update check found a newer GitHub release. The button opens
// the release page — phase 1 downloads and installs nothing. Dismissible for
// the session; it comes back on next launch if still out of date.

import { useState } from "react"
import { useEngineStore } from "@/engine/store"

export function UpdateBanner() {
  const update = useEngineStore((s) => s.state?.update)
  const [dismissed, setDismissed] = useState(false)

  if (!update || dismissed) return null

  return (
    <div className="border-b border-indigo-500/40 bg-indigo-500/10 px-4 py-2">
      <div className="mx-auto flex max-w-4xl flex-wrap items-center justify-center gap-x-3 gap-y-1 text-center text-sm text-indigo-300">
        <span>
          ✨ RiffAmp <span className="font-semibold">{update.version}</span> is
          available.
        </span>
        <a
          href={update.url}
          target="_blank"
          rel="noreferrer"
          className="shrink-0 rounded-md border border-indigo-400/70 bg-indigo-500/20 px-3 py-1 text-xs font-bold tracking-wide text-indigo-200 hover:bg-indigo-500/30"
        >
          What's new & download →
        </a>
        <button
          onClick={() => setDismissed(true)}
          className="shrink-0 rounded-md px-2 py-1 text-xs text-indigo-300/70 hover:text-indigo-200"
          aria-label="Dismiss"
        >
          Later
        </button>
      </div>
    </div>
  )
}
