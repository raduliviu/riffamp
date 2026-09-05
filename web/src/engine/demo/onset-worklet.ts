// AudioWorklet host for the demo's onset detector. Runs FluxOnsetDetector on
// the dry guitar input at audio rate and posts onset times on the
// AudioContext clock — the same clock the demo metronome schedules its clicks
// on — so the picking trainer gets sample-accurate timestamps instead of the
// old AnalyserNode-polling estimate. Bundled by Vite via `?worker&url`.
//
// Messages in:  {type:"config", sens, minGap}   {type:"reset"}
// Messages out: {type:"onsets", times:number[]}  (seconds, ctx.currentTime base)
import { FluxOnsetDetector } from "./onset-detector"

// AudioWorkletGlobalScope — not in lib.dom.
declare const sampleRate: number
declare const currentFrame: number
declare function registerProcessor(
  name: string,
  ctor: new () => AudioWorkletProcessorLike
): void
declare class AudioWorkletProcessor {
  readonly port: MessagePort
}
interface AudioWorkletProcessorLike {
  process(inputs: Float32Array[][], outputs: Float32Array[][]): boolean
}

class OnsetProcessor extends AudioWorkletProcessor implements AudioWorkletProcessorLike {
  private readonly det = new FluxOnsetDetector(sampleRate)
  private base = -1 // currentFrame at the detector's sample 0
  private readonly out: number[] = []

  constructor() {
    super()
    this.port.onmessage = (ev: MessageEvent) => {
      const m = ev.data as { type: string; sens?: number; minGap?: number }
      if (m.type === "config") {
        if (typeof m.sens === "number") this.det.setSensitivity(m.sens)
        if (typeof m.minGap === "number") this.det.setMinGap(m.minGap)
      } else if (m.type === "reset") {
        this.det.reset()
        this.base = -1
      }
    }
  }

  process(inputs: Float32Array[][]): boolean {
    const ch = inputs[0]?.[0]
    if (!ch || ch.length === 0) return true
    if (this.base < 0) this.base = currentFrame - this.det.position
    this.det.push(ch, this.out)
    if (this.out.length) {
      const times = this.out.map((s) => (this.base + s) / sampleRate)
      this.out.length = 0
      this.port.postMessage({ type: "onsets", times })
    }
    return true
  }
}

registerProcessor("riffamp-onset", OnsetProcessor)
