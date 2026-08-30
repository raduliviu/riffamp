// The StateMessage the demo engine reports. Many fields are inert in the
// browser demo (no device list, no pedals/drums, no presets) but the shape
// must match the helper's so every existing view renders unchanged.

import { KNOWN_PROTOCOL_VERSION } from "../protocol"
import type { AmpParams, StateMessage } from "../protocol"

export const DEMO_MODELS = ["Obsidian"]
export const DEMO_IRS = ["4x12 Full", "4x12 Modern", "4x12 Tight"]

// Maps an IR display name to its bundled file under `${BASE_URL}nam/`.
export const IR_FILES: Record<string, string> = {
  "4x12 Full": "4x12-full.wav",
  "4x12 Modern": "4x12-modern.wav",
  "4x12 Tight": "4x12-tight.wav",
}
export const MODEL_FILE = "obsidian.nam"

export function demoParams(): AmpParams {
  return {
    gainIn: 1,
    gainOut: 1,
    gate: -100, // off (gate not modelled in the demo)
    bass: 0,
    mid: 0,
    treble: 0,
    mute: true, // start silent (as the helper does) — user clicks ENABLE to play
    metroOn: false,
    metroAccent: true,
    metroBpm: 120,
    metroBeats: 4,
    metroVol: 0.5,
    drumVol: 0.5,
    tunerOn: false,
    pickOn: false,
    pickSens: 0.5,
    pickTarget: 4,
  }
}

export function demoState(params: AmpParams, sampleRate: number): StateMessage {
  return {
    type: "state",
    version: KNOWN_PROTOCOL_VERSION,
    params,
    model: "Obsidian",
    ir: "4x12 Full",
    models: DEMO_MODELS,
    irs: DEMO_IRS,
    pedals: [], // pedalboard deferred in the demo
    audio: {
      inputDevice: 0,
      outputDevice: 0,
      inCh: 0,
      inChannels: 1,
      buffer: 128,
      inputDevices: [
        { index: 0, name: "Browser input", api: "WebAudio", channels: 1 },
      ],
      outputDevices: [
        { index: 0, name: "Browser output", api: "WebAudio", channels: 2 },
      ],
    },
    drums: {
      on: false,
      vol: 0.5,
      step: 0,
      voices: [],
      beatsPerBar: 4,
      bars: 1,
      subdiv: 4,
      stepCount: 0,
      pattern: [],
    },
    presets: [],
    grooves: [],
    pickRunActive: false,
    engine: {
      api: "WebAudio",
      sampleRate,
      buffer: 128,
      xruns: 0,
      reportedLatencyMs: 0,
    },
  }
}
