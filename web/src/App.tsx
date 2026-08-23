import { ThemeProvider } from "@/components/theme-provider"
import { AppShell } from "@/components/shell/app-shell"
import { Playground } from "@/components/dev/playground"
import { EngineProvider } from "@/engine/engine-provider"

// ?dev renders the control playground instead of the app (dev-only surface).
const devMode = new URLSearchParams(window.location.search).has("dev")

export default function App() {
  return (
    <ThemeProvider defaultTheme="dark" storageKey="riffamp:theme">
      <EngineProvider>{devMode ? <Playground /> : <AppShell />}</EngineProvider>
    </ThemeProvider>
  )
}
