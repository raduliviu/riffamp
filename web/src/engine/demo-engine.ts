// DemoEngine: the in-browser WASM amp (P4c). Stub for now — reports offline
// so the app's engine-switching logic can be built and tested before the
// WASM implementation lands.

import { Emitter } from "./engine"
import type { ConnectionStatus, Engine, Unsubscribe } from "./engine"
import type {
  ClientCommand,
  MetersMessage,
  ParamId,
  PedalType,
  StateMessage,
  TunerMessage,
} from "./protocol"

export class DemoEngine implements Engine {
  readonly kind = "demo" as const

  private status$ = new Emitter<ConnectionStatus>()

  start() {
    this.status$.emit("offline") // not implemented yet
  }
  stop() {}

  send(_cmd: ClientCommand) {}
  setParam(_id: ParamId, _value: number) {}
  setPedalParam(_pedal: PedalType, _field: string, _value: number) {}

  onStatus(cb: (s: ConnectionStatus) => void): Unsubscribe {
    return this.status$.subscribe(cb)
  }
  onState(_cb: (s: StateMessage) => void): Unsubscribe {
    return () => {}
  }
  onError(_cb: (m: string) => void): Unsubscribe {
    return () => {}
  }
  onMeters(_cb: (m: MetersMessage) => void): Unsubscribe {
    return () => {}
  }
  onTuner(_cb: (t: TunerMessage) => void): Unsubscribe {
    return () => {}
  }
}
