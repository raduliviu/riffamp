// Scrolling timeline strip for the picking trainer: metronome clicks as grid
// lines, detected pick attacks as ticks. Now = right edge; two bars visible.
// Painted imperatively on each ~12 Hz picking message — never React state.

import { useRef } from "react"
import { usePicking } from "@/engine/use-streams"

const BEATS_SHOWN = 8

export function PickingTimeline() {
  const canvasRef = useRef<HTMLCanvasElement>(null)

  usePicking((p) => {
    const canvas = canvasRef.current
    if (!canvas) return
    const dpr = window.devicePixelRatio || 1
    const w = canvas.clientWidth
    const h = canvas.clientHeight
    if (canvas.width !== w * dpr || canvas.height !== h * dpr) {
      canvas.width = w * dpr
      canvas.height = h * dpr
    }
    const ctx = canvas.getContext("2d")
    if (!ctx) return
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
    ctx.clearRect(0, 0, w, h)

    const style = getComputedStyle(canvas)
    const gridColor = style.getPropertyValue("--border") || "#444"
    const onsetColor = style.getPropertyValue("--primary") || "#6366f1"

    const windowMs = p.beatMs * BEATS_SHOWN
    const x = (ageMs: number) => w - (ageMs / windowMs) * w

    ctx.strokeStyle = gridColor
    ctx.lineWidth = 1
    for (const age of p.clicks) {
      const px = x(age)
      if (px < 0) continue
      ctx.beginPath()
      ctx.moveTo(px, 0)
      ctx.lineTo(px, h)
      ctx.stroke()
    }

    ctx.strokeStyle = onsetColor
    ctx.lineWidth = 2
    for (const age of p.onsets) {
      const px = x(age)
      if (px < 0) continue
      ctx.beginPath()
      ctx.moveTo(px, h * 0.2)
      ctx.lineTo(px, h * 0.8)
      ctx.stroke()
    }
  })

  return (
    <canvas
      ref={canvasRef}
      className="h-12 w-full rounded-md border bg-muted/20"
      aria-label="Picking timeline: clicks as grid lines, picks as ticks"
    />
  )
}
