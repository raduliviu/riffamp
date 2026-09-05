// DemoEngine: the in-browser WASM amp (P4c). Implements the same Engine
// interface as HelperEngine, so every view works unchanged — it just runs the
// audio in Web Audio (NAM wasm + convolver) instead of the native helper.
//
// Scope ("amp wow + practice tools"): amp + cab IR + tone stack live here;
// metronome / tuner / picking are layered in sibling modules. Pedals, the
// noise gate, and drums are intentionally absent in the demo.

import { Emitter } from "../engine"
import type {
  ConnectionStatus,
  Engine,
  PairingState,
  PickRunMessage,
  Unsubscribe,
} from "../engine"
import type {
  AudioState,
  ClientCommand,
  DeviceInfo,
  MetersMessage,
  ParamId,
  PedalType,
  PickingMessage,
  StateMessage,
  TunerMessage,
} from "../protocol"
import { DemoAudioGraph } from "./audio-graph"
import type { AudioDevice } from "./audio-graph"
import { demoParams, demoState } from "./default-state"
import { DemoMetronome } from "./metronome"
import { DemoPicking } from "./picking"
import { detectPitch, freqToNote } from "./tuner"

const METER_HZ = 25
const TUNER_HZ = 20

export class DemoEngine implements Engine {
  readonly kind = "demo" as const

  private graph = new DemoAudioGraph()
  private metro: DemoMetronome | null = null
  private picking: DemoPicking | null = null
  private pickRunActive = false
  private params = demoParams()
  private model = "Obsidian"
  private ir = "4x12 Full"
  private meterTimer: ReturnType<typeof setInterval> | null = null
  private tunerTimer: ReturnType<typeof setInterval> | null = null
  private tunerBuf: Float32Array<ArrayBuffer> | null = null
  private started = false

  // Real audio devices (browsers CAN enumerate/select these — what they can't
  // do is low latency or buffer control, which is the native app's job).
  private inputs: AudioDevice[] = []
  private outputs: AudioDevice[] = []
  private inputDevice = 0 // index into `inputs`
  private outputDevice = 0 // index into `outputs`
  private inCh = 1 // 1-based guitar channel

  private status$ = new Emitter<ConnectionStatus>()
  private state$ = new Emitter<StateMessage>()
  private error$ = new Emitter<string>()
  private meters$ = new Emitter<MetersMessage>()
  private tuner$ = new Emitter<TunerMessage>()
  private picking$ = new Emitter<PickingMessage>()
  private pickRun$ = new Emitter<PickRunMessage>()

  start() {
    if (this.started) return
    this.started = true
    this.status$.emit("connecting")
    this.graph
      .init()
      .then(async () => {
        await this.refreshDevices()
        this.metro = new DemoMetronome(this.graph.ctx)
        this.picking = new DemoPicking({
          ctx: this.graph.ctx,
          onsets: this.graph.onsetNode,
          metro: this.metro,
          grid: () => ({
            bpm: this.params.metroBpm,
            beatsPerBar: this.params.metroBeats,
          }),
          sens: () => this.params.pickSens,
          target: () => this.params.pickTarget,
          emitPicking: (m) => this.picking$.emit(m),
          emitPickRun: (m) => this.pickRun$.emit(m),
          onRunActive: (active) => {
            this.pickRunActive = active
            this.emitState()
          },
        })
        this.applyAll()
        this.status$.emit("connected")
        this.emitState()
        this.startMeterLoop()
      })
      .catch((e: unknown) => {
        this.error$.emit(micErrorMessage(e))
        this.status$.emit("offline")
        // Allow a clean retry (e.g. after the user grants mic access).
        this.started = false
        void this.graph.dispose()
        this.graph = new DemoAudioGraph()
      })
  }

  stop() {
    this.started = false
    if (this.meterTimer) clearInterval(this.meterTimer)
    this.meterTimer = null
    this.stopTuner()
    this.picking?.stop()
    this.picking = null
    this.metro?.stop()
    this.metro = null
    void this.graph.dispose()
  }

  send(cmd: ClientCommand) {
    switch (cmd.type) {
      case "setParam":
        this.applyParam(cmd.id, cmd.value)
        break
      case "setModel":
        this.model = cmd.name
        void this.graph.setModel(cmd.name)
        this.emitState()
        break
      case "setIr":
        this.ir = cmd.name
        void this.graph.setIr(cmd.name)
        this.emitState()
        break
      case "panic":
        this.applyParam("mute", 1)
        break
      case "startPickRun":
        this.picking?.startRun(cmd.bars, cmd.countIn)
        break
      case "cancelPickRun":
        this.picking?.cancelRun()
        break
      case "setAudioDevice":
        void this.changeDevices(cmd.input, cmd.output)
        break
      // Buffer, pedals, drums, presets, pairing: no-ops in the demo (buffer is
      // native-only; the other views are hidden or inert here).
    }
  }

  /** Enumerate real devices and map the active input to its list index. */
  private async refreshDevices(): Promise<void> {
    try {
      const { inputs, outputs } = await this.graph.devices()
      this.inputs = inputs
      this.outputs = outputs
      const active = this.graph.inputDeviceId
      const i = inputs.findIndex((d) => d.deviceId === active)
      this.inputDevice = i >= 0 ? i : 0
      this.outputDevice = 0 // system default until the user picks one
      this.inCh = this.graph.currentChannel + 1
    } catch {
      /* enumeration can fail on locked-down browsers; keep defaults */
    }
  }

  private async changeDevices(inIdx: number, outIdx: number): Promise<void> {
    try {
      if (inIdx !== this.inputDevice && this.inputs[inIdx]) {
        this.inputDevice = inIdx
        await this.graph.openInput(this.inputs[inIdx].deviceId, this.inCh - 1)
        this.inCh = this.graph.currentChannel + 1
      }
      if (outIdx !== this.outputDevice && this.outputs[outIdx]) {
        this.outputDevice = outIdx
        await this.graph.setOutputDevice(this.outputs[outIdx].deviceId)
      }
    } catch (e) {
      this.error$.emit(micErrorMessage(e))
    }
    this.emitState()
  }

  pair(_code: string) {
    // The demo has no helper to pair with.
  }

  setParam(id: ParamId, value: number) {
    this.applyParam(id, value)
  }
  setPedalParam(_pedal: PedalType, _field: string, _value: number) {
    // Pedalboard is deferred in the demo.
  }

  private applyParam(id: ParamId, value: number) {
    if (id === "inCh") {
      this.inCh = value
      this.graph.setInputChannel(value - 1)
      this.emitState()
      return
    }
    const p = this.params as unknown as Record<string, number | boolean>
    // metroOn/mute/tunerOn/pickOn/metroAccent are booleans over the wire (0/1).
    if (
      id === "mute" ||
      id === "metroOn" ||
      id === "metroAccent" ||
      id === "tunerOn" ||
      id === "pickOn"
    ) {
      p[id] = value !== 0
    } else {
      p[id] = value
    }
    this.applyToGraph(id)
    this.emitState()
  }

  private applyToGraph(id: ParamId) {
    switch (id) {
      case "gainIn":
        this.graph.setInGain(this.params.gainIn)
        break
      case "gainOut":
        this.graph.setOutGain(this.params.gainOut)
        break
      case "mute":
        this.graph.setMute(this.params.mute)
        break
      case "bass":
      case "mid":
      case "treble":
        this.graph.setTone(this.params.bass, this.params.mid, this.params.treble)
        break
      case "metroBpm":
        this.metro?.setBpm(this.params.metroBpm)
        break
      case "metroBeats":
        this.metro?.setBeats(this.params.metroBeats)
        break
      case "metroAccent":
        this.metro?.setAccent(this.params.metroAccent)
        break
      case "metroVol":
        this.metro?.setVol(this.params.metroVol)
        break
      case "metroOn":
        if (this.params.metroOn) this.metro?.start()
        else this.metro?.stop()
        break
      case "tunerOn":
        if (this.params.tunerOn) this.startTuner()
        else this.stopTuner()
        break
      case "pickOn":
        this.picking?.setEnabled(this.params.pickOn)
        break
      // pickSens / pickTarget are read live by the picking module via getters.
    }
  }

  /** Push every current param into the graph after (re)connect. */
  private applyAll() {
    this.graph.setInGain(this.params.gainIn)
    this.graph.setOutGain(this.params.gainOut)
    this.graph.setMute(this.params.mute)
    this.graph.setTone(this.params.bass, this.params.mid, this.params.treble)
    this.metro?.setBpm(this.params.metroBpm)
    this.metro?.setBeats(this.params.metroBeats)
    this.metro?.setAccent(this.params.metroAccent)
    this.metro?.setVol(this.params.metroVol)
    if (this.params.metroOn) this.metro?.start()
    if (this.params.tunerOn) this.startTuner()
    if (this.params.pickOn) this.picking?.setEnabled(true)
  }

  private startTuner() {
    if (this.tunerTimer) return
    const a = this.graph.analyserIn
    this.tunerBuf = new Float32Array(new ArrayBuffer(a.fftSize * 4))
    this.tunerTimer = setInterval(() => {
      const buf = this.tunerBuf
      if (!buf) return
      this.graph.analyserIn.getFloatTimeDomainData(buf)
      const freq = detectPitch(buf, this.graph.ctx.sampleRate)
      if (freq > 0) {
        const { note, cents } = freqToNote(freq)
        this.tuner$.emit({ type: "tuner", freq, note, cents })
      } else {
        this.tuner$.emit({ type: "tuner", freq: 0 })
      }
    }, 1000 / TUNER_HZ)
  }

  private stopTuner() {
    if (this.tunerTimer) clearInterval(this.tunerTimer)
    this.tunerTimer = null
    this.tunerBuf = null
  }

  private startMeterLoop() {
    this.meterTimer = setInterval(() => {
      const { in: inLvl, out } = this.graph.levels()
      const beat =
        this.metro?.isRunning && this.params.metroOn
          ? this.metro.beatAt(this.graph.ctx.currentTime)
          : { beatCount: 0, beatInBar: 0 }
      this.meters$.emit({
        type: "meters",
        in: inLvl,
        out,
        beatCount: beat.beatCount,
        beatInBar: beat.beatInBar,
        drumStep: 0,
      })
    }, 1000 / METER_HZ)
  }

  private emitState() {
    const s = demoState(this.params, this.graph.ctx?.sampleRate ?? 48000)
    s.model = this.model
    s.ir = this.ir
    s.pickRunActive = this.pickRunActive
    s.audio = this.audioState()
    this.state$.emit(s)
  }

  private audioState(): AudioState {
    const toInfo = (d: AudioDevice, i: number): DeviceInfo => ({
      index: i,
      name: d.label,
      api: "WebAudio",
      channels: 0,
    })
    // Fall back to a single placeholder so the picker always renders something.
    const inputDevices = this.inputs.length
      ? this.inputs.map(toInfo)
      : [{ index: 0, name: "Browser input", api: "WebAudio", channels: 1 }]
    const outputDevices =
      this.graph.supportsOutputRouting && this.outputs.length
        ? this.outputs.map(toInfo)
        : [{ index: 0, name: "System default", api: "WebAudio", channels: 2 }]
    return {
      inputDevice: this.inputDevice,
      outputDevice: this.outputDevice,
      inCh: this.inCh,
      inChannels: this.graph.inChannels,
      buffer: 128,
      inputDevices,
      outputDevices,
    }
  }

  onStatus(cb: (s: ConnectionStatus) => void): Unsubscribe {
    return this.status$.subscribe(cb)
  }
  onState(cb: (s: StateMessage) => void): Unsubscribe {
    return this.state$.subscribe(cb)
  }
  onError(cb: (m: string) => void): Unsubscribe {
    return this.error$.subscribe(cb)
  }
  onPairing(_cb: (p: PairingState) => void): Unsubscribe {
    return () => {}
  }
  onMeters(cb: (m: MetersMessage) => void): Unsubscribe {
    return this.meters$.subscribe(cb)
  }
  onTuner(cb: (t: TunerMessage) => void): Unsubscribe {
    return this.tuner$.subscribe(cb)
  }
  onPicking(cb: (p: PickingMessage) => void): Unsubscribe {
    return this.picking$.subscribe(cb)
  }
  onPickRun(cb: (m: PickRunMessage) => void): Unsubscribe {
    return this.pickRun$.subscribe(cb)
  }
}

function micErrorMessage(e: unknown): string {
  const name = (e as { name?: string })?.name
  if (name === "NotAllowedError" || name === "SecurityError")
    return "Microphone access was blocked. Allow it to use the browser demo."
  if (name === "NotFoundError")
    return "No audio input found. Plug in your interface and reload."
  return `Couldn't start the browser demo: ${(e as Error)?.message ?? e}`
}
