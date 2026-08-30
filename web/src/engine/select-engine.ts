// Is the native helper reachable on its loopback WebSocket? Used by the hosted
// app to decide helper-vs-demo. A quick open/fail probe: on https origins the
// browser may block ws://127.0.0.1 (Safari always; Chrome behind the Local
// Network Access prompt) — a blocked probe simply means "use the demo".

import { HELPER_WS_URL } from "./protocol"

export function probeHelper(timeoutMs = 1500): Promise<boolean> {
  return new Promise((resolve) => {
    let ws: WebSocket
    try {
      ws = new WebSocket(HELPER_WS_URL)
    } catch {
      resolve(false)
      return
    }
    let done = false
    const finish = (ok: boolean) => {
      if (done) return
      done = true
      clearTimeout(timer)
      try {
        ws.close()
      } catch {
        /* ignore */
      }
      resolve(ok)
    }
    const timer = setTimeout(() => finish(false), timeoutMs)
    ws.onopen = () => finish(true)
    ws.onerror = () => finish(false)
  })
}

export type EngineChoice = "helper" | "demo"

/** URL override for testing: ?engine=demo | ?engine=helper. */
export function engineOverride(): EngineChoice | null {
  const v = new URLSearchParams(window.location.search).get("engine")
  return v === "demo" || v === "helper" ? v : null
}

// A local page (dev server, or the helper's own UI) can reach the helper on
// loopback with no pairing. A remote https origin (the hosted /app) needs the
// pairing handshake — until that client UI ships, remote origins use the demo.
export function isLocalOrigin(): boolean {
  const h = window.location.hostname
  return h === "localhost" || h === "127.0.0.1" || h === "[::1]" || h === ""
}
