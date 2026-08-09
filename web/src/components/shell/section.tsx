// A titled card section — the building block for the tabbed views.

import type { ReactNode } from "react"
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"

export function Section({
  title,
  children,
}: {
  title: string
  children: ReactNode
}) {
  return (
    <Card>
      <CardHeader>
        <CardTitle className="text-xs font-semibold tracking-widest text-muted-foreground">
          {title}
        </CardTitle>
      </CardHeader>
      <CardContent>{children}</CardContent>
    </Card>
  )
}
