import fs from "node:fs"
import os from "node:os"
import path from "node:path"
import { fileURLToPath } from "node:url"
import type { AppConfig } from "../shared/types.js"

const __dirname = path.dirname(fileURLToPath(import.meta.url))
export const COMPANION_ROOT = path.resolve(__dirname, "..")
export const REPO_ROOT = path.resolve(COMPANION_ROOT, "..")

const APP_DIR = path.join(os.homedir(), "AppData", "Roaming", "taiko-stats", "data")
const LEGACY_APP_DIRS = [
  path.join(os.homedir(), "AppData", "Roaming", "taiko-stats"),
  path.join(os.homedir(), "AppData", "Roaming", "yataidon-progress"),
]
const USER_CONFIG_PATH = path.join(APP_DIR, "config.json")

function migrateLegacyAppData(): void {
  try {
    fs.mkdirSync(APP_DIR, { recursive: true })
    for (const legacyDir of LEGACY_APP_DIRS) {
      if (!fs.existsSync(legacyDir) || legacyDir === APP_DIR) continue
      for (const name of ["config.json", "catalog.json"]) {
        const dest = path.join(APP_DIR, name)
        const src = path.join(legacyDir, name)
        if (!fs.existsSync(dest) && fs.existsSync(src)) {
          fs.copyFileSync(src, dest)
        }
      }
    }
  } catch {
    /* ignore migration errors */
  }
}

function resolveDefaultSongs(): string[] {
  const candidates = [
    path.resolve(REPO_ROOT, "..", "Songs"),
    path.resolve(REPO_ROOT, "Songs"),
    "G:\\taiko\\Songs",
  ]
  return candidates.filter((p) => fs.existsSync(p)).slice(0, 1)
}

function readGameConfigToml(): Partial<Pick<AppConfig, "tjaPaths">> {
  const tomlPath = path.join(REPO_ROOT, "config.toml")
  if (!fs.existsSync(tomlPath)) return {}
  try {
    const text = fs.readFileSync(tomlPath, "utf8")
    const paths: string[] = []
    const arrayMatch = text.match(/tja_path\s*=\s*\[([^\]]*)\]/)
    if (arrayMatch) {
      for (const m of arrayMatch[1].matchAll(/['"]([^'"]+)['"]/g)) {
        const raw = m[1]
        const resolved = path.isAbsolute(raw)
          ? raw
          : path.resolve(REPO_ROOT, raw)
        if (fs.existsSync(resolved)) paths.push(resolved)
      }
    }
    return paths.length ? { tjaPaths: paths } : {}
  } catch {
    return {}
  }
}

export function defaultConfig(): AppConfig {
  migrateLegacyAppData()
  const fromToml = readGameConfigToml()
  const tjaPaths = fromToml.tjaPaths?.length
    ? fromToml.tjaPaths
    : resolveDefaultSongs()

  fs.mkdirSync(APP_DIR, { recursive: true })

  return {
    scoresDb: path.join(REPO_ROOT, "scores.db"),
    tjaPaths,
    tierDataPath: path.join(REPO_ROOT, "data", "tier_data.json"),
    catalogCachePath: path.join(APP_DIR, "catalog.json"),
    mergeOniUraDefault: true,
    pollMs: 1500,
    playerId: "0",
  }
}

export function loadConfig(): AppConfig {
  const base = defaultConfig()
  if (!fs.existsSync(USER_CONFIG_PATH)) return base
  try {
    const raw = JSON.parse(fs.readFileSync(USER_CONFIG_PATH, "utf8")) as Partial<AppConfig> & {
      playerId?: string | number
    }
    const next = { ...base, ...raw }
    // Upstream scores.db migration: integer player_id 1 → access code "0".
    if (typeof raw.playerId === "number") {
      next.playerId = raw.playerId === 1 ? "0" : String(raw.playerId)
    } else if (raw.playerId === "1" && base.playerId === "0") {
      // Old companion configs stored default as "1" before access-code remap.
      next.playerId = "0"
    }
    return next
  } catch {
    return base
  }
}

export function saveConfig(patch: Partial<AppConfig>): AppConfig {
  const next = { ...loadConfig(), ...patch }
  fs.mkdirSync(path.dirname(USER_CONFIG_PATH), { recursive: true })
  fs.writeFileSync(USER_CONFIG_PATH, JSON.stringify(next, null, 2), "utf8")
  return next
}

export function userConfigPath(): string {
  return USER_CONFIG_PATH
}
