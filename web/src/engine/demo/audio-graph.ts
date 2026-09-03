// The demo's Web Audio graph:
//
//   input(device) ─▶ splitter[ch] ─▶ inGain ─▶ NAM(amp) ─▶ convolver(cab IR)
//         ─▶ bass▸mid▸treble ─▶ outGain ─▶ analyserOut ─▶ speakers(sinkId)
//   splitter[ch] ─▶ analyserIn, analyserFlux   (dry taps: meter, tuner, onsets)
//
// The amp is the WASM NamNode (mono in/out); the cab is a native ConvolverNode
// fed our .wav IR; the tone stack is three BiquadFilters. Guitar input needs
// the browser's voice DSP (echo/noise/AGC) OFF or it mangles the signal.
//
// Device selection IS possible in the browser (what the native app adds is real
// low latency + buffer control, not device access): the input front-end
// (stream → source → splitter) is rebuildable so the user can pick their
// interface and the guitar's channel, and output routes via AudioContext
// setSinkId where supported (Chromium). Only the input front-end is torn down
// on a device change — the amp chain and analysers persist, so playback and the
// tuner/picking taps survive it.

import { NamEngine } from "neural-amp-modeler-wasm/engine"
import type { NamNode } from "neural-amp-modeler-wasm/engine"
import { IR_FILES, MODEL_FILE } from "./default-state"

const MODEL_SR = 48000 // the Obsidian capture is 48 kHz; run the context to match

function assetUrl(file: string): string {
  return `${import.meta.env.BASE_URL}nam/${file}`
}

export interface AudioDevice {
  deviceId: string
  label: string
}

// AudioContext.setSinkId (output routing) is Chromium-only for now; typed here
// since lib.dom doesn't include it everywhere.
type SinkCapableContext = AudioContext & {
  setSinkId?: (id: string) => Promise<void>
}

export class DemoAudioGraph {
  ctx!: AudioContext
  analyserIn!: AnalyserNode // dry input: meters + tuner (time domain)
  analyserFlux!: AnalyserNode // dry input: onset detection (short FFT)
  analyserOut!: AnalyserNode

  // Live input state the engine reports back to the UI.
  inChannels = 1
  currentChannel = 0 // 0-based
  inputDeviceId: string | undefined // the actual device getUserMedia gave us
  outputDeviceId = "" // "" = system default sink

  // Rebuildable input front-end.
  private stream: MediaStream | null = null
  private source: MediaStreamAudioSourceNode | null = null
  private splitter: ChannelSplitterNode | null = null

  // Persistent amp chain.
  private inGain!: GainNode
  private nam!: NamNode
  private convolver!: ConvolverNode
  private low!: BiquadFilterNode
  private mid!: BiquadFilterNode
  private high!: BiquadFilterNode
  private outGain!: GainNode

  private muted = false
  private outLevel = 1

  /** Build the chain, load the model + IR, open the default input. */
  async init(): Promise<void> {
    this.ctx = new AudioContext({
      sampleRate: MODEL_SR,
      latencyHint: "interactive",
    })

    const ctx = this.ctx
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

    // Persistent chain: inGain → amp → cab → tone → out → destination.
    this.inGain.connect(this.nam)
    this.nam.connect(this.convolver)
    this.convolver.connect(this.low)
    this.low.connect(this.mid)
    this.mid.connect(this.high)
    this.high.connect(this.outGain)
    this.outGain.connect(this.analyserOut)
    this.analyserOut.connect(ctx.destination)

    await this.openInput(undefined, 0) // default device, first channel
    await this.setIr("4x12 Full")
    if (ctx.state === "suspended") await ctx.resume()
  }

  /** Whether output-device routing is available (Chromium setSinkId). */
  get supportsOutputRouting(): boolean {
    return typeof (this.ctx as SinkCapableContext).setSinkId === "function"
  }

  /** Enumerate real audio devices (labels require the mic permission we hold). */
  async devices(): Promise<{ inputs: AudioDevice[]; outputs: AudioDevice[] }> {
    const all = await navigator.mediaDevices.enumerateDevices()
    const pick = (kind: MediaDeviceKind) =>
      all
        .filter((d) => d.kind === kind && d.deviceId !== "communications")
        .map((d, i) => ({
          deviceId: d.deviceId,
          label: d.label || `${kind === "audioinput" ? "Input" : "Output"} ${i + 1}`,
        }))
    return { inputs: pick("audioinput"), outputs: pick("audiooutput") }
  }

  /** (Re)open the input from `deviceId` (undefined = default) and route `channel`. */
  async openInput(deviceId: string | undefined, channel: number): Promise<void> {
    this.stream?.getTracks().forEach((t) => t.stop())
    this.source?.disconnect()
    this.splitter?.disconnect()

    const audio: MediaTrackConstraints = {
      echoCancellation: false,
      noiseSuppression: false,
      autoGainControl: false,
      channelCount: { ideal: 2 }, // expose both jacks of a 2-in interface
    }
    if (deviceId) audio.deviceId = { exact: deviceId }
    this.stream = await navigator.mediaDevices.getUserMedia({ audio })

    const track = this.stream.getAudioTracks()[0]
    const settings = track.getSettings()
    this.inChannels = Math.max(1, settings.channelCount ?? 1)
    this.inputDeviceId = settings.deviceId ?? deviceId

    this.source = this.ctx.createMediaStreamSource(this.stream)
    this.splitter = this.ctx.createChannelSplitter(this.inChannels)
    this.source.connect(this.splitter)
    this.routeChannel(Math.min(Math.max(0, channel), this.inChannels - 1))
  }

  /** Route splitter output `ch` into the amp chain and the dry taps. */
  private routeChannel(ch: number): void {
    if (!this.splitter) return
    try {
      this.splitter.disconnect()
    } catch {
      /* nothing connected yet */
    }
    this.splitter.connect(this.inGain, ch, 0)
    this.splitter.connect(this.analyserIn, ch, 0)
    this.splitter.connect(this.analyserFlux, ch, 0)
    this.currentChannel = ch
  }

  setInputChannel(ch: number): void {
    this.routeChannel(Math.min(Math.max(0, ch), this.inChannels - 1))
  }

  /** Route output to a device (deviceId, or "" for the system default). */
  async setOutputDevice(deviceId: string): Promise<void> {
    const ctx = this.ctx as SinkCapableContext
    if (typeof ctx.setSinkId !== "function") return
    await ctx.setSinkId(deviceId)
    this.outputDeviceId = deviceId
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
