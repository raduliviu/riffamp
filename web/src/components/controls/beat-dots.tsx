// Metronome beat dots. Flashes are driven by the fast meters stream
// (beatCount changes → flash beatInBar) imperatively — no re-renders at 25 Hz.
// Accent-first flashes the primary color, other beats the plain highlight.

import { useEffect, useRef } from "react"
import { useEngine } from "@/engine/use-engine"

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
  const engine = useEngine()
  const boxRef = useRef<HTMLDivElement>(null)
  const lastBeatCount = useRef(-1)

  const opts = useRef({ accentFirst, enabled })
  useEffect(() => {
    opts.current = { accentFirst, enabled }
  })

  useEffect(
    () =>
      engine.onMeters((m) => {
        if (m.beatCount === lastBeatCount.current) return
        const first = lastBeatCount.current < 0
        lastBeatCount.current = m.beatCount
        if (first || !opts.current.enabled || !boxRef.current) return
        const dots = boxRef.current.children
        const dot = dots[m.beatInBar % Math.max(1, dots.length)] as HTMLElement
        if (!dot) return
        const cls =
          opts.current.accentFirst && m.beatInBar === 0 ? "bg-primary" : "bg-foreground"
        dot.classList.add(...cls.split(" "), "scale-125")
        setTimeout(() => dot.classList.remove(...cls.split(" "), "scale-125"), FLASH_MS)
      }),
    [engine],
  )

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
