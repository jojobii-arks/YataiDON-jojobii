import type { CSSProperties } from "react"
import type { CourseFilter, TierOption } from "../../../shared/types"
import { COURSE_LABELS } from "../../../shared/types"
import { ToggleGroup, ToggleGroupItem } from "../../components/ui/toggle-group"
import { useFilterStore } from "../../stores/filters"
import { useProgressStore } from "../../stores/progress"
import { cn } from "../../lib/utils"

const COURSES: CourseFilter[] = [0, 1, 2, 3, 4, "oni_ura"]

const COURSE_ACCENT: Record<string, string> = {
  "0": "var(--course-easy)",
  "1": "var(--course-normal)",
  "2": "var(--course-hard)",
  "3": "var(--course-oni)",
  "4": "var(--course-ura)",
  oni_ura: "var(--course-oni)",
}

function groupTiersByLevel(tiers: TierOption[]): Array<{
  level: number
  options: TierOption[]
}> {
  const map = new Map<number, TierOption[]>()
  for (const opt of tiers) {
    const list = map.get(opt.level) ?? []
    list.push(opt)
    map.set(opt.level, list)
  }
  return [...map.entries()]
    .sort(([a], [b]) => a - b)
    .map(([level, options]) => ({ level, options }))
}

export function FilterBar() {
  const { course, star, tier, setCourse, setStar, setLevelTier } = useFilterStore()
  const tiers = useProgressStore((s) => s.tiers)
  const tierEnabled = course === "oni_ura" || course === 3 || course === 4

  const groups = groupTiersByLevel(tiers).filter(
    (g) => star == null || g.level === star,
  )

  return (
    <div className="flex flex-col gap-3">
      <section className="flex flex-col gap-2">
        <Label>Difficulty</Label>
        <ToggleGroup
          type="single"
          value={String(course)}
          onValueChange={(v) => {
            if (!v) return
            if (v === "oni_ura") setCourse("oni_ura")
            else setCourse(Number(v) as CourseFilter)
          }}
          className="gap-1"
        >
          {COURSES.map((c) => (
            <ToggleGroupItem
              key={String(c)}
              value={String(c)}
              className="min-w-[3.25rem] data-[state=on]:!bg-[var(--chip-bg)] data-[state=on]:!border-[var(--chip-bg)] data-[state=on]:!text-white"
              style={
                {
                  "--chip-bg": COURSE_ACCENT[String(c)],
                } as CSSProperties
              }
            >
              {COURSE_LABELS[c]}
            </ToggleGroupItem>
          ))}
        </ToggleGroup>
      </section>

      <section className="flex flex-col gap-2">
        <Label>Level</Label>
        <ToggleGroup
          type="single"
          value={star == null ? "all" : String(star)}
          onValueChange={(v) => {
            if (!v) return
            setStar(v === "all" ? null : Number(v))
          }}
          className="gap-1"
        >
          <ToggleGroupItem value="all" className="min-w-[2.75rem]">
            All
          </ToggleGroupItem>
          {Array.from({ length: 10 }, (_, i) => i + 1).map((n) => (
            <ToggleGroupItem key={n} value={String(n)} className="min-w-[2rem] px-2">
              ★{n}
            </ToggleGroupItem>
          ))}
        </ToggleGroup>
      </section>

      <section className="flex flex-col gap-2">
        <Label>Tier list</Label>
        {!tierEnabled ? (
          <p className="text-[12px] text-[var(--color-muted)]">
            Community clear tiers are Oni / Ura only (per ★ letter).
          </p>
        ) : groups.length === 0 ? (
          <p className="text-[12px] text-[var(--color-muted)]">
            {star != null && star < 6
              ? `No community tiers for ★${star} (list covers ★6–★10).`
              : "No tier entries for this filter."}
          </p>
        ) : (
          <div className="flex flex-col gap-2">
            {groups.map(({ level, options }) => {
              const activeLetter = tier?.startsWith(`${level}:`)
                ? tier.slice(`${level}:`.length)
                : null
              // "All" on a row = this ★ is selected and no letter filter
              const rowAllSelected = star === level && tier == null

              return (
                <div
                  key={level}
                  className="rounded-[var(--radius-md)] border border-[var(--color-hairline)] bg-[var(--color-surface-card)] px-2.5 py-2"
                >
                  <div className="mb-1.5 flex items-center justify-between gap-2">
                    <span className="text-[12px] font-semibold text-[var(--color-ink)]">
                      ★{level}
                    </span>
                    <span className="font-mono text-[11px] text-[var(--color-muted)]">
                      {options.reduce((n, o) => n + o.count, 0)} charts
                    </span>
                  </div>
                  <div className="flex flex-wrap gap-1">
                    <TierChip
                      selected={rowAllSelected}
                      onClick={() => setLevelTier(level, null)}
                      label="All"
                    />
                    {options.map((opt) => (
                      <TierChip
                        key={opt.id}
                        selected={activeLetter === opt.letter}
                        onClick={() => setLevelTier(level, opt.letter)}
                        label={opt.letter}
                        count={opt.count}
                      />
                    ))}
                  </div>
                </div>
              )
            })}
            {star == null && tier == null && (
              <p className="text-[11px] text-[var(--color-muted)]">
                Pick a letter under a ★, or tap All on a row to scope that level.
              </p>
            )}
          </div>
        )}
      </section>
    </div>
  )
}

function TierChip({
  selected,
  onClick,
  label,
  count,
}: {
  selected: boolean
  onClick: () => void
  label: string
  count?: number
}) {
  return (
    <button
      type="button"
      onClick={onClick}
      className={cn(
        "inline-flex h-8 items-center gap-1.5 rounded-[var(--radius-md)] border px-2.5 text-[12px] font-medium transition-colors",
        selected
          ? "border-[var(--color-ink)] bg-[var(--color-ink)] text-[var(--color-canvas)]"
          : "border-[var(--color-hairline)] bg-[var(--color-canvas-soft)] text-[var(--color-body)] hover:border-[var(--color-hairline-strong)] hover:text-[var(--color-ink)]",
      )}
    >
      <span>{label}</span>
      {typeof count === "number" && (
        <span
          className={cn(
            "font-mono text-[10px]",
            selected ? "text-[var(--color-canvas)]/70" : "text-[var(--color-muted)]",
          )}
        >
          {count}
        </span>
      )}
    </button>
  )
}

function Label({ children }: { children: React.ReactNode }) {
  return (
    <div className="text-[11px] font-semibold uppercase tracking-[0.08em] text-[var(--color-muted)]">
      {children}
    </div>
  )
}
