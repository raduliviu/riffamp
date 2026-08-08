import { ThemeProvider } from "@/components/theme-provider"
import { AppShell } from "@/components/shell/app-shell"
import { EngineProvider } from "@/engine/engine-provider"

export default function App() {
  return (
    <ThemeProvider defaultTheme="dark" storageKey="webamp:theme">
      <EngineProvider>
        <AppShell />
      </EngineProvider>
    </ThemeProvider>
  )
}
