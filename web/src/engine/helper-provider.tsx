import { useEffect, useMemo } from "react"
import type { ReactNode } from "react"
import type { Engine } from "./engine"
import { HelperEngine } from "./helper-engine"
import { bindEngineToStore } from "./store"
import { EngineContext } from "./use-engine"

// Helper-only provider: the native build's path (and the local UI the helper
// serves). Synchronous — no probing, unchanged from the original behavior.
export function HelperProvider({ children }: { children: ReactNode }) {
  const engine = useMemo<Engine>(() => new HelperEngine(), [])
  useEffect(() => {
    const unbind = bindEngineToStore(engine)
    engine.start()
    return () => {
      unbind()
      engine.stop()
    }
  }, [engine])
  return (
    <EngineContext.Provider value={engine}>{children}</EngineContext.Provider>
  )
}
