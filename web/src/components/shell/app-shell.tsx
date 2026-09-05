// Layout: header (always), then either the offline card or the input gate +
// tabbed views. Active tab persists across reloads.

import { useState } from "react"
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs"
import { useEngineStore } from "@/engine/store"
import { PlayView } from "@/views/play-view"
import { PracticeView } from "@/views/practice-view"
import { SettingsView } from "@/views/settings-view"
import { DemoFunnelBanner } from "./demo-funnel-banner"
import { EnableBanner } from "./enable-banner"
import { Header } from "./header"
import { MixerFooter } from "./mixer-footer"
import { OfflineCard } from "./offline-card"
import { PairingCard } from "./pairing-card"
import { UpdateBanner } from "./update-banner"

const TAB_KEY = "riffamp:tab"

export function AppShell() {
  const status = useEngineStore((s) => s.status)
  const isDemo = useEngineStore((s) => s.kind === "demo")
  const needsPairing = useEngineStore((s) => s.pairing.needed)
  const [tab, setTab] = useState(() => localStorage.getItem(TAB_KEY) ?? "play")

  const selectTab = (value: string) => {
    setTab(value)
    localStorage.setItem(TAB_KEY, value)
  }

  return (
    <div className="mx-auto flex min-h-dvh max-w-4xl flex-col">
      {isDemo && <DemoFunnelBanner />}
      <Header />
      {needsPairing ? (
        <PairingCard />
      ) : status !== "connected" ? (
        <OfflineCard />
      ) : (
        <>
          <UpdateBanner />
          <EnableBanner />
          <main className="flex flex-1 flex-col gap-4 p-4">
            <Tabs value={tab} onValueChange={selectTab}>
              <TabsList className="grid w-full grid-cols-3">
                <TabsTrigger value="play">Play</TabsTrigger>
                <TabsTrigger value="practice">Practice</TabsTrigger>
                <TabsTrigger value="settings">Settings</TabsTrigger>
              </TabsList>
              <TabsContent value="play">
                <PlayView />
              </TabsContent>
              <TabsContent value="practice">
                <PracticeView />
              </TabsContent>
              <TabsContent value="settings">
                <SettingsView />
              </TabsContent>
            </Tabs>
          </main>
          <MixerFooter />
        </>
      )}
    </div>
  )
}
