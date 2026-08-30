// Slim upsell shown across the top of the app while it's running on the browser
// demo engine: the demo works, but the native helper is the low-latency tier.

export function DemoFunnelBanner() {
  return (
    <div className="border-b border-primary/20 bg-primary/10 px-4 py-1.5 text-center text-xs text-foreground">
      You're on the free browser demo.{" "}
      <a
        href="/#download"
        className="font-semibold text-primary underline underline-offset-2"
      >
        Install the helper
      </a>{" "}
      for real sub-10&nbsp;ms latency.
    </div>
  )
}
