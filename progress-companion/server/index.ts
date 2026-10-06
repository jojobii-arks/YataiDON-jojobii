import { serve } from "@hono/node-server"
import { serveStatic } from "@hono/node-server/serve-static"
import { Hono } from "hono"
import { cors } from "hono/cors"
import { streamSSE } from "hono/streaming"
import fs from "node:fs"
import path from "node:path"
import type {
  CourseFilter,
  ProgressFilters,
  StatusPayload,
} from "../shared/types.js"
import { aggregatePad, buildTierOptions } from "./aggregate.js"
import {
  loadCatalogCache,
  saveCatalogCache,
  scanLibrary,
  type Catalog,
} from "./catalog.js"
import { COMPANION_ROOT, loadConfig, saveConfig } from "./config.js"
import {
  loadBestScores,
  loadSongs,
  openScoresDb,
  type BestScore,
} from "./scores.js"
import { loadTierData, type TierData } from "./tier.js"
import { ScoresWatcher } from "./watch.js"

const DEFAULT_PORT = Number(process.env.PROGRESS_PORT ?? 3789)

let config = loadConfig()
let catalog: Catalog | null = loadCatalogCache(config.catalogCachePath)
let tierData: TierData | null = loadTierData(config.tierDataPath)
let bestScores = new Map<string, BestScore>()
let lastScoresRefreshAt: string | null = null
let lastError: string | null = null
let watching = false

const sseClients = new Set<(event: string, data: unknown) => void>()
const watcher = new ScoresWatcher(400)

function broadcast(event: string, data: unknown) {
  for (const send of sseClients) send(event, data)
}

function refreshScores(): void {
  try {
    if (!fs.existsSync(config.scoresDb)) {
      lastError = `scores.db not found: ${config.scoresDb}`
      bestScores = new Map()
      return
    }
    const db = openScoresDb(config.scoresDb)
    try {
      bestScores = loadBestScores(db, config.playerId)
      lastScoresRefreshAt = new Date().toISOString()
      lastError = null
    } finally {
      db.close()
    }
  } catch (err) {
    lastError = err instanceof Error ? err.message : String(err)
  }
}

/** Re-read scores.db when the file (or WAL) changes. */
let lastDbSig = ""
function scoresDbSignature(): string {
  const dbPath = config.scoresDb
  const dir = path.dirname(dbPath)
  const base = path.basename(dbPath)
  let sig = ""
  for (const file of [
    dbPath,
    path.join(dir, `${base}-wal`),
    path.join(dir, `${base}-shm`),
  ]) {
    try {
      const st = fs.statSync(file)
      sig += `${st.mtimeMs}:${st.size};`
    } catch {
      sig += "x;"
    }
  }
  return sig
}

function refreshScoresIfChanged(force = false): boolean {
  const sig = scoresDbSignature()
  // Retry when a previous read failed (sticky SQLITE_BUSY / lock errors)
  if (!force && !lastError && sig === lastDbSig && bestScores.size > 0) {
    return false
  }
  lastDbSig = sig
  refreshScores()
  return true
}

function rescanLibrary(): { charts: number; songs: number } {
  let songs: ReturnType<typeof loadSongs> = []
  try {
    if (fs.existsSync(config.scoresDb)) {
      const db = openScoresDb(config.scoresDb)
      try {
        songs = loadSongs(db)
      } finally {
        db.close()
      }
    }
  } catch (err) {
    lastError = err instanceof Error ? err.message : String(err)
  }

  catalog = scanLibrary(config.tjaPaths, songs)
  saveCatalogCache(config.catalogCachePath, catalog)
  tierData = loadTierData(config.tierDataPath)
  return {
    charts: catalog.charts.length,
    songs: new Set(catalog.charts.map((c) => c.folder)).size,
  }
}

function parseFilters(query: Record<string, string | undefined>): ProgressFilters {
  const courseRaw = query.course ?? (config.mergeOniUraDefault ? "oni_ura" : "3")
  let course: CourseFilter
  if (courseRaw === "oni_ura") course = "oni_ura"
  else {
    const n = Number(courseRaw)
    course = (n >= 0 && n <= 4 ? n : 3) as CourseFilter
  }

  const starRaw = query.star
  const star =
    starRaw == null || starRaw === "" || starRaw === "all"
      ? null
      : Math.min(10, Math.max(1, Number(starRaw) || 1))

  const tier = query.tier && query.tier !== "all" ? query.tier : null

  return { course, star, tier }
}

function statusPayload(): StatusPayload {
  return {
    config,
    catalogReady: catalog != null,
    catalogCharts: catalog?.charts.length ?? 0,
    catalogSongs: catalog
      ? new Set(catalog.charts.map((c) => c.folder)).size
      : 0,
    lastCatalogScanAt: catalog?.scannedAt ?? null,
    lastScoresRefreshAt,
    watching,
    scoresDbExists: fs.existsSync(config.scoresDb),
    error: lastError,
  }
}

function ensureWatching() {
  if (watching) return
  watcher.start(
    config.scoresDb,
    () => {
      refreshScoresIfChanged(true)
      broadcast("scores-updated", statusPayload())
    },
    config.pollMs,
  )
  watching = true
  console.log(
    `[progress] watching ${config.scoresDb} (poll ${config.pollMs}ms)`,
  )
}

function createApp(options: { serveStaticDir?: string | null } = {}) {
  const app = new Hono()
  app.use("*", cors({ origin: "*" }))

  app.get("/api/health", (c) => c.json({ ok: true }))

  app.get("/api/status", (c) => {
    ensureWatching()
    refreshScoresIfChanged()
    return c.json(statusPayload())
  })

  app.get("/api/config", (c) => c.json(config))

  app.post("/api/config", async (c) => {
    const body = (await c.req.json()) as Partial<typeof config>
    config = saveConfig(body)
    watching = false
    watcher.stop()
    ensureWatching()
    refreshScores()
    return c.json(statusPayload())
  })

  app.post("/api/rescan", (c) => {
    try {
      const result = rescanLibrary()
      refreshScoresIfChanged(true)
      lastError = null
      const payload = { ...statusPayload(), ...result }
      broadcast("catalog-updated", payload)
      return c.json(payload)
    } catch (err) {
      lastError = err instanceof Error ? err.message : String(err)
      return c.json({ ...statusPayload(), ok: false }, 500)
    }
  })

  app.get("/api/progress", (c) => {
    ensureWatching()
    // Always re-read the DB. Signature short-circuit is for the watcher path;
    // polling clients need fresh maps even when Windows leaves mtime unchanged.
    refreshScores()
    lastDbSig = scoresDbSignature()
    const filters = parseFilters(c.req.query())
    const pad = aggregatePad(catalog, bestScores, filters, tierData)
    return c.json(pad)
  })

  app.get("/api/tiers", (c) => {
    const filters = parseFilters(c.req.query())
    const options = buildTierOptions(
      catalog,
      tierData,
      filters.course,
      filters.star,
    )
    return c.json({ options, levels: tierData?.levels ?? [] })
  })

  app.get("/api/events", (c) => {
    ensureWatching()
    return streamSSE(c, async (stream) => {
      const send = (event: string, data: unknown) => {
        void stream.writeSSE({ event, data: JSON.stringify(data) })
      }
      sseClients.add(send)
      send("hello", statusPayload())
      try {
        while (true) {
          await stream.sleep(30_000)
          send("ping", { t: Date.now() })
        }
      } finally {
        sseClients.delete(send)
      }
    })
  })

  const staticRoot = options.serveStaticDir
  if (staticRoot && fs.existsSync(staticRoot)) {
    // serveStatic root is relative to cwd; prefer absolute via rewrite
    const rel = path.relative(process.cwd(), staticRoot) || "."
    app.use(
      "/*",
      serveStatic({
        root: rel,
        rewriteRequestPath: (p) => (p === "/" ? "/index.html" : p),
      }),
    )
    app.get("*", async (c) => {
      const indexPath = path.join(staticRoot, "index.html")
      if (!fs.existsSync(indexPath)) return c.text("UI not built", 404)
      const html = fs.readFileSync(indexPath, "utf8")
      return c.html(html)
    })
  }

  return app
}

export interface StartServerOptions {
  port?: number
  /** Serve Vite `dist/` (Electron production / `electron:start`). */
  serveStatic?: boolean
}

export function startProgressServer(options: StartServerOptions = {}) {
  const port = options.port ?? DEFAULT_PORT
  const distDir = path.join(COMPANION_ROOT, "dist")
  const serveUi =
    options.serveStatic === true ||
    process.env.PROGRESS_STATIC === "1" ||
    (options.serveStatic !== false &&
      process.env.PROGRESS_STATIC !== "0" &&
      fs.existsSync(path.join(distDir, "index.html")))

  refreshScores()
  lastDbSig = scoresDbSignature()
  if (!catalog) {
    console.log(
      "[progress] no catalog cache — run Rescan from the UI (or POST /api/rescan)",
    )
  } else {
    console.log(
      `[progress] catalog cache: ${catalog.charts.length} charts (${catalog.scannedAt})`,
    )
  }
  ensureWatching()

  const app = createApp({
    serveStaticDir: serveUi ? distDir : null,
  })

  const server = serve({ fetch: app.fetch, port, hostname: "127.0.0.1" })

  server.on("error", (err: NodeJS.ErrnoException) => {
    if (err.code === "EADDRINUSE") {
      console.error(
        `[progress] port ${port} already in use.\n` +
          `  Close the other taiko-stats window, or run launch.bat again ` +
          `(it clears leftover ports).`,
      )
      process.exit(1)
    }
    console.error("[progress] server error:", err)
    process.exit(1)
  })

  console.log(
    `[progress] listening on http://127.0.0.1:${port}` +
      (serveUi ? " (api + static UI)" : " (api)"),
  )

  return server
}

// Always listen when this module is executed via `tsx server/index.ts`.
// (Electron never imports it — it spawns this file as a child process.)
startProgressServer()
