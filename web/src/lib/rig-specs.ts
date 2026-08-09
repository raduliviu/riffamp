// Static UI specs for the amp control knobs and the pedal roster — ranges,
// labels, and value formatting, ported from the legacy PARAMS / PEDAL_SPECS.
// Pure data so views and the playground share one source.

import type { ParamId, PedalType } from "@/engine/protocol"

export interface KnobSpec {
  id: Extract<
    ParamId,
    "gainIn" | "gate" | "bass" | "mid" | "treble" | "gainOut"
  >
  label: string
  min: number
  max: number
  defaultValue: number
  format: (v: number) => string
}

const signed = (v: number) => (v > 0 ? "+" : "") + v.toFixed(1)

// Order matches the legacy Controls row.
export const AMP_KNOBS: KnobSpec[] = [
  {
    id: "gainIn",
    label: "GAIN",
    min: 0,
    max: 8,
    defaultValue: 1,
    format: (v) => v.toFixed(2),
  },
  {
    id: "gate",
    label: "GATE",
    min: -100,
    max: -20,
    defaultValue: -100,
    format: (v) => (v <= -99 ? "off" : `${v.toFixed(0)} dB`),
  },
  {
    id: "bass",
    label: "BASS",
    min: -12,
    max: 12,
    defaultValue: 0,
    format: signed,
  },
  {
    id: "mid",
    label: "MID",
    min: -12,
    max: 12,
    defaultValue: 0,
    format: signed,
  },
  {
    id: "treble",
    label: "TREBLE",
    min: -12,
    max: 12,
    defaultValue: 0,
    format: signed,
  },
  {
    id: "gainOut",
    label: "VOLUME",
    min: 0,
    max: 4,
    defaultValue: 1,
    format: (v) => v.toFixed(2),
  },
]

export interface PedalParamSpec {
  id: string
  label: string
  min: number
  max: number
  format: (v: number) => string
}

export interface PedalSpec {
  type: PedalType
  label: string
  params: PedalParamSpec[]
}

const pct = (v: number) => (v * 100).toFixed(0)

export const PEDAL_SPECS: PedalSpec[] = [
  {
    type: "comp",
    label: "COMP",
    params: [
      {
        id: "threshold",
        label: "THRESH",
        min: -60,
        max: 0,
        format: (v) => `${v.toFixed(0)}dB`,
      },
      {
        id: "ratio",
        label: "RATIO",
        min: 1,
        max: 20,
        format: (v) => `${v.toFixed(1)}:1`,
      },
      {
        id: "makeup",
        label: "GAIN",
        min: 0,
        max: 24,
        format: (v) => `+${v.toFixed(0)}`,
      },
    ],
  },
  {
    type: "drive",
    label: "DRIVE",
    params: [
      { id: "drive", label: "DRIVE", min: 0, max: 1, format: pct },
      { id: "tone", label: "TONE", min: 0, max: 1, format: pct },
      { id: "level", label: "LEVEL", min: 0, max: 1, format: pct },
    ],
  },
  {
    type: "chorus",
    label: "CHORUS",
    params: [
      { id: "rate", label: "RATE", min: 0, max: 1, format: pct },
      { id: "depth", label: "DEPTH", min: 0, max: 1, format: pct },
      { id: "mix", label: "MIX", min: 0, max: 1, format: pct },
    ],
  },
  {
    type: "delay",
    label: "DELAY",
    params: [
      {
        id: "time",
        label: "TIME",
        min: 20,
        max: 2000,
        format: (v) => `${v.toFixed(0)}ms`,
      },
      { id: "feedback", label: "FBK", min: 0, max: 0.95, format: pct },
      { id: "mix", label: "MIX", min: 0, max: 1, format: pct },
    ],
  },
  {
    type: "reverb",
    label: "REVERB",
    params: [
      { id: "size", label: "SIZE", min: 0, max: 1, format: pct },
      { id: "damp", label: "DAMP", min: 0, max: 1, format: pct },
      { id: "mix", label: "MIX", min: 0, max: 1, format: pct },
    ],
  },
]

export const PEDAL_SPEC_BY_TYPE: Record<PedalType, PedalSpec> =
  Object.fromEntries(PEDAL_SPECS.map((s) => [s.type, s])) as Record<
    PedalType,
    PedalSpec
  >
