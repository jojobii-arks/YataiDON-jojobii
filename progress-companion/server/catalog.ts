import fs from "node:fs"
import path from "node:path"
import type { CourseId } from "../shared/types.js"
import type { SongRow } from "./scores.js"

export interface CatalogChart {
  /** Stable id: folder|course|level|filename */
  id: string
  title: string
  /** Lookup keys: TITLE*, SUBTITLE*, folder name */
  keys: string[]
  folder: string
  course: CourseId
  level: number
  /** Diff hash from songs table when matched; empty if never played / unmatched */
  hash: string
  tjaPath: string
}

export interface Catalog {
  version: 1
  scannedAt: string
  roots: string[]
  charts: CatalogChart[]
}

const COURSE_ALIASES: Record<string, CourseId> = {
  "0": 0,
  easy: 0,
  "1": 1,
  normal: 1,
  "2": 2,
  hard: 2,
  "3": 3,
  oni: 3,
  "4": 4,
  edit: 4,
  ura: 4,
}

export function normalizeKey(s: string): string {
  return s.trim().toLowerCase()
}

function parseTjaHeader(filePath: string): {
  titles: string[]
  subtitles: string[]
  courses: Array<{ course: CourseId; level: number }>
} | null {
  let text: string
  try {
    text = fs.readFileSync(filePath, "utf8")
  } catch {
    try {
      text = fs.readFileSync(filePath, "latin1")
    } catch {
      return null
    }
  }

  const titles: string[] = []
  const subtitles: string[] = []
  const courses: Array<{ course: CourseId; level: number }> = []
  let current: CourseId | null = null
  let inChart = false

  for (const rawLine of text.split(/\r?\n/)) {
    const line = rawLine.trim()
    if (!line || line.startsWith("//")) continue

    const upper = line.toUpperCase()
    if (upper.startsWith("#START")) {
      inChart = true
      continue
    }
    if (upper.startsWith("#END")) {
      inChart = false
      continue
    }
    if (inChart) continue

    const colon = line.indexOf(":")
    if (colon < 0) continue
    const key = line.slice(0, colon).trim().toUpperCase()
    const value = line.slice(colon + 1).trim()

    if (key === "TITLE" || key.startsWith("TITLE")) {
      if (value && !titles.includes(value)) titles.push(value)
    } else if (key === "SUBTITLE" || key.startsWith("SUBTITLE")) {
      // Strip common "--" / "++" subtitle prefixes used in TJAs
      const cleaned = value.replace(/^[-−–—+]+\s*/, "").trim()
      if (cleaned && !subtitles.includes(cleaned)) subtitles.push(cleaned)
    } else if (key === "COURSE") {
      const alias = COURSE_ALIASES[value.toLowerCase()]
      current = alias ?? null
      if (current != null) {
        const existing = courses.find((c) => c.course === current)
        if (!existing) courses.push({ course: current, level: 0 })
      }
    } else if (key === "LEVEL" && current != null) {
      const level = Math.trunc(Number.parseFloat(value) || 0)
      const entry = courses.find((c) => c.course === current)
      if (entry) entry.level = level
    }
  }

  return { titles, subtitles, courses }
}

function walkTjas(root: string): string[] {
  const out: string[] = []
  const stack = [root]
  while (stack.length) {
    const dir = stack.pop()!
    let entries: fs.Dirent[]
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true })
    } catch {
      continue
    }
    for (const ent of entries) {
      const full = path.join(dir, ent.name)
      if (ent.isDirectory()) {
        if (ent.name === "node_modules" || ent.name.startsWith(".")) continue
        stack.push(full)
      } else if (ent.isFile() && ent.name.toLowerCase().endsWith(".tja")) {
        out.push(full)
      }
    }
  }
  return out
}

export type SongIndex = {
  byTitle: Map<string, SongRow[]>
}

/** Index songs by normalized title (and subtitle) — may have collisions. */
export function buildSongIndex(songs: SongRow[]): SongIndex {
  const byTitle = new Map<string, SongRow[]>()
  const push = (key: string, song: SongRow) => {
    const k = normalizeKey(key)
    if (!k) return
    const list = byTitle.get(k) ?? []
    if (!list.includes(song)) list.push(song)
    byTitle.set(k, list)
  }
  for (const song of songs) {
    push(song.title, song)
    if (song.subtitle) push(song.subtitle, song)
  }
  return { byTitle }
}

/**
 * Resolve the songs-table hash for a chart.
 * Handles title collisions (e.g. Phoenix vs PHOENIX) via subtitle / exact title.
 */
export function resolveChartHash(
  chart: { title: string; keys: string[]; course: number },
  index: SongIndex,
): string {
  const candidates: SongRow[] = []
  const seen = new Set<SongRow>()
  for (const key of chart.keys) {
    for (const song of index.byTitle.get(normalizeKey(key)) ?? []) {
      if (seen.has(song)) continue
      seen.add(song)
      candidates.push(song)
    }
  }
  if (candidates.length === 0) return ""
  if (candidates.length === 1) return candidates[0].hashes[chart.course] ?? ""

  // Prefer subtitle overlap with chart keys
  const bySubtitle = candidates.filter((song) => {
    if (!song.subtitle) return false
    const sub = normalizeKey(song.subtitle)
    return chart.keys.some((k) => {
      const nk = normalizeKey(k)
      return nk && (sub === nk || sub.includes(nk) || nk.includes(sub))
    })
  })
  if (bySubtitle.length === 1) {
    return bySubtitle[0].hashes[chart.course] ?? ""
  }

  // Prefer exact (case-sensitive) title match to TJA primary title
  const exact = candidates.filter((s) => s.title === chart.title)
  if (exact.length === 1) return exact[0].hashes[chart.course] ?? ""

  // Prefer normalized title equality over subtitle-only index hits
  const titleHits = candidates.filter(
    (s) => normalizeKey(s.title) === normalizeKey(chart.title),
  )
  if (titleHits.length === 1) return titleHits[0].hashes[chart.course] ?? ""

  // Last resort: first candidate with a hash for this course
  for (const song of bySubtitle.length ? bySubtitle : titleHits.length ? titleHits : candidates) {
    const h = song.hashes[chart.course] ?? ""
    if (h) return h
  }
  return ""
}

/** @deprecated kept for callers; prefer buildSongIndex + resolveChartHash */
export function buildHashIndex(
  songs: Array<{ title: string; hashes: string[] }>,
): Map<string, string> {
  const map = new Map<string, string>()
  for (const song of songs) {
    const titleKey = normalizeKey(song.title)
    if (!titleKey) continue
    for (let course = 0; course <= 4; course++) {
      const hash = song.hashes[course] ?? ""
      if (!hash) continue
      // Do not overwrite an existing entry — first song wins (legacy)
      const k = `${titleKey}|${course}`
      if (!map.has(k)) map.set(k, hash)
    }
  }
  return map
}

export function scanLibrary(roots: string[], songs: SongRow[]): Catalog {
  const index = buildSongIndex(songs)
  const charts: CatalogChart[] = []
  // Deduplicate identical path+course only (same file). Game counts separate
  // TJA files even when they share a hash, so we keep those.
  const seen = new Set<string>()

  for (const root of roots) {
    if (!fs.existsSync(root)) continue
    for (const tjaPath of walkTjas(root)) {
      const parsed = parseTjaHeader(tjaPath)
      if (!parsed || parsed.courses.length === 0) continue

      const folder = path.basename(path.dirname(tjaPath))
      const primaryTitle = parsed.titles[0] ?? folder
      const keys = [
        ...parsed.titles,
        ...parsed.subtitles,
        folder,
      ].filter(Boolean)

      for (const { course, level } of parsed.courses) {
        if (course < 0 || course > 4) continue
        if (level < 1 || level > 10) continue

        // Unique per file path + course (not folder — allows duplicate TJAs)
        const id = `${normalizeKey(tjaPath)}|${course}`
        if (seen.has(id)) continue
        seen.add(id)

        const hash = resolveChartHash(
          { title: primaryTitle, keys, course },
          index,
        )

        charts.push({
          id,
          title: primaryTitle,
          keys,
          folder,
          course,
          level,
          hash,
          tjaPath,
        })
      }
    }
  }

  return {
    version: 1,
    scannedAt: new Date().toISOString(),
    roots,
    charts,
  }
}

export function loadCatalogCache(cachePath: string): Catalog | null {
  try {
    if (!fs.existsSync(cachePath)) return null
    const raw = JSON.parse(fs.readFileSync(cachePath, "utf8")) as Catalog
    if (raw?.version !== 1 || !Array.isArray(raw.charts)) return null
    return raw
  } catch {
    return null
  }
}

export function saveCatalogCache(cachePath: string, catalog: Catalog): void {
  fs.mkdirSync(path.dirname(cachePath), { recursive: true })
  const tmp = `${cachePath}.${process.pid}.tmp`
  fs.writeFileSync(tmp, JSON.stringify(catalog), "utf8")
  fs.renameSync(tmp, cachePath)
}
