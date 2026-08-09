// HelperEngine: the native helper over its loopback WebSocket.
// Ports the legacy UI's connection behavior faithfully:
//  - hello on open, auto-reconnect 2 s after close
//  - params throttled via setTimeout(33 ms), NOT requestAnimationFrame —
//    rAF suspends in background/embedded tabs and silently drops changes.

import { Emitter } from "./engine"
import type { ConnectionStatus, Engine, Unsubscribe } from "./engine"
import { HELPER_WS_URL } from "./protocol"
import type {
  ClientCommand,
  MetersMessage,
  ParamId,
  PedalType,
  ServerMessage,
  StateMessage,
  TunerMessage,
} from "./protocol"

const RECONNECT_MS = 2000
const PARAM_FLUSH_MS = 33

export class HelperEngine implements Engine {
  readonly kind = "helper" as const

  private ws: WebSocket | null = null
  private stopped = true
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null

  // Coalesced command queue, keyed so rapid knob drags collapse to one send
  // per key per flush (33 ms). Keys: `param:<id>` and `pedal:<type>:<field>`.
  private queue = new Map<string, ClientCommand>()
  private flushScheduled = false

  private status$ = new Emitter<ConnectionStatus>()
  private state$ = new Emitter<StateMessage>()
  private error$ = new Emitter<string>()
  private meters$ = new Emitter<MetersMessage>()
  private tuner$ = new Emitter<TunerMessage>()

  private url: string
  constructor(url: string = HELPER_WS_URL) {
    this.url = url
  }

  start() {
    this.stopped = false
    this.connect()
  }

  stop() {
    this.stopped = true
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer)
    this.ws?.close()
    this.ws = null
  }

  send(cmd: ClientCommand) {
    if (this.ws?.readyState === WebSocket.OPEN)
      this.ws.send(JSON.stringify(cmd))
  }

  setParam(id: ParamId, value: number) {
    this.enqueue(`param:${id}`, { type: "setParam", id, value })
  }

  setPedalParam(pedal: PedalType, field: string, value: number) {
    this.enqueue(`pedal:${pedal}:${field}`, {
      type: "setPedal",
      pedal,
      field,
      value,
    })
  }

  private enqueue(key: string, cmd: ClientCommand) {
    this.queue.set(key, cmd)
    if (this.flushScheduled) return
    this.flushScheduled = true
    setTimeout(() => {
      this.flushScheduled = false
      if (this.ws?.readyState !== WebSocket.OPEN) return
      for (const cmd of this.queue.values()) this.ws.send(JSON.stringify(cmd))
      this.queue.clear()
    }, PARAM_FLUSH_MS)
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
  onMeters(cb: (m: MetersMessage) => void): Unsubscribe {
    return this.meters$.subscribe(cb)
  }
  onTuner(cb: (t: TunerMessage) => void): Unsubscribe {
    return this.tuner$.subscribe(cb)
  }

  private connect() {
    if (this.stopped) return
    this.status$.emit("connecting")
    const ws = new WebSocket(this.url)
    this.ws = ws

    ws.onopen = () => {
      this.status$.emit("connected")
      ws.send(JSON.stringify({ type: "hello" }))
    }
    ws.onmessage = (ev) =>
      this.dispatch(JSON.parse(ev.data as string) as ServerMessage)
    ws.onerror = () => ws.close()
    ws.onclose = () => {
      if (this.ws !== ws) return // superseded by a newer socket
      this.status$.emit("offline")
      if (!this.stopped)
        this.reconnectTimer = setTimeout(() => this.connect(), RECONNECT_MS)
    }
  }

  private dispatch(msg: ServerMessage) {
    switch (msg.type) {
      case "state":
        this.state$.emit(msg)
        break
      case "meters":
        this.meters$.emit(msg)
        break
      case "tuner":
        this.tuner$.emit(msg)
        break
      case "error":
        this.error$.emit(msg.message)
        break
    }
  }
}
