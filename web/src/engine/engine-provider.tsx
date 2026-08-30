import type { ReactNode } from "react"
import { HelperProvider } from "./helper-provider"
import { DemoAwareProvider } from "./demo-aware-provider"

// `__DEMO__` is a build-time constant. In the helper's single-file build it is
// false, so this folds to `<HelperProvider>` and Rollup drops DemoAwareProvider
// (and the WASM demo it dynamically imports) from the bundle entirely. In dev
// and the hosted /app build it is true, enabling helper-or-demo selection.
export function EngineProvider({ children }: { children: ReactNode }) {
  return __DEMO__ ? (
    <DemoAwareProvider>{children}</DemoAwareProvider>
  ) : (
    <HelperProvider>{children}</HelperProvider>
  )
}
