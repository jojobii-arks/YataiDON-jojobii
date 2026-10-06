import { RefreshCw, ScanSearch } from "lucide-react"
import { useEffect, useState } from "react"
import { FEATURES, getFeature } from "../../features/registry"
import { Button } from "../ui/button"
import { Separator } from "../ui/separator"
import { applyTheme, getStoredTheme, THEMES, type ThemeId } from "../../lib/theme"
import { formatTime } from "../../lib/utils"
import { useProgressStore } from "../../stores/progress"
import { cn } from "../../lib/utils"

export function AppShell() {
  const [featureId, setFeatureId] = useState("summary")
  const [theme, setTheme] = useState<ThemeId>(() => getStoredTheme())
  const status = useProgressStore((s) => s.status)
  const rescanning = useProgressStore((s) => s.rescanning)
  const refresh = useProgressStore((s) => s.refresh)
  const rescan = useProgressStore((s) => s.rescan)
  const feature = getFeature(featureId)
  const Feature = feature.component

  useEffect(() => {
    applyTheme(theme)
  }, [theme])

  return (
    <div className="flex min-h-dvh flex-col bg-[var(--color-canvas)] text-[var(--color-ink)]">
      <div className="mx-auto flex w-full max-w-[1100px] flex-1 flex-col gap-0 px-3 py-3 sm:px-5 sm:py-5">
        <header className="mb-4 flex flex-wrap items-center justify-between gap-3">
          <div className="min-w-0">
            <div className="text-[11px] font-semibold uppercase tracking-[0.08em] text-[var(--color-muted)]">
              YataiDON
            </div>
            <div className="truncate text-[22px] font-normal tracking-[-0.02em]">
              taiko-stats
            </div>
          </div>
          <div className="flex flex-wrap items-center gap-2">
            <div className="flex rounded-[var(--radius-md)] border border-[var(--color-hairline)] bg-[var(--color-surface-card)] p-0.5">
              {THEMES.map((t) => (
                <button
                  key={t.id}
                  type="button"
                  onClick={() => setTheme(t.id)}
                  className={cn(
                    "rounded-[6px] px-2.5 py-1.5 text-[12px] font-medium transition-colors",
                    theme === t.id
                      ? "bg-[var(--color-ink)] text-[var(--color-canvas)]"
                      : "text-[var(--color-muted)] hover:text-[var(--color-ink)]",
                  )}
                >
                  {t.label}
                </button>
              ))}
            </div>
            <Button
              variant="secondary"
              size="sm"
              onClick={() => void refresh()}
              title="Refresh scores"
            >
              <RefreshCw className="h-3.5 w-3.5" />
              Refresh
            </Button>
            <Button
              variant="primary"
              size="sm"
              disabled={rescanning}
              onClick={() => void rescan()}
              title="Rescan TJA library"
            >
              <ScanSearch className="h-3.5 w-3.5" />
              {rescanning ? "Scanning…" : "Rescan"}
            </Button>
          </div>
        </header>

        <div className="flex flex-1 flex-col gap-4 lg:flex-row">
          <nav className="flex shrink-0 gap-1 overflow-x-auto lg:w-44 lg:flex-col lg:overflow-visible">
            {FEATURES.map((f) => (
              <button
                key={f.id}
                type="button"
                onClick={() => setFeatureId(f.id)}
                className={cn(
                  "rounded-[var(--radius-md)] px-3 py-2 text-left text-sm transition-colors",
                  featureId === f.id
                    ? "bg-[var(--color-surface-card)] text-[var(--color-ink)] border border-[var(--color-hairline)]"
                    : "text-[var(--color-muted)] hover:text-[var(--color-ink)] border border-transparent",
                )}
              >
                <div className="font-medium">{f.label}</div>
                <div className="mt-0.5 hidden text-[11px] leading-snug text-[var(--color-muted)] lg:block">
                  {f.description}
                </div>
              </button>
            ))}
          </nav>

          <main className="min-w-0 flex-1">
            <Feature />
          </main>
        </div>

        <Separator className="mt-5 mb-3" />

        <footer className="flex flex-col gap-1 text-[12px] text-[var(--color-muted)] sm:flex-row sm:flex-wrap sm:items-center sm:justify-between">
          <div className="min-w-0 truncate font-mono">
            {status?.config.scoresDb ?? "scores.db —"}
          </div>
          <div className="flex flex-wrap gap-x-3 gap-y-1">
            <span>
              Catalog{" "}
              <span className="text-[var(--color-body)]">
                {status?.catalogCharts ?? 0} charts
              </span>
            </span>
            <span>
              Scan{" "}
              <span className="text-[var(--color-body)]">
                {formatTime(status?.lastCatalogScanAt)}
              </span>
            </span>
            <span>
              DB{" "}
              <span className="text-[var(--color-body)]">
                {status?.scoresDbExists ? "ok" : "missing"}
              </span>
            </span>
          </div>
        </footer>
      </div>
    </div>
  )
}
