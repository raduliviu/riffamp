// The demo's entry screen. Starting the browser audio engine requires a user
// gesture (an AudioContext can't resume, and the mic can't prompt, without
// one), so the demo waits behind this button. Shown only on the hosted /app
// demo path — never in the native build.

import { Button } from "@/components/ui/button"

export function DemoStartGate({ onStart }: { onStart: () => void }) {
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
      <Button size="lg" className="w-56" onClick={onStart}>
        ▶ Start the demo
      </Button>
      <p className="text-xs leading-relaxed text-muted-foreground">
        The browser adds audio latency. For real sub-10&nbsp;ms feel,{" "}
        <a className="underline hover:text-foreground" href="/#download">
          install the free helper
        </a>
        .
      </p>
    </div>
  )
}
