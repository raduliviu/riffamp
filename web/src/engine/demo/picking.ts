// Picking trainer for the demo (P5a/P5b in the browser). Onset DETECTION lives
// in an AudioWorklet (onset-worklet.ts → onset-detector.ts, a port of the
// native helper's flux.h) and arrives here as timestamps on the AudioContext
// clock — the same clock the demo metronome schedules its clicks on, so
// onsets and clicks are directly comparable and sample-accurate. This class
// owns the trainer state: the run (count-in, record, boundary) and the live
// notes-per-beat readout. Scoring itself happens in the UI
// (web/src/lib/pick-analysis.ts), exactly as with the helper.

import type { PickRunMessage } from "../engine"
import type { PickRunResultMessage, PickingMessage } from "../protocol"
import type { DemoMetronome } from "./metronome"

const LIVE_MS = 80 // ~12 Hz live readout / run status
const MAX_ONSETS = 4096 // a 16-bar run at sextuplets is ~1500 notes

export interface PickingDeps {
  ctx: AudioContext
  onsets: AudioWorkletNode // the riffamp-onset worklet on the dry input
  metro: DemoMetronome
  grid: () => { bpm: number; beatsPerBar: number }
  sens: () => number
  target: () => number
  emitPicking: (m: PickingMessage) => void
  emitPickRun: (m: PickRunMessage) => void
  onRunActive: (active: boolean) => void
}

interface Run {
  bars: number
  countIn: number
  beatsPerBar: number
  recStart: number
  grid: number[] // absolute click times
}

export class DemoPicking {
  private d: PickingDeps
  private onsets: number[] = [] // absolute audio times (s), from the worklet

  private liveTimer: ReturnType<typeof setInterval> | null = null
  private statusTimer: ReturnType<typeof setInterval> | null = null

  private enabled = false
  private run: Run | null = null
  private metroWasRunning = false

  constructor(deps: PickingDeps) {
    this.d = deps
    deps.onsets.port.onmessage = (ev: MessageEvent) => {
      const m = ev.data as { type: string; times?: number[] }
      if (m.type !== "onsets" || !m.times) return
      if (!this.enabled && !this.run) return // detector idle: nothing to collect
      for (const t of m.times) this.onsets.push(t)
      if (this.onsets.length > MAX_ONSETS)
        this.onsets.splice(0, this.onsets.length - MAX_ONSETS)
    }
  }

  setEnabled(on: boolean) {
    const wasIdle = !this.enabled && !this.run
    this.enabled = on
    if (on) {
      if (wasIdle) this.arm()
      this.startLive()
    } else {
      this.stopLive()
    }
  }

  get runActive() {
    return this.run !== null
  }

  startRun(bars: number, countIn: number) {
    const { bpm, beatsPerBar } = this.d.grid()
    const spb = 60 / bpm
    this.metroWasRunning = this.d.metro.isRunning
    this.d.metro.setBpm(bpm)
    this.d.metro.setBeats(beatsPerBar)
    this.d.metro.setAccent(true)

    const t0 = this.d.ctx.currentTime + 0.2
    this.d.metro.startAtTime(t0)
    const recStart = t0 + countIn * beatsPerBar * spb
    const points = bars * beatsPerBar + 1
    const grid: number[] = []
    for (let i = 0; i < points; i++) grid.push(recStart + i * spb)

    if (!this.enabled) this.arm()
    this.run = { bars, countIn, beatsPerBar, recStart, grid }
    this.onsets = []
    this.configure()
    this.d.onRunActive(true)
    this.startStatus()
  }

  cancelRun() {
    if (!this.run) return
    this.finishRun(false)
  }

  stop() {
    this.stopLive()
    this.stopStatus()
    this.run = null
    this.enabled = false
  }

  // ---- detector control --------------------------------------------------

  /** Fresh session: clear the detector's history and our collected onsets. */
  private arm() {
    this.onsets = []
    this.d.onsets.port.postMessage({ type: "reset" })
    this.configure()
  }

  /** Push sensitivity + the expected-rate gate to the worklet (idempotent). */
  private configure() {
    const { bpm } = this.d.grid()
    // Run-aware gate, mirroring the helper: 0.8x the target subdivision while
    // a run is graded, 0.6x in free play so off-target subdivisions still read.
    const frac = this.run ? 0.8 : 0.6
    const minGap = Math.max(0.025, (60 / bpm / this.d.target()) * frac)
    this.d.onsets.port.postMessage({ type: "config", sens: this.d.sens(), minGap })
  }

  // ---- live readout ----------------------------------------------------

  private startLive() {
    if (this.liveTimer) return
    this.liveTimer = setInterval(() => this.emitLive(), LIVE_MS)
  }
  private stopLive() {
    if (this.liveTimer) clearInterval(this.liveTimer)
    this.liveTimer = null
  }

  private emitLive() {
    this.configure() // tempo/target/sens may have changed
    const { bpm, beatsPerBar } = this.d.grid()
    const beatMs = 60000 / bpm
    const now = this.d.ctx.currentTime
    const windowSec = (beatsPerBar * 60) / bpm
    const recent = this.onsets.filter((t) => t >= now - windowSec)
    const n = recent.length
    const cv = ioiCv(recent)
    this.d.emitPicking({
      type: "picking",
      n,
      npb: n / beatsPerBar,
      cv,
      beatMs,
      onsets: recent.map((t) => (now - t) * 1000),
      clicks: this.d.metro
        .recentClickTimes(now - windowSec)
        .map((t) => (now - t) * 1000),
    })
  }

  // ---- pick run --------------------------------------------------------

  private startStatus() {
    if (this.statusTimer) return
    this.statusTimer = setInterval(() => this.tickStatus(), LIVE_MS)
  }
  private stopStatus() {
    if (this.statusTimer) clearInterval(this.statusTimer)
    this.statusTimer = null
  }

  private tickStatus() {
    const run = this.run
    if (!run) return
    const now = this.d.ctx.currentTime
    const spb = 60 / this.d.grid().bpm
    const end = run.grid[run.grid.length - 1]
    // Finalize half a beat after the boundary, but never before the
    // detector's emission hold (its refractory gate, ≤150 ms) has passed.
    if (now >= end + Math.max(spb * 0.5, 0.2)) {
      this.finishRun(true)
      return
    }
    if (now < run.recStart) {
      const t0 = run.recStart - run.countIn * run.beatsPerBar * spb
      const beat = now >= t0 ? Math.floor((now - t0) / spb) + 1 : 0
      this.d.emitPickRun({
        type: "pickRun",
        phase: "countIn",
        bar: Math.max(1, Math.floor(Math.max(0, beat - 1) / run.beatsPerBar) + 1),
        beat: beat === 0 ? 0 : ((beat - 1) % run.beatsPerBar) + 1,
        bars: run.bars,
        countIn: run.countIn,
      })
    } else {
      const rb = Math.floor((now - run.recStart) / spb)
      this.d.emitPickRun({
        type: "pickRun",
        phase: "recording",
        bar: Math.min(run.bars, Math.floor(rb / run.beatsPerBar) + 1),
        beat: (rb % run.beatsPerBar) + 1,
        bars: run.bars,
        countIn: run.countIn,
      })
    }
  }

  private finishRun(emitResult: boolean) {
    const run = this.run
    this.run = null
    this.stopStatus()
    this.d.onRunActive(false)
    if (!this.metroWasRunning) this.d.metro.stop()
    if (this.enabled) this.configure() // back to the free-play gate

    if (emitResult && run) {
      const spb = 60 / this.d.grid().bpm
      const end = run.grid[run.grid.length - 1]
      const lo = run.recStart - spb * 0.5
      const hi = end + spb * 0.5
      const onsets = this.onsets
        .filter((t) => t >= lo && t <= hi)
        .map((t) => (t - run.recStart) * 1000)
      const result: PickRunResultMessage = {
        type: "pickRunResult",
        bars: run.bars,
        countIn: run.countIn,
        beatsPerBar: run.beatsPerBar,
        clicks: run.grid.map((t) => (t - run.recStart) * 1000),
        onsets,
      }
      this.d.emitPickRun(result)
    }
  }
}

function ioiCv(times: number[]): number | null {
  if (times.length < 4) return null
  const iois: number[] = []
  for (let i = 1; i < times.length; i++) iois.push(times[i] - times[i - 1])
  const mean = iois.reduce((a, v) => a + v, 0) / iois.length
  if (mean <= 0) return null
  const varc =
    iois.reduce((a, v) => a + (v - mean) * (v - mean), 0) / iois.length
  return Math.sqrt(varc) / mean
}
