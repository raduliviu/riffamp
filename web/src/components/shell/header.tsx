import { ConnectionChip } from "./connection-chip"
import { MiniMeters } from "./mini-meters"
import { ThemeToggle } from "./theme-toggle"

export function Header() {
  return (
    <header className="flex items-center gap-4 border-b px-4 py-2">
      <div className="text-lg font-bold tracking-widest">
        RIFF<span className="text-primary">AMP</span>
      </div>
      <ConnectionChip />
      <div className="ml-auto flex items-center gap-3">
        <MiniMeters />
        <ThemeToggle />
      </div>
    </header>
  )
}
