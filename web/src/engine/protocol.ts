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
  | "pickOn"
  | "pickSens"

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
  pickOn: boolean
  pickSens: number
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
  /** A pick run (P5b) is currently counting in or recording. */
  pickRunActive: boolean
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

// Picking trainer (P5a), ~12 Hz while pickOn: notes-per-beat and evenness over
// the last bar, plus recent onset/click ages (ms) for the timeline strip.
export interface PickingMessage {
  type: "picking"
  n: number
  npb: number | null
  cv: number | null
  beatMs: number
  onsets: number[]
  clicks: number[]
}

// Pick run (P5b), while a run is active: ~12 Hz progress during the count-in
// and recording, then one result message. The grid is the metronome's actual
// click times (ms relative to bar 1 beat 1); onsets may be slightly negative
// (an anticipated first note inside the engine's half-beat margin). All
// scoring happens client-side in lib/pick-analysis.ts.
export interface PickRunStatusMessage {
  type: "pickRun"
  phase: "countIn" | "recording"
  bar: number // 1-based within the phase
  beat: number // 1-based; 0 while armed before the first click
  bars: number
  countIn: number
}

export interface PickRunResultMessage {
  type: "pickRunResult"
  bars: number
  countIn: number
  beatsPerBar: number
  clicks: number[] // bars*beatsPerBar+1 grid beats, [0 .. end], ms
  onsets: number[] // detected attacks, ms relative to clicks[0]
}

// Pairing (P4f): a non-local origin (the hosted app) gets `needPair` on connect
// and must send { type: "pair", code } with the code the helper prints on the
// local machine; a wrong code yields `pairFailed`. Local origins never see these.
export interface NeedPairMessage {
  type: "needPair"
}

export interface PairFailedMessage {
  type: "pairFailed"
  attemptsLeft: number
}

export type ServerMessage =
  | StateMessage
  | MetersMessage
  | TunerMessage
  | PickingMessage
  | PickRunStatusMessage
  | PickRunResultMessage
  | ErrorMessage
  | NeedPairMessage
  | PairFailedMessage

export type ClientCommand =
  | { type: "hello" }
  | { type: "pair"; code: string }
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
  | { type: "startPickRun"; bars: number; countIn: number }
  | { type: "cancelPickRun" }
