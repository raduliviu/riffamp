// Placeholder shown in the browser demo for a feature that only runs in the
// native app (drums, pedalboard, presets). Keeps the section visible — honest,
// and a gentle funnel — instead of rendering dead controls the demo can't back.

export function DemoLocked({ feature }: { feature: string }) {
  return (
    <div className="space-y-1.5 rounded-md border border-dashed border-border px-4 py-5 text-center">
      <div className="text-sm font-semibold text-foreground/80">{feature}</div>
      <div className="text-xs text-muted-foreground">
        Available in the desktop app — the browser demo covers amp tone and the
        practice tools.
      </div>
      <a
        href="https://riffamp.app/#download"
        className="inline-block pt-1 text-xs font-semibold text-emerald-500 hover:underline"
      >
        Install RiffAmp →
      </a>
    </div>
  )
}
