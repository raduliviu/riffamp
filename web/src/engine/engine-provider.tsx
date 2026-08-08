import { useEffect, useMemo } from "react"
import type { ReactNode } from "react"
import type { Engine } from "./engine"
import { HelperEngine } from "./helper-engine"
import { bindEngineToStore } from "./store"
import { EngineContext } from "./use-engine"

export function EngineProvider({ children }: { children: ReactNode }) {
  // Helper-only for now; the demo engine joins the selection logic in P4c.
  const engine = useMemo<Engine>(() => new HelperEngine(), [])

  useEffect(() => {
    const unbind = bindEngineToStore(engine)
    engine.start()
    return () => {
      unbind()
      engine.stop()
    }
  }, [engine])

  return <EngineContext.Provider value={engine}>{children}</EngineContext.Provider>
}
