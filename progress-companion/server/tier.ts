import fs from "node:fs"
import { normalizeKey } from "./catalog.js"

export interface TierEntry {
  romaji: string
  keys: string[]
  difficulty: "oni" | "ura"
  tier: string
  order: number
  level: number
}

export interface TierData {
  levels: number[]
  /** First-seen order of letters in the artifact */
  tiers: string[]
  entries: TierEntry[]
  /** normalize(key) -> level -> { oni?, ura? } */
  byKey: Map<string, Map<number, { oni?: string; ura?: string }>>
}

const TIER_SORT_FALLBACK = [
  "SSS",
  "SS",
  "S+",
  "S",
  "A",
  "B",
  "C",
  "D",
  "E",
  "F",
  "X",
]

export function loadTierData(filePath: string): TierData | null {
  if (!fs.existsSync(filePath)) return null
  try {
    const raw = JSON.parse(fs.readFileSync(filePath, "utf8")) as {
      levels?: number[]
      tiers?: string[]
      entries?: Array<{
        romaji?: string
        keys?: string[]
        difficulty?: string
        tier?: string
        order?: number
        level?: number
      }>
    }

    const entries: TierEntry[] = []
    const byKey = new Map<string, Map<number, { oni?: string; ura?: string }>>()

    const apply = (key: string, entry: TierEntry) => {
      const nk = normalizeKey(key)
      if (!nk) return
      let levels = byKey.get(nk)
      if (!levels) {
        levels = new Map()
        byKey.set(nk, levels)
      }
      let slot = levels.get(entry.level)
      if (!slot) {
        slot = {}
        levels.set(entry.level, slot)
      }
      if (entry.difficulty === "ura") slot.ura = entry.tier
      else slot.oni = entry.tier
    }

    for (const e of raw.entries ?? []) {
      if (!e.tier || !e.level || !e.difficulty) continue
      const difficulty = e.difficulty === "ura" ? "ura" : "oni"
      const entry: TierEntry = {
        romaji: e.romaji ?? "",
        keys: Array.isArray(e.keys) ? e.keys.map(String) : [],
        difficulty,
        tier: e.tier,
        order: e.order ?? 0,
        level: e.level,
      }
      entries.push(entry)
      apply(entry.romaji, entry)
      for (const k of entry.keys) apply(k, entry)
    }

    return {
      levels: raw.levels?.length ? raw.levels : [6, 7, 8, 9, 10],
      tiers: raw.tiers?.length ? raw.tiers : [...TIER_SORT_FALLBACK],
      entries,
      byKey,
    }
  } catch {
    return null
  }
}

export function lookupTier(
  data: TierData,
  keys: string[],
  course: number,
  level: number,
): string {
  for (const key of keys) {
    const levels = data.byKey.get(normalizeKey(key))
    if (!levels) continue
    const slot = levels.get(level)
    if (!slot) continue
    if (course === 4 && slot.ura) return slot.ura
    if (course === 3 && slot.oni) return slot.oni
  }
  return ""
}

/**
 * Letters that appear at a given ★ level.
 * Preserve first-seen order from the artifact (wiki chart order), matching
 * `TierListManager::tiers_for_level` — each ★ has its own letter scale.
 */
export function tiersForLevel(data: TierData, level: number): string[] {
  const seen = new Set<string>()
  const letters: string[] = []
  for (const e of data.entries) {
    if (e.level !== level) continue
    if (seen.has(e.tier)) continue
    seen.add(e.tier)
    letters.push(e.tier)
  }
  return letters
}
