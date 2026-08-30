import { useEffect, useState } from "react"
import type { ReactNode } from "react"
import { DemoStartGate } from "@/components/shell/demo-start-gate"
import type { Engine } from "./engine"
import { HelperEngine } from "./helper-engine"
import { bindEngineToStore, useEngineStore } from "./store"
import { engineOverride, isLocalOrigin, probeHelper } from "./select-engine"
import { EngineContext } from "./use-engine"

// Hosted / dev provider: prefer the native helper if it's reachable, otherwise
// fall back to the in-browser WASM demo. The demo engine is dynamically
// imported so its wasm payload only loads when actually chosen, and it starts
// behind a click (browsers need a user gesture to open audio + prompt the mic).
export function DemoAwareProvider({ children }: { children: ReactNode }) {
  const [engine, setEngine] = useState<Engine | null>(null)
  const [needsGesture, setNeedsGesture] = useState(false)

  useEffect(() => {
    let live = true
    let current: Engine | null = null
    let unbind = () => {}

    const bind = (e: Engine) => {
      current = e
      useEngineStore.setState({ kind: e.kind })
      unbind = bindEngineToStore(e)
    }

    const decide = async () => {
      const override = engineOverride()
      const choice =
        override ??
        (isLocalOrigin() && (await probeHelper()) ? "helper" : "demo")
      if (!live) return
      if (choice === "helper") {
        const e = new HelperEngine()
        bind(e)
        e.start()
        setEngine(e)
      } else {
        const { DemoEngine } = await import("./demo/demo-engine")
        if (!live) return
        const e = new DemoEngine()
        bind(e)
        setEngine(e)
        setNeedsGesture(true) // wait for the start click before opening audio
      }
    }
    void decide()

    return () => {
      live = false
      unbind()
      current?.stop()
    }
  }, [])

  if (!engine) return <Booting />
  return (
    <EngineContext.Provider value={engine}>
      {needsGesture ? (
        <DemoStartGate
          onStart={() => {
            engine.start()
            setNeedsGesture(false)
          }}
        />
      ) : (
        children
      )}
    </EngineContext.Provider>
  )
}

function Booting() {
  return (
    <div className="flex min-h-svh items-center justify-center text-sm text-muted-foreground">
      Starting RiffAmp…
    </div>
  )
}
