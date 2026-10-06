import type {
  CourseFilter,
  CrownId,
  ProgressFilters,
  ProgressPad,
  RankId,
  TierOption,
} from "../shared/types.js"
import type { Catalog, CatalogChart } from "./catalog.js"
import { emptyCrowns, emptyRanks, type BestScore } from "./scores.js"
import {
  lookupTier,
  tiersForLevel,
  type TierData,
} from "./tier.js"

function courseMatches(filter: CourseFilter, course: number): boolean {
  if (filter === "oni_ura") return course === 3 || course === 4
  return course === filter
}

function chartPasses(
  chart: CatalogChart,
  filters: ProgressFilters,
  tierData: TierData | null,
): boolean {
  if (!courseMatches(filters.course, chart.course)) return false
  if (filters.star != null && chart.level !== filters.star) return false

  if (filters.tier) {
    if (!tierData) return false
    const [levelStr, letter] = filters.tier.split(":")
    const level = Number(levelStr)
    if (!letter || Number.isNaN(level)) return false
    if (chart.level !== level) return false
    // Tier list only ranks Oni/Ura
    if (chart.course !== 3 && chart.course !== 4) return false
    const found = lookupTier(tierData, chart.keys, chart.course, chart.level)
    if (found !== letter) return false
  }

  return true
}

export function aggregatePad(
  catalog: Catalog | null,
  bestScores: Map<string, BestScore>,
  filters: ProgressFilters,
  tierData: TierData | null,
): ProgressPad {
  const ranks = emptyRanks()
  const crowns = emptyCrowns()
  let total = 0
  let scored = 0

  // Library total counts every chart file (same as the game).
  // Rank/crown buckets count each (hash, course) once: title→hash matching can
  // attach the same songs-table hash to Nijisanji / duplicate TJAs whose real
  // content hashes differ, which would inflate Clear/FC vs the in-game pad.
  const charts = catalog?.charts ?? []
  const seenScore = new Set<string>()
  for (const chart of charts) {
    if (!chartPasses(chart, filters, tierData)) continue
    total++

    if (!chart.hash) continue
    const key = `${chart.hash}|${chart.course}`
    if (seenScore.has(key)) continue
    seenScore.add(key)

    const best = bestScores.get(key)
    if (!best) continue
    scored++

    const r = best.rank as RankId
    if (r >= 1 && r <= 7) ranks[r]++

    const c = best.crown as CrownId
    if (c >= 1 && c <= 3) crowns[c]++
  }

  return {
    total,
    scored,
    ranks,
    crowns,
    clears: crowns[1] + crowns[2] + crowns[3],
    fullCombos: crowns[2] + crowns[3],
    dfcs: crowns[3],
    filters,
    catalogCharts: charts.length,
    lastRefreshAt: new Date().toISOString(),
  }
}

export function buildTierOptions(
  catalog: Catalog | null,
  tierData: TierData | null,
  course: CourseFilter,
  star: number | null,
): TierOption[] {
  if (!tierData) return []

  const levels =
    star != null
      ? tierData.levels.filter((l) => l === star)
      : [...tierData.levels]

  const counts = new Map<string, number>()
  for (const chart of catalog?.charts ?? []) {
    if (!courseMatches(course, chart.course)) continue
    if (star != null && chart.level !== star) continue
    if (chart.course !== 3 && chart.course !== 4) continue
    const letter = lookupTier(tierData, chart.keys, chart.course, chart.level)
    if (!letter) continue
    const id = `${chart.level}:${letter}`
    counts.set(id, (counts.get(id) ?? 0) + 1)
  }

  const options: TierOption[] = []
  for (const level of levels) {
    for (const letter of tiersForLevel(tierData, level)) {
      const id = `${level}:${letter}`
      options.push({
        id,
        level,
        letter,
        label: `★${level} · ${letter}`,
        count: counts.get(id) ?? 0,
      })
    }
  }

  return options
}
