// Metronome beat dots. Flashes are driven by the fast meters stream
// (beatCount changes → flash beatInBar) imperatively — no re-renders at 25 Hz.
// The useMeters handler is an effect event, so it reads current props directly.

import { useRef } from "react"
import { useMeters } from "@/engine/use-streams"

const FLASH_MS = 90

export function BeatDots({
  beats,
  accentFirst,
  enabled,
}: {
  beats: number
  accentFirst: boolean
  enabled: boolean
}) {
  const boxRef = useRef<HTMLDivElement>(null)
  const lastBeatCount = useRef(-1)

  useMeters((m) => {
    if (m.beatCount === lastBeatCount.current) return
    const first = lastBeatCount.current < 0
    lastBeatCount.current = m.beatCount
    if (first || !enabled || !boxRef.current) return
    const dots = boxRef.current.children
    const dot = dots[m.beatInBar % Math.max(1, dots.length)] as HTMLElement
    if (!dot) return
    const cls =
      accentFirst && m.beatInBar === 0 ? "bg-primary" : "bg-foreground"
    dot.classList.add(cls, "scale-125")
    setTimeout(() => dot.classList.remove(cls, "scale-125"), FLASH_MS)
  })

  return (
    <div ref={boxRef} className="flex items-center justify-center gap-2">
      {Array.from({ length: beats }, (_, i) => (
        <div
          key={i}
          className="size-3 rounded-full bg-muted transition-transform duration-75"
        />
      ))}
    </div>
  )
}
