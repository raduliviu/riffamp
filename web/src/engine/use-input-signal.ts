// True while there's live guitar signal on the input (the user is playing).
// Reads the existing meter stream, which carries the DRY input level even while
// muted — so we can tell "they're playing but hear nothing" and nudge them.
//
// State is set from the meter handler (an event, not the render/effect path):
// a loud sample flips it on and (re)arms an off-timer, so it stays on while
// playing and clears HOLD_MS after the last note. Only tracks while `enabled`.

import { useEffect, useRef, useState } from "react"
import { useMeters } from "./use-streams"

const THRESHOLD = 0.03 // linear input peak that counts as playing (noise floor is far below)
const HOLD_MS = 1500 // stay "active" this long after the last note

export function useInputSignal(enabled: boolean): boolean {
  const [active, setActive] = useState(false)
  const offTimer = useRef<ReturnType<typeof setTimeout> | null>(null)

  useMeters((m) => {
    if (!enabled || m.in <= THRESHOLD) return
    setActive(true) // no-op re-render once already true
    if (offTimer.current) clearTimeout(offTimer.current)
    offTimer.current = setTimeout(() => setActive(false), HOLD_MS)
  })

  useEffect(() => {
    return () => {
      if (offTimer.current) clearTimeout(offTimer.current)
    }
  }, [])

  return active
}
