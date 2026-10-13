# taiko-stats

Local side panel for Nijiiro-style rank / crown totals with filters you control.
Reads YataiDON `scores.db` read-only and refreshes after each play.

## Stack

- React + Vite + Tailwind v4 + shadcn-style primitives + Zustand
- Node (Hono) API: SQLite (`better-sqlite3`), TJA header scan, `chokidar` watch
- Electron shell (thin BrowserWindow; API stays in a normal Node process)
- Design tokens from [getdesign Cursor](https://getdesign.md/cursor/design-md) (`themes/cursor.css`), plus Ink dark

## Run

```bash
cd progress-companion
pnpm install

# Browser (API + Vite)
pnpm dev

# Desktop window (API + Vite + Electron)
pnpm electron:dev

# Built UI in Electron (serves dist from the API)
pnpm electron:start
```

Or double-click **`launch.bat`** (same as `electron:dev`). Pin a shortcut to that file for the taskbar/desktop.

- UI (dev): http://127.0.0.1:5173
- API: http://127.0.0.1:3789

On first launch click **Rescan** to index TJAs (needed for ★ and tier filters).

Electron extras: **View → Always on Top**, window size/position remembered under
`%APPDATA%/taiko-stats/window.json`.

## Filters

| Filter | Notes |
|--------|--------|
| Difficulty | Easy…Ura + Oni+Ura merge |
| Level | ★1–10 or All |
| Tier list | One big list of `★N · letter` (from `data/tier_data.json`); Oni/Ura only |

## Config

Defaults auto-detect repo `scores.db`, `config.toml` `tja_path`, and `data/tier_data.json`.
Overrides are stored at `%APPDATA%/taiko-stats/config.json`.

## Extending

Register new views in `src/features/registry.ts`. Theme CSS lives under `themes/`
and is selected via `data-theme` (see `src/lib/theme.ts`).
