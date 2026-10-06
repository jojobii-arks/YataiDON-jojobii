/** Shared contracts between the Node API and the React UI. */

export type CourseId = 0 | 1 | 2 | 3 | 4

export type CourseFilter = CourseId | "oni_ura"

export type RankId = 1 | 2 | 3 | 4 | 5 | 6 | 7
export type CrownId = 1 | 2 | 3

export const COURSE_LABELS: Record<CourseId | "oni_ura", string> = {
  0: "Easy",
  1: "Normal",
  2: "Hard",
  3: "Oni",
  4: "Ura",
  oni_ura: "Oni+Ura",
}

export const RANK_LABELS: Record<RankId, string> = {
  1: "White",
  2: "Bronze",
  3: "Silver",
  4: "Gold",
  5: "Pink",
  6: "Purple",
  7: "Rainbow",
}

export const CROWN_LABELS: Record<CrownId, string> = {
  1: "Clear",
  2: "FC",
  3: "DFC",
}

export interface AppConfig {
  scoresDb: string
  tjaPaths: string[]
  tierDataPath: string
  catalogCachePath: string
  mergeOniUraDefault: boolean
  pollMs: number
  playerId: number
}

export interface ProgressFilters {
  course: CourseFilter
  /** null = all ★ */
  star: number | null
  /**
   * Tier filter as "level:letter" (e.g. "9:S").
   * null = no tier filter.
   */
  tier: string | null
}

export interface ProgressPad {
  total: number
  scored: number
  ranks: Record<RankId, number>
  crowns: Record<CrownId, number>
  clears: number
  fullCombos: number
  dfcs: number
  filters: ProgressFilters
  catalogCharts: number
  lastRefreshAt: string
}

export interface TierOption {
  /** "9:S" */
  id: string
  level: number
  letter: string
  label: string
  count: number
}

export interface StatusPayload {
  config: AppConfig
  catalogReady: boolean
  catalogCharts: number
  catalogSongs: number
  lastCatalogScanAt: string | null
  lastScoresRefreshAt: string | null
  watching: boolean
  scoresDbExists: boolean
  error: string | null
}

export interface FeatureMeta {
  id: string
  label: string
  description: string
  /** Future features can be registered without being implemented yet. */
  available: boolean
}
