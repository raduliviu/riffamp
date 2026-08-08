// The JSON protocol spoken over ws://127.0.0.1:43717 — mirrors
// helper/src/engine/control.h. If a message shape changes there, it changes
// here; nothing else in the app touches raw protocol JSON.

export const HELPER_WS_URL = "ws://127.0.0.1:43717"

// The newest protocol we know. The helper reports its own in state.version;
// a mismatch is surfaced, not fatal (older helpers just lack newer commands).
export const KNOWN_PROTOCOL_VERSION = "0.2.0"

export type ParamId =
  | "gainIn"
  | "gainOut"
  | "gate"
  | "bass"
  | "mid"
  | "treble"
  | "mute"
  | "metroOn"
  | "metroAccent"
  | "metroBpm"
  | "metroBeats"
  | "metroVol"
  | "tunerOn"
  | "inCh"
  | "drumOn"
  | "drumVol"

export type PedalType = "comp" | "drive" | "chorus" | "delay" | "reverb"
export type Placement = "pre" | "post"

export interface AmpParams {
  gainIn: number
  gainOut: number
  gate: number
  bass: number
  mid: number
  treble: number
  mute: boolean
  metroOn: boolean
  metroAccent: boolean
  metroBpm: number
  metroBeats: number
  metroVol: number
  drumVol: number
  tunerOn: boolean
}

export interface PedalState {
  type: PedalType
  enabled: boolean
  placement: Placement
  order: number
  params: Record<string, number>
}

export interface DeviceInfo {
  index: number
  name: string
  api: string
  channels: number
}

export interface AudioState {
  inputDevice: number
  outputDevice: number
  inCh: number
  inChannels: number
  buffer: number
  inputDevices: DeviceInfo[]
  outputDevices: DeviceInfo[]
  pending?: { input?: string; output?: string; buffer?: number }
}

export interface DrumsState {
  on: boolean
  vol: number
  step: number
  voices: string[]
  beatsPerBar: number
  bars: number
  subdiv: number
  stepCount: number
  pattern: number[][]
}

export interface EngineInfo {
  api: string
  sampleRate: number
  buffer: number
  xruns: number
  reportedLatencyMs: number
}

export interface StateMessage {
  type: "state"
  version: string
  params: AmpParams
  model: string
  ir: string
  models: string[]
  irs: string[]
  pedals: PedalState[]
  audio: AudioState
  drums: DrumsState
  presets: string[]
  grooves: string[]
  engine: EngineInfo
}

export interface MetersMessage {
  type: "meters"
  in: number
  out: number
  beatCount: number
  beatInBar: number
  drumStep: number
}

export interface TunerMessage {
  type: "tuner"
  freq: number
  note?: string
  cents?: number
}

export interface ErrorMessage {
  type: "error"
  message: string
}

export type ServerMessage =
  StateMessage | MetersMessage | TunerMessage | ErrorMessage

export type ClientCommand =
  | { type: "hello" }
  | { type: "panic" }
  | { type: "setParam"; id: ParamId; value: number }
  | { type: "setPedal"; pedal: PedalType; field: string; value: number }
  | { type: "setModel"; name: string }
  | { type: "setIr"; name: string }
  | { type: "setAudioDevice"; input: number; output: number }
  | { type: "setBuffer"; value: number }
  | { type: "setDrumCell"; voice: number; step: number; on: boolean }
  | { type: "setDrumGrid"; beatsPerBar: number; bars: number; subdiv: number }
  | { type: "clearDrums" }
  | { type: "savePreset"; name: string }
  | { type: "loadPreset"; name: string }
  | { type: "deletePreset"; name: string }
  | { type: "saveGroove"; name: string }
  | { type: "loadGroove"; name: string }
  | { type: "deleteGroove"; name: string }
