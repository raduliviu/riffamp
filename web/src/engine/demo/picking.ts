// Picking trainer for the demo (P5a/P5b in the browser). Onset detection uses
// spectral flux on the dry input: sum of positive bin-to-bin magnitude
// increases, peak-picked against a rolling median+MAD threshold with a
// tempo-derived refractory gate. It's deliberately simpler than the native
// helper's sample-accurate SuperFlux — good enough to show the feature, and the
// timing jitter of an AnalyserNode poll is part of why the helper plays better.

import type { PickRunMessage } from "../engine"
import type { PickRunResultMessage, PickingMessage } from "../protocol"
import type { DemoMetronome } from "./metronome"

const POLL_MS = 6 // ~160 Hz onset polling
const LIVE_MS = 80 // ~12 Hz live readout
const HISTORY = 100 // flux frames kept for the adaptive threshold
const MAX_BINS = 80 // ~7.5 kHz — guitar fundamentals + lower harmonics

export interface PickingDeps {
  ctx: AudioContext
  flux: AnalyserNode
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
  private prevMag: Float32Array<ArrayBuffer>
  private freqDb: Float32Array<ArrayBuffer>
  private history: number[] = []
  private prevAbove = false
  private lastOnset = -1
  private onsets: number[] = [] // absolute audio times (s)

  private pollTimer: ReturnType<typeof setInterval> | null = null
  private liveTimer: ReturnType<typeof setInterval> | null = null
  private statusTimer: ReturnType<typeof setInterval> | null = null

  private enabled = false
  private run: Run | null = null
  private metroWasRunning = false

  constructor(deps: PickingDeps) {
    this.d = deps
    const bins = deps.flux.frequencyBinCount
    this.prevMag = new Float32Array(new ArrayBuffer(bins * 4))
    this.freqDb = new Float32Array(new ArrayBuffer(bins * 4))
  }

  setEnabled(on: boolean) {
    this.enabled = on
    if (on) {
      this.ensurePolling()
      this.startLive()
    } else {
      this.stopLive()
      if (!this.run) this.stopPolling()
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

    this.run = { bars, countIn, beatsPerBar, recStart, grid }
    this.onsets = []
    this.ensurePolling()
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
    this.stopPolling()
    this.run = null
  }

  // ---- onset detection -------------------------------------------------

  private ensurePolling() {
    if (this.pollTimer) return
    this.history = []
    this.prevAbove = false
    this.prevMag.fill(0)
    this.pollTimer = setInterval(() => this.poll(), POLL_MS)
  }
  private stopPolling() {
    if (this.pollTimer) clearInterval(this.pollTimer)
    this.pollTimer = null
  }

  private poll() {
    const a = this.d.flux
    a.getFloatFrequencyData(this.freqDb)
    const n = Math.min(MAX_BINS, this.freqDb.length)
    let flux = 0
    for (let i = 1; i < n; i++) {
      const mag = this.freqDb[i] <= -160 ? 0 : Math.pow(10, this.freqDb[i] / 20)
      const diff = mag - this.prevMag[i]
      if (diff > 0) flux += diff
      this.prevMag[i] = mag
    }

    const thr = this.threshold()
    const now = this.d.ctx.currentTime
    const above = flux > thr
    if (above && !this.prevAbove && now - this.lastOnset > this.minGap()) {
      this.lastOnset = now
      this.onsets.push(now)
      if (this.onsets.length > 128) this.onsets.shift()
    }
    this.prevAbove = above

    this.history.push(flux)
    if (this.history.length > HISTORY) this.history.shift()
  }

  private threshold(): number {
    if (this.history.length < 8) return 1e-3
    const sorted = [...this.history].sort((a, b) => a - b)
    const median = sorted[sorted.length >> 1]
    const mad =
      sorted
        .map((v) => Math.abs(v - median))
        .sort((a, b) => a - b)[sorted.length >> 1] || 1e-4
    // Sensitivity 0 → conservative (k≈3.5), 1 → hair-trigger (k≈0.5).
    const k = 3.5 - 3 * clamp01(this.d.sens())
    return Math.max(median + k * mad, 5e-4)
  }

  private minGap(): number {
    const { bpm } = this.d.grid()
    const gap = (60 / bpm / this.d.target()) * 0.45
    return Math.max(0.04, gap)
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
    if (now >= end + spb * 0.5) {
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
    if (!this.enabled) this.stopPolling()

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

function clamp01(v: number): number {
  return v < 0 ? 0 : v > 1 ? 1 : v
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
