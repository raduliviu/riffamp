// The Engine abstraction: the UI talks to one of these and never knows
// whether it's the native helper (WebSocket) or the in-browser WASM demo.

import type {
  ClientCommand,
  MetersMessage,
  ParamId,
  PedalType,
  StateMessage,
  TunerMessage,
} from "./protocol"

export type EngineKind = "helper" | "demo"

export type ConnectionStatus =
  | "connecting" // trying to reach the engine
  | "connected" // live
  | "offline" // unreachable (helper not running / demo unsupported)

export type Unsubscribe = () => void

export interface Engine {
  readonly kind: EngineKind

  start(): void
  stop(): void

  /** Fire-and-forget command (state broadcasts come back via onState). */
  send(cmd: ClientCommand): void
  /** Throttled param set — knob drags coalesce to one send per ~33 ms. */
  setParam(id: ParamId, value: number): void
  /** Throttled pedal-param set — same coalescing, keyed per pedal+field. */
  setPedalParam(pedal: PedalType, field: string, value: number): void

  onStatus(cb: (status: ConnectionStatus) => void): Unsubscribe
  onState(cb: (state: StateMessage) => void): Unsubscribe
  onError(cb: (message: string) => void): Unsubscribe

  // Fast streams (~25 Hz). Subscribers update the DOM imperatively
  // (refs / CSS vars) — these must never route through React state.
  onMeters(cb: (m: MetersMessage) => void): Unsubscribe
  onTuner(cb: (t: TunerMessage) => void): Unsubscribe
}

/** Tiny typed pub-sub used by engine implementations. */
export class Emitter<T> {
  private listeners = new Set<(v: T) => void>()
  subscribe(cb: (v: T) => void): Unsubscribe {
    this.listeners.add(cb)
    return () => this.listeners.delete(cb)
  }
  emit(v: T) {
    for (const cb of this.listeners) cb(v)
  }
}
