// React-facing state: slow-changing engine state only. The fast streams
// (meters/tuner/beat) never enter this store — subscribe to them directly on
// the Engine and update the DOM imperatively.

import { create } from "zustand"
import type {
  ConnectionStatus,
  Engine,
  EngineKind,
  PairingState,
} from "./engine"
import { KNOWN_PROTOCOL_VERSION } from "./protocol"
import type { StateMessage } from "./protocol"

interface EngineStore {
  kind: EngineKind
  status: ConnectionStatus
  state: StateMessage | null
  lastError: string | null
  /** Helper speaks a different protocol generation than this UI knows. */
  versionMismatch: boolean
  /** Hosted origin awaiting a pairing code (helper engine only). */
  pairing: PairingState
  clearError: () => void
}

export const useEngineStore = create<EngineStore>((set) => ({
  kind: "helper",
  status: "connecting",
  state: null,
  lastError: null,
  versionMismatch: false,
  pairing: { needed: false, attemptsLeft: null },
  clearError: () => set({ lastError: null }),
}))

const majorMinor = (v: string) => v.split(".").slice(0, 2).join(".")

/** Pipe an Engine's slow events into the store. Returns a teardown fn. */
export function bindEngineToStore(engine: Engine): () => void {
  const unsubs = [
    engine.onStatus((status) =>
      useEngineStore.setState({ status, kind: engine.kind })
    ),
    engine.onState((state) =>
      useEngineStore.setState({
        state,
        versionMismatch:
          majorMinor(state.version) !== majorMinor(KNOWN_PROTOCOL_VERSION),
      })
    ),
    engine.onError((lastError) => useEngineStore.setState({ lastError })),
    engine.onPairing((pairing) => useEngineStore.setState({ pairing })),
  ]
  return () => unsubs.forEach((u) => u())
}
