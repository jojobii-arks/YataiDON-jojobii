# Companion progress app (Tauri) — implementation plan

Side project: a desktop companion that reads YataiDON `scores.db` and shows
Nijiiro-style rank/crown summary cards with filters you control. Updates after
each play (not mid-song).

## Goals

- Same pad math as in-game: exclusive best-rank buckets + Clear / FC / DFC crowns
- Filters: course (Easy…Ura, optional Oni+Ura merge), ★ 1–10, “all ★”
- Live-enough: refresh when `scores.db` changes after a result
- Granular UI you own (layout, themes, multi-monitor) without touching the game skin

## Non-goals (v1)

- Mid-song / in-play overlay
- Writing to `scores.db`
- Online sync / cloud
- Pixel-perfect arcade art (reuse simple icons or screenshots later)

## Stack

| Piece | Choice |
|-------|--------|
| Shell | **Tauri 2** (small binary, native FS, good Windows story) |
| UI | React or Svelte + Vite |
| DB | `rusqlite` (Rust) read-only against `scores.db` |
| Watch | `notify` crate on the DB path + short debounce / retry |

## Data model (mirror game)

From YataiDON (`src/libs/scores.*`, progress pad docs):

- Score row: hash, difficulty (0–4), score, crown, rank, …
- Pad buckets: `rank_counts[1..7]`, `crown_counts[1..3]` (exclusive)
- Course ★ level comes from chart metadata (TJA / song cache), not from the score
  row alone — companion needs a **hash → (title, course, level)** map

### Chart catalog (required for ★ / titles)

Options, pick one for v1:

1. **Scan Songs roots** (same `tja_path` as `config.toml`) and build hash→meta offline
2. **Export JSON** from YataiDON once per session (optional later hook)
3. **Reuse** a cached index if we add one to the game later

v1 recommendation: scan TJAs on first launch + “Rescan library” button; persist
catalog next to the app config.

## Architecture

```
┌─────────────────┐     read-only      ┌────────────┐
│  YataiDON.exe   │ ───────────────► │ scores.db  │
└─────────────────┘   (after result)   └─────▲──────┘
                                             │ notify + open
┌─────────────────┐   IPC commands     ┌─────┴──────┐
│  WebView (UI)   │ ◄───────────────► │ Tauri/Rust │
│  filter + cards │                    │ aggregate  │
└─────────────────┘                    └────────────┘
```

### Rust commands (sketch)

- `get_config` / `set_config` — paths to `scores.db`, Songs roots, merge Oni/Ura
- `rescan_library` — rebuild hash→{title, course, level}
- `get_progress_pad { course?, star?, merge_ura }` — returns ranks/crowns/total
- `watch_scores_start` — emit `scores-updated` events to the UI

### Aggregation (same as engine)

For each chart (hash, course) take best score; bucket by rank and crown exclusively;
filter by course (± merge 3+4) and optional ★ from catalog.

## UI (v1)

One window, not a dashboard:

- Course chips: Easy / Normal / Hard / Oni / Ura / Oni+Ura
- ★ slider or 1–10 + “All”
- Summary card: course mark(s), 2×3 ranks, 3 crowns, counts as text
- Footer: last refresh time, DB path, “Rescan”

Later: second card for “session delta” (counts since app open).

## Config

App config (e.g. `%APPDATA%/taiko-stats/config.json`):

```json
{
  "scores_db": "/path/to/scores.db",
  "tja_paths": ["/path/to/Songs"],
  "merge_oni_ura_default": true,
  "poll_ms": 1500
}
```

Auto-detect: look beside `YataiDON.exe` / read game `config.toml` `[paths]` if present.

## SQLite concurrency

- Open DB **read-only** (`mode=ro`, `immutable=0`)
- On `SQLITE_BUSY` / locked: retry a few times; fall back to copy-on-read temp file
- Debounce file events (~300–500 ms) — WAL may touch `-wal`/`-shm`

## Milestones

1. **Scaffold** Tauri 2 + empty window + config path picker for `scores.db`
2. **Read scores** — list row counts; prove refresh after a play
3. **Catalog** — TJA/hash scan; show ★ filter working
4. **Pad UI** — ranks + crowns + course marks; Oni+Ura merge toggle
5. **Watch** — auto-refresh; polish errors when game locks DB
6. **Optional** — session deltas, always-on-top, opacity, tray icon

## Repo layout

Implemented at `progress-companion/` as **taiko-stats**:

```
progress-companion/   # app id: taiko-stats
  server/             # Node (Hono) — scores.db, TJA catalog, tier, watch
  src/                # React UI (Summary + feature registry)
  electron/           # thin Electron shell
  shared/             # Shared types
  themes/             # Cursor (+ Ink) design tokens
  launch.bat
  README.md
```

Handoff originally suggested Tauri 2; v1 ships Electron + Node+Vite.
API shapes match the plan so a Tauri shell can still wrap later.



## Relation to in-game pad

| | In-game Nijiiro pad | Companion |
|--|---------------------|-----------|
| When | Song select | Always (while open) |
| Filter after diff-sort folder | **Known gap** (sort menu OK; folder browse falls back to merged totals) | Full control |
| Mid-play | No | No (v1) |
| Art | Skin textures | Simple / copied assets |

Companion is the right place for granular filters until the in-game
post-sort filter is fixed.
