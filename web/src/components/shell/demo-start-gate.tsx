// The demo's entry screen. Starting the browser audio engine requires a user
// gesture (an AudioContext can't resume, and the mic can't prompt, without
// one), so the demo waits behind this button. Shown only on the hosted /app
// demo path — never in the native build.

import { Button } from "@/components/ui/button"

// Chromium exposes AudioContext.setSinkId (output-device routing); Firefox and
// Safari don't, and their WebAudio capture is flakier for this use — so the
// demo is best on Chrome/Edge.
function isChromium(): boolean {
  try {
    return (
      typeof AudioContext !== "undefined" && "setSinkId" in AudioContext.prototype
    )
  } catch {
    return false
  }
}

export function DemoStartGate({ onStart }: { onStart: () => void }) {
  const chromium = isChromium()
  return (
    <div className="mx-auto flex min-h-dvh max-w-md flex-col items-center justify-center gap-6 px-6 text-center">
      <div className="text-lg font-extrabold tracking-[0.2em]">
        RIFF<span className="text-primary">AMP</span>
      </div>
      <div>
        <h1 className="text-2xl font-bold">Browser demo</h1>
        <p className="mt-3 text-sm leading-relaxed text-muted-foreground">
          Play your guitar through a real neural amp capture and cab, right in
          your browser. Plug into your audio interface, then start — we'll ask
          for microphone access so RiffAmp can hear your input.
        </p>
      </div>
      {!chromium && (
        <p className="w-full rounded-md border border-amber-500/50 bg-amber-500/10 px-3 py-2 text-xs text-amber-500">
          For the full demo, use <strong>Chrome</strong> or{" "}
          <strong>Edge</strong> — some audio features (like choosing your output
          device) only work in a Chromium browser.
        </p>
      )}
      <Button size="lg" className="w-56" onClick={onStart}>
        ▶ Start the demo
      </Button>
      <p className="text-xs leading-relaxed text-muted-foreground">
        Chrome or Edge recommended. The browser adds audio latency — for real
        sub-10&nbsp;ms feel,{" "}
        <a className="underline hover:text-foreground" href="/#download">
          install the free helper
        </a>
        .
      </p>
    </div>
  )
}
