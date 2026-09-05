// Spectral-flux onset detector — a line-for-line port of the native helper's
// `helper/src/dsp/flux.h` (FluxDetectorT<512> + OdfPeakPicker) so the browser
// demo detects exactly what the installed app detects. Keep the two in step:
// every constant below has a namesake in flux.h, and `web/tests/onset_eval.ts`
// checks this port against picking_test.exe on the same recordings.
//
// Pure TypeScript, no DOM/audio dependencies: it runs inside the
// AudioWorklet (onset-worklet.ts) and in Node for offline verification.
//
// Method: 512-pt Hann STFT, hop 256 (5.3 ms) · level normalizer in front of
// the STFT · raw-frame silence gate · log-compressed magnitudes · SuperFlux
// positive difference vs the 3-bin max-filtered previous frame · median+MAD
// adaptive threshold with a relative floor · post-onset mask · stronger-wins
// refractory gate driven by the expected note rate. See flux.h for the
// measurements behind each choice.

const K_FFT = 512
const K_HOP = 256
const K_BINS = K_FFT / 2 + 1

/** Fixed 512-point real FFT (iterative radix-2), magnitudes for bins 0..256. */
class Fft512 {
  private readonly twRe = new Float32Array(K_FFT / 2)
  private readonly twIm = new Float32Array(K_FFT / 2)
  private readonly rev = new Uint16Array(K_FFT)
  private readonly re = new Float64Array(K_FFT)
  private readonly im = new Float64Array(K_FFT)

  constructor() {
    for (let k = 0; k < K_FFT / 2; k++) {
      this.twRe[k] = Math.cos((-2 * Math.PI * k) / K_FFT)
      this.twIm[k] = Math.sin((-2 * Math.PI * k) / K_FFT)
    }
    for (let i = 0; i < K_FFT; i++) {
      let r = 0
      for (let b = 0; b < 9; b++) r |= ((i >> b) & 1) << (8 - b)
      this.rev[i] = r
    }
  }

  magnitudes(input: Float32Array, mag: Float32Array): void {
    const { re, im, rev, twRe, twIm } = this
    for (let i = 0; i < K_FFT; i++) {
      re[i] = input[rev[i]]
      im[i] = 0
    }
    for (let len = 2; len <= K_FFT; len *= 2) {
      const step = K_FFT / len
      const half = len / 2
      for (let start = 0; start < K_FFT; start += len) {
        for (let k = 0; k < half; k++) {
          const wr = twRe[k * step]
          const wi = twIm[k * step]
          const a = start + k
          const b = a + half
          const xr = re[b] * wr - im[b] * wi
          const xi = re[b] * wi + im[b] * wr
          re[b] = re[a] - xr
          im[b] = im[a] - xi
          re[a] += xr
          im[a] += xi
        }
      }
    }
    for (let k = 0; k < K_BINS; k++) mag[k] = Math.sqrt(re[k] * re[k] + im[k] * im[k])
  }
}

/** Peak picker over an onset detection function — see OdfPeakPicker in flux.h. */
class OdfPeakPicker {
  static readonly LOOKAHEAD = 3
  static readonly WINDOW = 130
  static readonly RECENT = 8

  peakFrac = 0.05
  absFloor = 0.5
  deltaK = 2.0
  minGapSamples = 1200
  strongerWins = true
  swRatio = 2.0
  postMaskFrac = 0.5
  postMaskTau = 0.06
  sr = 48000

  private lastV = 0
  private readonly recentV = new Float32Array(OdfPeakPicker.RECENT)
  private recentN = 0
  private recentPos = 0
  private readonly hist = new Float32Array(OdfPeakPicker.WINDOW)
  private readonly histT = new Float64Array(OdfPeakPicker.WINDOW)
  private histN = 0
  private lastOnset = 0
  private anyOnset = false
  private pendT = 0
  private pendV = 0
  private havePend = false
  private readonly scratch = new Float32Array(OdfPeakPicker.WINDOW)
  private readonly scratch2 = new Float32Array(OdfPeakPicker.WINDOW)

  reset(): void {
    this.histN = 0
    this.anyOnset = false
    this.havePend = false
    this.recentN = 0
    this.recentPos = 0
  }
  setSensitivity(sens: number): void {
    this.deltaK = 3.0 - 2.0 * Math.min(1, Math.max(0, sens))
  }
  setMinGap(seconds: number, sampleRate: number): void {
    this.sr = sampleRate
    this.minGapSamples = Math.floor(Math.min(0.15, Math.max(0.025, seconds)) * sampleRate)
  }
  /** End of stream (offline use): emit a held-back candidate. */
  flush(out: number[]): void {
    if (!this.havePend) return
    this.havePend = false
    this.emit(this.pendT, this.pendV, out)
  }

  private emit(t: number, v: number, out: number[]): void {
    this.lastOnset = t
    this.lastV = v
    this.recentV[this.recentPos] = v
    this.recentPos = (this.recentPos + 1) % OdfPeakPicker.RECENT
    this.recentN = Math.min(this.recentN + 1, OdfPeakPicker.RECENT)
    this.anyOnset = true
    out.push(t)
  }
  private maskAnchor(): number {
    if (this.recentN < 2) return 0 // one note so far (typically from silence, ~10x)
    const w = Array.from(this.recentV.subarray(0, this.recentN)).sort((a, b) => a - b)
    return Math.min(this.lastV, 3.0 * w[this.recentN >> 1])
  }

  /** Feed one frame's ODF value with its frame start (samples). */
  push(v: number, frameStart: number, out: number[]): void {
    const { hist, histT } = this
    const W = OdfPeakPicker.WINDOW
    const L = OdfPeakPicker.LOOKAHEAD
    if (this.histN === W) {
      hist.copyWithin(0, 1)
      histT.copyWithin(0, 1)
      this.histN--
    }
    hist[this.histN] = v
    histT[this.histN] = frameStart
    this.histN++

    if (this.havePend && frameStart - this.pendT >= this.minGapSamples) {
      this.havePend = false
      this.emit(this.pendT, this.pendV, out)
    }

    const c = this.histN - 1 - L
    if (c < L) return
    for (let j = c - L; j <= c + L; j++) if (hist[j] > hist[c]) return

    // median + MAD over hist[0..c], plus the running peak
    const n = c + 1
    const w = this.scratch.subarray(0, n)
    w.set(hist.subarray(0, n))
    w.sort()
    const med = w[c >> 1] // matches nth_element(w, w + c/2)
    let peak = 0
    const dev = this.scratch2.subarray(0, n)
    for (let j = 0; j < n; j++) {
      dev[j] = Math.abs(hist[j] - med)
      if (hist[j] > peak) peak = hist[j]
    }
    dev.sort()
    const mad = dev[c >> 1]
    let thr = Math.max(med + this.deltaK * 1.4826 * mad, this.peakFrac * peak, this.absFloor)
    const t = histT[c]
    if (this.postMaskFrac > 0 && this.anyOnset && t > this.lastOnset) {
      const dt = (t - this.lastOnset) / this.sr
      thr = Math.max(thr, this.postMaskFrac * this.maskAnchor() * Math.exp(-dt / this.postMaskTau))
    }
    if (hist[c] < thr) return

    if (!this.strongerWins) {
      if (this.anyOnset && t - this.lastOnset < this.minGapSamples) return
      this.emit(t, hist[c], out)
      return
    }
    if (this.anyOnset && t - this.lastOnset < this.minGapSamples) return
    if (this.havePend) {
      if (hist[c] > this.swRatio * this.pendV) {
        this.pendT = t
        this.pendV = hist[c]
      }
      return
    }
    this.pendT = t
    this.pendV = hist[c]
    this.havePend = true
  }
}

export class FluxOnsetDetector {
  // Tunables — same names and defaults as FluxDetectorT in flux.h.
  peakFrac = 0.05
  binFloor = 0.6
  magFloor = 0.2
  normalize = true
  normTarget = 0.5
  normMaxGain = 20
  normAtkS = 0.03
  silenceDb = -60

  readonly pick = new OdfPeakPicker()
  private readonly fft = new Fft512()
  private readonly window = new Float32Array(K_FFT)
  private readonly frame = new Float32Array(K_FFT)
  private readonly buf = new Float32Array(K_FFT)
  private readonly mag = new Float32Array(K_BINS)
  private readonly logMag = new Float32Array(K_BINS)
  private readonly prevLog = new Float32Array(K_BINS)
  private hopFill = 0
  private samplePos = 0
  private havePrev = false
  private env = 0
  private readonly envAtk: number
  private readonly envRel: number
  private readonly logFloor: number
  readonly sr: number

  constructor(sr: number) {
    this.sr = sr
    for (let i = 0; i < K_FFT; i++)
      this.window[i] = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (K_FFT - 1))
    this.envAtk = 1 - Math.exp(-1 / (this.normAtkS * sr))
    this.envRel = 1 - Math.exp(-1 / (2.0 * sr))
    this.logFloor = Math.log1p(50 * this.magFloor)
    this.pick.sr = sr
    this.reset()
  }

  setSensitivity(sens: number): void {
    this.pick.setSensitivity(sens)
  }
  setMinGap(seconds: number): void {
    this.pick.setMinGap(seconds, this.sr)
  }
  flush(out: number[]): void {
    this.pick.flush(out)
  }
  reset(): void {
    this.frame.fill(0)
    this.hopFill = 0
    this.samplePos = 0
    this.havePrev = false
    this.env = 0
    this.pick.reset()
  }
  /** Samples consumed so far (the time base of the onsets in `out`). */
  get position(): number {
    return this.samplePos
  }

  /** Feed samples; detected onsets (sample index of their frame start) go to `out`. */
  push(x: Float32Array, out: number[]): void {
    const { frame } = this
    for (let i = 0; i < x.length; i++) {
      const a = Math.abs(x[i])
      this.env += (a - this.env) * (a > this.env ? this.envAtk : this.envRel)
      frame[K_FFT - K_HOP + this.hopFill] = x[i]
      this.hopFill++
      this.samplePos++
      if (this.hopFill < K_HOP) continue
      this.hopFill = 0
      this.processFrame(this.samplePos >= K_FFT ? this.samplePos - K_FFT : 0, out)
      frame.copyWithin(0, K_HOP)
    }
  }

  private processFrame(frameStart: number, out: number[]): void {
    const { frame, window, buf, mag, logMag, prevLog } = this
    let gain = 1
    if (this.normalize) gain = Math.min(this.normMaxGain, this.normTarget / Math.max(this.env, 1e-6))
    let e = 0
    for (let i = 0; i < K_FFT; i++) {
      buf[i] = frame[i] * window[i] * gain
      e += frame[i] * frame[i]
    }
    const rmsDb = 10 * Math.log10(e / K_FFT + 1e-20)
    this.fft.magnitudes(buf, mag)
    for (let k = 0; k < K_BINS; k++) logMag[k] = Math.log1p(50 * mag[k])

    let flux = 0
    if (this.havePrev && rmsDb >= this.silenceDb) {
      for (let k = 0; k < K_BINS; k++) {
        if (logMag[k] < this.logFloor) continue
        let p = prevLog[k]
        if (k > 0 && prevLog[k - 1] > p) p = prevLog[k - 1]
        if (k + 1 < K_BINS && prevLog[k + 1] > p) p = prevLog[k + 1]
        const d = logMag[k] - p
        if (d > this.binFloor) flux += d
      }
    }
    prevLog.set(logMag)
    this.havePrev = true

    this.pick.peakFrac = this.peakFrac
    this.pick.push(flux, frameStart, out)
  }
}
