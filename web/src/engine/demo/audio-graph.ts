// The demo's Web Audio graph:
//
//   mic ─▶ inGain ─▶ NAM(amp) ─▶ convolver(cab IR) ─▶ bass▸mid▸treble ─▶
//         outGain ─▶ analyserOut ─▶ speakers
//   mic ─▶ analyserIn   (dry tap: input meter + tuner + picking onsets)
//
// The amp is the WASM NamNode (mono in/out); the cab is a native ConvolverNode
// fed our .wav IR; the tone stack is three BiquadFilters. Guitar input needs
// the browser's voice DSP (echo/noise/AGC) OFF or it mangles the signal.

import { NamEngine } from "neural-amp-modeler-wasm/engine"
import type { NamNode } from "neural-amp-modeler-wasm/engine"
import { IR_FILES, MODEL_FILE } from "./default-state"

const MODEL_SR = 48000 // the Obsidian capture is 48 kHz; run the context to match

function assetUrl(file: string): string {
  return `${import.meta.env.BASE_URL}nam/${file}`
}

export class DemoAudioGraph {
  ctx!: AudioContext
  analyserIn!: AnalyserNode // dry input: meters + tuner (time domain)
  analyserFlux!: AnalyserNode // dry input: onset detection (short FFT)
  analyserOut!: AnalyserNode

  private stream!: MediaStream
  private source!: MediaStreamAudioSourceNode
  private inGain!: GainNode
  private nam!: NamNode
  private convolver!: ConvolverNode
  private low!: BiquadFilterNode
  private mid!: BiquadFilterNode
  private high!: BiquadFilterNode
  private outGain!: GainNode

  private muted = false
  private outLevel = 1

  /** Build the graph and load the model + IR. Throws on mic denial / load. */
  async init(): Promise<void> {
    this.ctx = new AudioContext({
      sampleRate: MODEL_SR,
      latencyHint: "interactive",
    })
    this.stream = await navigator.mediaDevices.getUserMedia({
      audio: {
        echoCancellation: false,
        noiseSuppression: false,
        autoGainControl: false,
        channelCount: 1,
      },
    })

    const ctx = this.ctx
    this.source = ctx.createMediaStreamSource(this.stream)
    this.inGain = ctx.createGain()
    this.convolver = ctx.createConvolver()
    this.low = ctx.createBiquadFilter()
    this.low.type = "lowshelf"
    this.low.frequency.value = 120
    this.mid = ctx.createBiquadFilter()
    this.mid.type = "peaking"
    this.mid.frequency.value = 750
    this.mid.Q.value = 0.9
    this.high = ctx.createBiquadFilter()
    this.high.type = "highshelf"
    this.high.frequency.value = 3200
    this.outGain = ctx.createGain()
    this.analyserIn = ctx.createAnalyser()
    this.analyserIn.fftSize = 2048
    this.analyserFlux = ctx.createAnalyser()
    this.analyserFlux.fftSize = 512 // ~11 ms window — crisp attacks for onsets
    this.analyserFlux.smoothingTimeConstant = 0
    this.analyserOut = ctx.createAnalyser()
    this.analyserOut.fftSize = 1024

    // The amp node.
    const engine = await NamEngine.attach(ctx)
    this.nam = await engine.createNode()
    await this.nam.loadModel(await fetchText(assetUrl(MODEL_FILE)))

    // Wire the chain.
    this.source.connect(this.inGain)
    this.inGain.connect(this.nam)
    this.nam.connect(this.convolver)
    this.convolver.connect(this.low)
    this.low.connect(this.mid)
    this.mid.connect(this.high)
    this.high.connect(this.outGain)
    this.outGain.connect(this.analyserOut)
    this.analyserOut.connect(ctx.destination)
    // Dry taps: input meter + tuner, and the onset detector.
    this.source.connect(this.analyserIn)
    this.source.connect(this.analyserFlux)

    await this.setIr("4x12 Full")
    if (ctx.state === "suspended") await ctx.resume()
  }

  async setIr(name: string): Promise<void> {
    const file = IR_FILES[name]
    if (!file) return
    const buf = await this.ctx.decodeAudioData(
      await fetchArrayBuffer(assetUrl(file))
    )
    this.convolver.buffer = buf
  }

  // Only one model in the demo; setModel is a no-op kept for interface parity.
  async setModel(_name: string): Promise<void> {}

  setInGain(v: number) {
    this.inGain.gain.setTargetAtTime(v, this.ctx.currentTime, 0.01)
  }
  setOutGain(v: number) {
    this.outLevel = v
    this.applyOut()
  }
  setMute(m: boolean) {
    this.muted = m
    this.applyOut()
  }
  private applyOut() {
    const g = this.muted ? 0 : this.outLevel
    this.outGain.gain.setTargetAtTime(g, this.ctx.currentTime, 0.01)
  }
  setTone(bass: number, mid: number, treble: number) {
    this.low.gain.value = bass
    this.mid.gain.value = mid
    this.high.gain.value = treble
  }

  /** Peak amplitude (0..1) at input and output, for the meter stream. */
  levels(): { in: number; out: number } {
    return {
      in: peak(this.analyserIn),
      out: this.muted ? 0 : peak(this.analyserOut),
    }
  }

  async dispose(): Promise<void> {
    try {
      await this.nam?.dispose()
    } catch {
      /* already gone */
    }
    this.stream?.getTracks().forEach((t) => t.stop())
    await this.ctx?.close()
  }
}

async function fetchText(url: string): Promise<string> {
  const r = await fetch(url)
  if (!r.ok) throw new Error(`failed to load ${url}: ${r.status}`)
  return r.text()
}
async function fetchArrayBuffer(url: string): Promise<ArrayBuffer> {
  const r = await fetch(url)
  if (!r.ok) throw new Error(`failed to load ${url}: ${r.status}`)
  return r.arrayBuffer()
}

const scratch = new WeakMap<AnalyserNode, Float32Array<ArrayBuffer>>()
function peak(a: AnalyserNode): number {
  let buf = scratch.get(a)
  if (!buf || buf.length !== a.fftSize) {
    buf = new Float32Array(new ArrayBuffer(a.fftSize * 4))
    scratch.set(a, buf)
  }
  a.getFloatTimeDomainData(buf)
  let m = 0
  for (let i = 0; i < buf.length; i++) {
    const x = Math.abs(buf[i])
    if (x > m) m = x
  }
  return m
}
