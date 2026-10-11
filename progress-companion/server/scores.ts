import Database from "better-sqlite3"
import type { CrownId, RankId } from "../shared/types.js"

export interface BestScore {
  hash: string
  difficulty: number
  crown: number
  rank: number
  score: number
}

export interface SongRow {
  title: string
  subtitle: string
  hashes: [string, string, string, string, string]
}

export function openScoresDb(dbPath: string): Database.Database {
  return new Database(dbPath, {
    readonly: true,
    fileMustExist: true,
    timeout: 2500,
  })
}

/** Exclusive best play per (hash, difficulty) — crown DESC, score DESC. */
export function loadBestScores(
  db: Database.Database,
  playerId: string,
): Map<string, BestScore> {
  const rows = db
    .prepare(
      `SELECT hash, difficulty, crown, rank, score
       FROM scores
       WHERE player_id = ? AND hash IS NOT NULL AND hash != ''
       ORDER BY crown DESC, score DESC`,
    )
    .all(playerId) as Array<{
    hash: string
    difficulty: number
    crown: number
    rank: number
    score: number
  }>

  const best = new Map<string, BestScore>()
  for (const row of rows) {
    const key = `${row.hash}|${row.difficulty}`
    if (best.has(key)) continue
    best.set(key, row)
  }
  return best
}

export function loadSongs(db: Database.Database): SongRow[] {
  const rows = db
    .prepare(
      `SELECT title, subtitle, hash_0, hash_1, hash_2, hash_3, hash_4
       FROM songs`,
    )
    .all() as Array<{
    title: string
    subtitle: string | null
    hash_0: string | null
    hash_1: string | null
    hash_2: string | null
    hash_3: string | null
    hash_4: string | null
  }>

  return rows.map((r) => ({
    title: r.title ?? "",
    subtitle: r.subtitle ?? "",
    hashes: [
      r.hash_0 ?? "",
      r.hash_1 ?? "",
      r.hash_2 ?? "",
      r.hash_3 ?? "",
      r.hash_4 ?? "",
    ],
  }))
}

export function emptyRanks(): Record<RankId, number> {
  return { 1: 0, 2: 0, 3: 0, 4: 0, 5: 0, 6: 0, 7: 0 }
}

export function emptyCrowns(): Record<CrownId, number> {
  return { 1: 0, 2: 0, 3: 0 }
}
