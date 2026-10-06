import type { CrownId, ProgressPad, RankId } from "../../../shared/types"
import { CROWN_LABELS, RANK_LABELS } from "../../../shared/types"
import { cn } from "../../lib/utils"

const RANK_COLORS: Record<RankId, string> = {
  1: "var(--rank-white)",
  2: "var(--rank-bronze)",
  3: "var(--rank-silver)",
  4: "var(--rank-gold)",
  5: "var(--rank-pink)",
  6: "var(--rank-purple)",
  7: "var(--rank-rainbow)",
}

const CROWN_COLORS: Record<CrownId, string> = {
  1: "var(--crown-clear)",
  2: "var(--crown-fc)",
  3: "var(--crown-dfc)",
}

export function ProgressPadCard({ pad }: { pad: ProgressPad }) {
  const ranks = ([1, 2, 3, 4, 5, 6, 7] as RankId[]).map((id) => ({
    id,
    label: RANK_LABELS[id],
    count: pad.ranks[id] ?? 0,
  }))
  const crowns = ([1, 2, 3] as CrownId[]).map((id) => ({
    id,
    label: CROWN_LABELS[id],
    count: pad.crowns[id] ?? 0,
  }))

  return (
    <div className="overflow-hidden rounded-[var(--radius-lg)] border border-[var(--color-hairline)] bg-[var(--color-surface-card)]">
      <div className="flex flex-wrap items-end justify-between gap-3 border-b border-[var(--color-hairline)] px-5 py-4">
        <div>
          <div className="text-[11px] font-semibold uppercase tracking-[0.08em] text-[var(--color-muted)]">
            Progress pad
          </div>
          <div className="mt-1 flex items-baseline gap-2">
            <span className="text-[36px] font-normal leading-none tracking-[-0.02em] text-[var(--color-ink)]">
              {pad.total}
            </span>
            <span className="text-sm text-[var(--color-muted)]">charts</span>
          </div>
        </div>
        <div className="flex flex-wrap gap-3 text-[12px] text-[var(--color-body)]">
          <Stat label="Scored" value={pad.scored} />
          <Stat label="Clear" value={pad.crowns[1] ?? 0} />
          <Stat label="FC" value={pad.crowns[2] ?? 0} />
          <Stat label="DFC" value={pad.crowns[3] ?? 0} />
        </div>
      </div>

      <div className="grid gap-5 p-5 sm:grid-cols-[1.4fr_1fr]">
        <div>
          <SectionLabel>Ranks</SectionLabel>
          <div className="mt-2 grid grid-cols-2 gap-2 xs:grid-cols-3 sm:grid-cols-3 lg:grid-cols-4">
            {ranks.map((r) => (
              <MetricChip
                key={r.id}
                label={r.label}
                count={r.count}
                swatch={RANK_COLORS[r.id]}
                rainbow={r.id === 7}
              />
            ))}
          </div>
        </div>
        <div>
          <SectionLabel>Crowns</SectionLabel>
          <div className="mt-2 grid grid-cols-3 gap-2">
            {crowns.map((c) => (
              <MetricChip
                key={c.id}
                label={c.label}
                count={c.count}
                swatch={CROWN_COLORS[c.id]}
              />
            ))}
          </div>
        </div>
      </div>
    </div>
  )
}

function SectionLabel({ children }: { children: React.ReactNode }) {
  return (
    <div className="text-[11px] font-semibold uppercase tracking-[0.08em] text-[var(--color-muted)]">
      {children}
    </div>
  )
}

function Stat({ label, value }: { label: string; value: number }) {
  return (
    <div className="flex items-baseline gap-1.5">
      <span className="text-[var(--color-muted)]">{label}</span>
      <span className="font-mono text-[13px] text-[var(--color-ink)]">{value}</span>
    </div>
  )
}

function MetricChip({
  label,
  count,
  swatch,
  rainbow,
}: {
  label: string
  count: number
  swatch: string
  rainbow?: boolean
}) {
  return (
    <div
      className={cn(
        "flex items-center gap-2.5 rounded-[var(--radius-md)] border border-[var(--color-hairline)] bg-[var(--color-canvas-soft)] px-2.5 py-2",
      )}
    >
      <span
        className="h-3.5 w-3.5 shrink-0 rounded-full border border-[var(--color-hairline-strong)]"
        style={
          rainbow
            ? { backgroundImage: swatch }
            : { background: swatch }
        }
        aria-hidden
      />
      <div className="min-w-0 flex-1">
        <div className="truncate text-[11px] text-[var(--color-muted)]">{label}</div>
        <div className="font-mono text-[16px] leading-tight text-[var(--color-ink)]">
          {count}
        </div>
      </div>
    </div>
  )
}
