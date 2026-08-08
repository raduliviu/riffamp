import { EngineProvider } from "@/engine/engine-provider"
import { ConnectionProbe } from "@/components/dev/connection-probe"

export default function App() {
  return (
    <EngineProvider>
      <ConnectionProbe />
    </EngineProvider>
  )
}
