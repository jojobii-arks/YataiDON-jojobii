import chokidar, { type FSWatcher } from "chokidar"
import fs from "node:fs"
import path from "node:path"

export type WatchCallback = () => void

function fileSignature(targets: string[]): string {
  let sig = ""
  for (const file of targets) {
    try {
      const st = fs.statSync(file)
      sig += `${file}:${st.mtimeMs}:${st.size};`
    } catch {
      sig += `${file}:missing;`
    }
  }
  return sig
}

/**
 * Watch scores.db (+ WAL/SHM). Native FS events are flaky on Windows with
 * SQLite writers, so we always enable polling and an mtime signature check.
 */
export class ScoresWatcher {
  private watcher: FSWatcher | null = null
  private debounceTimer: ReturnType<typeof setTimeout> | null = null
  private pollTimer: ReturnType<typeof setInterval> | null = null
  private debounceMs: number
  private lastSig = ""

  constructor(debounceMs = 400) {
    this.debounceMs = debounceMs
  }

  start(dbPath: string, onChange: WatchCallback, pollMs = 1500): void {
    this.stop()
    const dir = path.dirname(dbPath)
    const base = path.basename(dbPath)
    const targets = [
      dbPath,
      path.join(dir, `${base}-wal`),
      path.join(dir, `${base}-shm`),
    ]

    const kick = () => {
      if (this.debounceTimer) clearTimeout(this.debounceTimer)
      this.debounceTimer = setTimeout(() => onChange(), this.debounceMs)
    }

    this.lastSig = fileSignature(targets)

    this.watcher = chokidar.watch(dir, {
      ignoreInitial: true,
      usePolling: true,
      interval: Math.max(500, Math.floor(pollMs / 2)),
      binaryInterval: Math.max(500, Math.floor(pollMs / 2)),
      awaitWriteFinish: { stabilityThreshold: 200, pollInterval: 100 },
      depth: 0,
      ignored: (watchedPath) => {
        const name = path.basename(watchedPath)
        if (watchedPath === dir) return false
        return !(
          name === base ||
          name === `${base}-wal` ||
          name === `${base}-shm`
        )
      },
    })

    this.watcher.on("all", () => kick())
    this.watcher.on("error", (err) => {
      console.warn("[progress] scores watch error:", err)
    })

    // Belt-and-suspenders: explicit mtime poll (survives chokidar quirks)
    this.pollTimer = setInterval(() => {
      const next = fileSignature(targets)
      if (next !== this.lastSig) {
        this.lastSig = next
        kick()
      }
    }, Math.max(750, pollMs))
  }

  stop(): void {
    if (this.debounceTimer) {
      clearTimeout(this.debounceTimer)
      this.debounceTimer = null
    }
    if (this.pollTimer) {
      clearInterval(this.pollTimer)
      this.pollTimer = null
    }
    if (this.watcher) {
      void this.watcher.close()
      this.watcher = null
    }
  }
}
