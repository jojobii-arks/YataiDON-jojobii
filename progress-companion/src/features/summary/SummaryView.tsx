import { FilterBar } from "./FilterBar"
import { ProgressPadCard } from "./ProgressPad"
import { useProgressStore } from "../../stores/progress"
import { Badge } from "../../components/ui/badge"
import { formatTime } from "../../lib/utils"

export function SummaryView() {
  const pad = useProgressStore((s) => s.pad)
  const status = useProgressStore((s) => s.status)
  const loading = useProgressStore((s) => s.loading)
  const error = useProgressStore((s) => s.error)

  return (
    <div className="flex flex-col gap-4">
      <header className="flex flex-wrap items-end justify-between gap-3">
        <div>
          <div className="text-[11px] font-semibold uppercase tracking-[0.08em] text-[var(--color-muted)]">
            Library totals
          </div>
          <h1 className="mt-1 text-[26px] font-normal leading-tight tracking-[-0.02em] text-[var(--color-ink)]">
            Summary
          </h1>
          <p className="mt-1 max-w-xl text-sm text-[var(--color-body)]">
            Exclusive best-rank buckets and Clear / FC / DFC crowns — same pad math as in-game.
          </p>
        </div>
        <div className="flex flex-wrap items-center gap-2">
          {status?.watching && <Badge>Live</Badge>}
          {loading && (
            <span className="text-[12px] text-[var(--color-muted)]">Refreshing…</span>
          )}
          <span className="font-mono text-[12px] text-[var(--color-muted)]">
            {formatTime(pad?.lastRefreshAt ?? status?.lastScoresRefreshAt)}
          </span>
        </div>
      </header>

      <FilterBar />

      {error && (
        <div className="rounded-[var(--radius-md)] border border-[var(--color-semantic-error)]/30 bg-[var(--color-semantic-error)]/8 px-3 py-2 text-sm text-[var(--color-semantic-error)]">
          {error}
        </div>
      )}

      {!status?.catalogReady && (
        <div className="rounded-[var(--radius-md)] border border-dashed border-[var(--color-hairline-strong)] bg-[var(--color-canvas-soft)] px-4 py-3 text-sm text-[var(--color-body)]">
          No song catalog yet. Hit <strong className="text-[var(--color-ink)]">Rescan library</strong> in the footer to index TJAs (needed for ★ / tier filters).
        </div>
      )}

      {pad ? (
        <ProgressPadCard pad={pad} />
      ) : (
        <div className="rounded-[var(--radius-lg)] border border-[var(--color-hairline)] bg-[var(--color-surface-card)] px-5 py-10 text-center text-sm text-[var(--color-muted)]">
          Waiting for progress data…
        </div>
      )}
    </div>
  )
}
