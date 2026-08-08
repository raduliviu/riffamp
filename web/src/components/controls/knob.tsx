// Rotary knob — faithful port of the legacy makeKnob: 270° sweep from -135°,
// vertical drag (full range per 150 px), wheel = range/100 × 4 per notch,
// double-click resets. While dragging, the local value wins over server
// echoes (the caller keeps sending state; we ignore it until release).

import { useEffect, useRef, useState } from "react"

export interface KnobProps {
  label: string
  value: number
  min: number
  max: number
  defaultValue: number
  format: (v: number) => string
  onChange: (v: number) => void
  size?: number
}

const SWEEP_START = -135 // degrees; sweep is 270° total

function arcPath(r: number, c: number, angle: number): string {
  const a0 = ((SWEEP_START - 90) * Math.PI) / 180
  const a1 = ((angle - 90) * Math.PI) / 180
  const large = angle - SWEEP_START > 180 ? 1 : 0
  return `M ${c + r * Math.cos(a0)} ${c + r * Math.sin(a0)} A ${r} ${r} 0 ${large} 1 ${
    c + r * Math.cos(a1)
  } ${c + r * Math.sin(a1)}`
}

export function Knob({
  label,
  value,
  min,
  max,
  defaultValue,
  format,
  onChange,
  size = 72,
}: KnobProps) {
  const svgRef = useRef<SVGSVGElement>(null)
  const drag = useRef<{ startY: number; startVal: number } | null>(null)
  // Local value while dragging; null = follow the prop (server state).
  const [dragValue, setDragValue] = useState<number | null>(null)

  const v = dragValue ?? value
  const clamp = (x: number) => Math.min(max, Math.max(min, x))
  const c = size / 2
  const r = c - 6
  const angle = SWEEP_START + ((v - min) / (max - min)) * 270

  // Latest props/value for the once-bound wheel listener; mirrored in an
  // effect (mutating refs during render is forbidden by react-hooks rules).
  const liveRef = useRef({ v, min, max, onChange })
  useEffect(() => {
    liveRef.current = { v, min, max, onChange }
  })

  // Wheel must be non-passive to preventDefault (page would scroll).
  useEffect(() => {
    const el = svgRef.current
    if (!el) return
    const onWheel = (e: WheelEvent) => {
      e.preventDefault()
      const live = liveRef.current
      const step = ((live.max - live.min) / 100) * 4
      const next = live.v - Math.sign(e.deltaY) * step
      live.onChange(Math.min(live.max, Math.max(live.min, next)))
    }
    el.addEventListener("wheel", onWheel, { passive: false })
    return () => el.removeEventListener("wheel", onWheel)
  }, [])

  return (
    <div className="flex w-20 flex-col items-center gap-0.5 select-none">
      <svg
        ref={svgRef}
        width={size}
        height={size}
        viewBox={`0 0 ${size} ${size}`}
        className="cursor-ns-resize touch-none"
        onPointerDown={(e) => {
          drag.current = { startY: e.clientY, startVal: v }
          setDragValue(v)
          // Capture can throw for exotic/synthetic pointers; drag still works
          // as long as moves keep hitting the svg.
          try {
            e.currentTarget.setPointerCapture(e.pointerId)
          } catch {
            /* noop */
          }
        }}
        onPointerMove={(e) => {
          if (!drag.current) return
          const next = clamp(
            drag.current.startVal +
              ((drag.current.startY - e.clientY) * (max - min)) / 150,
          )
          setDragValue(next)
          onChange(next)
        }}
        onPointerUp={() => {
          drag.current = null
          setDragValue(null)
        }}
        onPointerCancel={() => {
          drag.current = null
          setDragValue(null)
        }}
        onDoubleClick={() => onChange(defaultValue)}
      >
        <circle cx={c} cy={c} r={r} className="fill-muted/60 stroke-border" strokeWidth="2" />
        <path d={arcPath(r, c, angle)} fill="none" className="stroke-primary" strokeWidth="4" strokeLinecap="round" />
        <line
          x1={c}
          y1={c}
          x2={c}
          y2={6}
          className="stroke-foreground"
          strokeWidth="3"
          strokeLinecap="round"
          transform={`rotate(${angle} ${c} ${c})`}
        />
      </svg>
      <div className="text-[10px] font-semibold tracking-wider text-muted-foreground">{label}</div>
      <div className="text-xs tabular-nums">{format(v)}</div>
    </div>
  )
}
