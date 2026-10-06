import { Badge } from "../components/ui/badge"
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "../components/ui/card"

export function PlaceholderView({
  title,
  body,
}: {
  title: string
  body: string
}) {
  return (
    <Card className="max-w-xl">
      <CardHeader>
        <div className="flex items-center gap-2">
          <CardTitle>{title}</CardTitle>
          <Badge>Soon</Badge>
        </div>
        <CardDescription>{body}</CardDescription>
      </CardHeader>
      <CardContent>
        <div className="rounded-[var(--radius-md)] border border-dashed border-[var(--color-hairline-strong)] bg-[var(--color-canvas-soft)] px-4 py-8 text-center text-sm text-[var(--color-muted)]">
          Register new views in{" "}
          <code className="font-mono text-[12px]">src/features/registry.ts</code>
        </div>
      </CardContent>
    </Card>
  )
}

export function SessionPlaceholder() {
  return (
    <PlaceholderView
      title="Session deltas"
      body="Track how many clears / FCs / ranks you’ve picked up since opening the companion. Wired as a stub so the shell stays expandable."
    />
  )
}
