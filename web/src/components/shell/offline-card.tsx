// Shown instead of the app when the helper isn't reachable. Becomes the
// download-funnel card in P4e; for now it mirrors the legacy hint.

export function OfflineCard() {
  return (
    <div className="mx-auto max-w-md py-16 text-center text-muted-foreground">
      <div className="mb-2 text-lg font-semibold text-foreground">
        Engine not found
      </div>
      <p className="text-sm leading-relaxed">
        Start the RiffAmp helper on this machine and this page will connect
        automatically.
      </p>
    </div>
  )
}
