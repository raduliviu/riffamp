// A sample-accurate Web Audio metronome (the classic lookahead scheduler:
// a coarse setInterval that schedules the next clicks slightly ahead on the
// audio clock). Beats are logged with their audio-time so the meter loop can
// report the beat that is actually sounding now, not the one just scheduled.

const LOOKAHEAD_S = 0.12 // schedule this far ahead of the audio clock
const TICK_MS = 25 // how often the scheduler wakes
const CLICK_S = 0.03 // click envelope length

interface Beat {
  count: number // total beats since start
  inBar: number // 0-based position within the bar
  time: number // AudioContext time it sounds at
}

export class DemoMetronome {
  private gain: GainNode
  private running = false
  private bpm = 120
  private beats = 4
  private accent = true
  private nextTime = 0
  private count = 0
  private timer: ReturnType<typeof setInterval> | null = null
  private log: Beat[] = []
  private ctx: AudioContext

  constructor(ctx: AudioContext) {
    this.ctx = ctx
    this.gain = ctx.createGain()
    this.gain.gain.value = 0.5
    this.gain.connect(ctx.destination)
  }

  setBpm(v: number) {
    this.bpm = v
  }
  setBeats(v: number) {
    this.beats = Math.max(1, v)
  }
  setAccent(on: boolean) {
    this.accent = on
  }
  setVol(v: number) {
    this.gain.gain.setTargetAtTime(v, this.ctx.currentTime, 0.01)
  }

  start() {
    this.startAtTime(this.ctx.currentTime + 0.06)
  }

  /** Start with beat 0 landing exactly at audio-time `t0` (for pick runs). */
  startAtTime(t0: number) {
    if (this.timer) clearInterval(this.timer)
    this.running = true
    this.count = 0
    this.log = []
    this.nextTime = t0
    this.timer = setInterval(() => this.schedule(), TICK_MS)
  }

  stop() {
    this.running = false
    if (this.timer) clearInterval(this.timer)
    this.timer = null
    this.log = []
  }

  get isRunning() {
    return this.running
  }

  /** Audio-times of recently scheduled clicks at or after `sinceT`. */
  recentClickTimes(sinceT: number): number[] {
    return this.log.filter((b) => b.time >= sinceT).map((b) => b.time)
  }

  /** The beat sounding at audio-time `t` (for the meter/beat-dots stream). */
  beatAt(t: number): { beatCount: number; beatInBar: number } {
    let cur: Beat | null = null
    for (const b of this.log) if (b.time <= t) cur = b
    return cur
      ? { beatCount: cur.count, beatInBar: cur.inBar }
      : { beatCount: 0, beatInBar: 0 }
  }

  private schedule() {
    if (!this.running) return
    const spb = 60 / this.bpm
    while (this.nextTime < this.ctx.currentTime + LOOKAHEAD_S) {
      const inBar = this.count % this.beats
      this.click(this.nextTime, inBar === 0 && this.accent)
      this.log.push({ count: this.count, inBar, time: this.nextTime })
      if (this.log.length > 16) this.log.shift()
      this.count++
      this.nextTime += spb
    }
  }

  private click(time: number, strong: boolean) {
    const osc = this.ctx.createOscillator()
    const env = this.ctx.createGain()
    osc.frequency.value = strong ? 1600 : 1000
    env.gain.setValueAtTime(0, time)
    env.gain.linearRampToValueAtTime(1, time + 0.001)
    env.gain.exponentialRampToValueAtTime(0.0001, time + CLICK_S)
    osc.connect(env)
    env.connect(this.gain)
    osc.start(time)
    osc.stop(time + CLICK_S + 0.01)
  }
}
