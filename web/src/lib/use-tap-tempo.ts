// Tap-tempo: average the last few tap intervals into a BPM. Resets if a gap
// over 2 s suggests a fresh count (ports the legacy tap()).

import { useRef } from "react"

const MAX_TAPS = 5
const RESET_GAP_MS = 2000

export function useTapTempo(onBpm: (bpm: number) => void) {
  const taps = useRef<number[]>([])
  return () => {
    const now = performance.now()
    const t = taps.current
    if (t.length && now - t[t.length - 1] > RESET_GAP_MS) t.length = 0
    t.push(now)
    if (t.length > MAX_TAPS) t.shift()
    if (t.length >= 2) {
      let sum = 0
      for (let i = 1; i < t.length; i++) sum += t[i] - t[i - 1]
      onBpm(60000 / (sum / (t.length - 1)))
    }
  }
}
