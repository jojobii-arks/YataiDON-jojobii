import {
  app,
  BrowserWindow,
  Menu,
  nativeTheme,
  shell,
} from "electron"
import { spawn } from "node:child_process"
import fs from "node:fs"
import path from "node:path"
import { fileURLToPath } from "node:url"

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const COMPANION_ROOT = path.resolve(__dirname, "..")

// Keep Electron profile separate from our catalog/config JSON.
app.setPath(
  "userData",
  path.join(app.getPath("appData"), "taiko-stats", "electron"),
)

const APP_DIR = path.join(app.getPath("appData"), "taiko-stats", "data")
const LEGACY_APP_DIRS = [
  path.join(app.getPath("appData"), "taiko-stats"),
  path.join(app.getPath("appData"), "yataidon-progress"),
]
const WINDOW_STATE_PATH = path.join(APP_DIR, "window.json")

function migrateLegacyWindowState() {
  try {
    fs.mkdirSync(APP_DIR, { recursive: true })
    for (const legacyDir of LEGACY_APP_DIRS) {
      const legacy = path.join(legacyDir, "window.json")
      if (!fs.existsSync(WINDOW_STATE_PATH) && fs.existsSync(legacy)) {
        fs.copyFileSync(legacy, WINDOW_STATE_PATH)
        break
      }
    }
  } catch {
    /* ignore */
  }
}

const API_PORT = Number(process.env.PROGRESS_PORT ?? 3789)
const API_ORIGIN = `http://127.0.0.1:${API_PORT}`
const DEV_UI = process.env.PROGRESS_UI_URL ?? "http://127.0.0.1:5173"
const isDev = process.env.PROGRESS_ELECTRON_DEV === "1" || !app.isPackaged

/** @type {import('node:child_process').ChildProcess | null} */
let serverProc = null
/** @type {BrowserWindow | null} */
let mainWindow = null

/**
 * @typedef {{ x?: number, y?: number, width: number, height: number, alwaysOnTop?: boolean }} WindowState
 */

/** @returns {WindowState} */
function loadWindowState() {
  migrateLegacyWindowState()
  try {
    if (!fs.existsSync(WINDOW_STATE_PATH)) {
      return { width: 920, height: 980, alwaysOnTop: false }
    }
    const raw = JSON.parse(fs.readFileSync(WINDOW_STATE_PATH, "utf8"))
    return {
      width: Math.max(420, Number(raw.width) || 920),
      height: Math.max(480, Number(raw.height) || 980),
      x: typeof raw.x === "number" ? raw.x : undefined,
      y: typeof raw.y === "number" ? raw.y : undefined,
      alwaysOnTop: Boolean(raw.alwaysOnTop),
    }
  } catch {
    return { width: 920, height: 980, alwaysOnTop: false }
  }
}

/** @param {BrowserWindow} win */
function saveWindowState(win) {
  if (win.isDestroyed()) return
  const bounds = win.getBounds()
  /** @type {WindowState} */
  const state = {
    ...bounds,
    alwaysOnTop: win.isAlwaysOnTop(),
  }
  fs.mkdirSync(APP_DIR, { recursive: true })
  fs.writeFileSync(WINDOW_STATE_PATH, JSON.stringify(state, null, 2), "utf8")
}

async function waitForHealth(timeoutMs = 45_000) {
  const start = Date.now()
  let lastErr = ""
  while (Date.now() - start < timeoutMs) {
    try {
      const res = await fetch(`${API_ORIGIN}/api/health`)
      if (res.ok) return
      lastErr = `HTTP ${res.status}`
    } catch (err) {
      lastErr = err instanceof Error ? err.message : String(err)
    }
    await new Promise((r) => setTimeout(r, 250))
  }
  throw new Error(`Progress API not ready at ${API_ORIGIN} (${lastErr})`)
}

function spawnServer() {
  if (process.env.PROGRESS_SPAWN_SERVER === "0") return

  const env = {
    ...process.env,
    PROGRESS_PORT: String(API_PORT),
    // Electron prod loads the built UI from the API origin
    PROGRESS_STATIC: isDev ? "0" : "1",
  }

  serverProc = spawn(
    process.platform === "win32" ? "pnpm.cmd" : "pnpm",
    ["exec", "tsx", "server/index.ts"],
    {
      cwd: COMPANION_ROOT,
      env,
      stdio: "inherit",
      windowsHide: true,
      shell: process.platform === "win32",
    },
  )

  serverProc.on("exit", (code, signal) => {
    console.log(`[electron] server exited code=${code} signal=${signal}`)
    serverProc = null
  })
}

function stopServer() {
  if (!serverProc || serverProc.killed) return
  if (process.platform === "win32" && serverProc.pid) {
    spawn("taskkill", ["/pid", String(serverProc.pid), "/f", "/t"], {
      stdio: "ignore",
      windowsHide: true,
    })
  } else {
    serverProc.kill("SIGTERM")
  }
  serverProc = null
}

function buildMenu(win) {
  const state = loadWindowState()
  const template = [
    {
      label: "View",
      submenu: [
        {
          label: "Always on Top",
          type: "checkbox",
          checked: win.isAlwaysOnTop() || state.alwaysOnTop,
          click: (item) => {
            win.setAlwaysOnTop(Boolean(item.checked))
            saveWindowState(win)
          },
        },
        { type: "separator" },
        { role: "reload" },
        { role: "toggleDevTools" },
        { type: "separator" },
        { role: "resetZoom" },
        { role: "zoomIn" },
        { role: "zoomOut" },
      ],
    },
    {
      label: "Window",
      submenu: [{ role: "minimize" }, { role: "close" }],
    },
  ]
  Menu.setApplicationMenu(Menu.buildFromTemplate(template))
}

function createWindow() {
  const state = loadWindowState()
  const win = new BrowserWindow({
    width: state.width,
    height: state.height,
    x: state.x,
    y: state.y,
    minWidth: 400,
    minHeight: 480,
    backgroundColor: nativeTheme.shouldUseDarkColors ? "#161512" : "#f7f7f4",
    show: false,
    autoHideMenuBar: true,
    title: "taiko-stats",
    webPreferences: {
      preload: path.join(__dirname, "preload.cjs"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      backgroundThrottling: false,
    },
  })

  if (state.alwaysOnTop) win.setAlwaysOnTop(true)
  win.webContents.setBackgroundThrottling(false)

  win.once("ready-to-show", () => win.show())
  win.on("close", () => saveWindowState(win))

  win.webContents.setWindowOpenHandler(({ url }) => {
    void shell.openExternal(url)
    return { action: "deny" }
  })

  buildMenu(win)
  mainWindow = win

  const url = isDev ? DEV_UI : API_ORIGIN
  void win.loadURL(url)
  return win
}

app.commandLine.appendSwitch("disable-renderer-backgrounding")
app.commandLine.appendSwitch("disable-background-timer-throttling")
app.commandLine.appendSwitch("disable-backgrounding-occluded-windows")

app.whenReady().then(async () => {
  // In `electron:dev`, concurrently already starts the API — don't double-spawn
  // unless explicitly asked.
  const shouldSpawn =
    process.env.PROGRESS_SPAWN_SERVER === "1" ||
    (!isDev && process.env.PROGRESS_SPAWN_SERVER !== "0")

  if (shouldSpawn) spawnServer()

  try {
    await waitForHealth(shouldSpawn || isDev ? 60_000 : 5_000)
  } catch (err) {
    console.error(err)
    app.quit()
    return
  }

  createWindow()

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit()
})

app.on("before-quit", () => {
  if (mainWindow && !mainWindow.isDestroyed()) saveWindowState(mainWindow)
  stopServer()
})
