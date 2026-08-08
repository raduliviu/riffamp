// Context + hook for the app's Engine instance (provider lives in
// engine-provider.tsx). Views call useEngine() for commands and fast-stream
// subscriptions; useEngineStore for renderable state.

import { createContext, useContext } from "react"
import type { Engine } from "./engine"

export const EngineContext = createContext<Engine | null>(null)

export function useEngine(): Engine {
  const engine = useContext(EngineContext)
  if (!engine) throw new Error("useEngine must be used inside <EngineProvider>")
  return engine
}
