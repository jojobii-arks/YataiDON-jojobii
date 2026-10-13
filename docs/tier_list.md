# Community clear-tier list (ESE / taiko.wiki)

Fork feature: star-level Tier Sort + gold tier badges on song boards.

## Data

- Artifact: `data/tier_data.json` — **not shipped in this repository.** Fetch it
  with `python tools/fetch_tiers.py` once after cloning; a missing file simply
  disables tiers (the loader warns and returns, it does not crash).
- Refresh: re-run the same script to rewrite the JSON from taiko.wiki.
- A tier is per **(song, course, star level)** — ★6..★10 each have their own letter scale

## Lookup order (source of truth)

Matching tries keys **in this order**; first hit wins:

1. `TITLE` / `TITLEEN` (and other `TITLE*` languages) from the TJA
2. Song **folder name** (ESE parent of the `.tja`) as fallback only

Keys are trimmed + ASCII-lowercased (`TierListManager::normalize_key`).
JSON entries also index `romaji` and `keys` aliases.

Why title-first: Windows folder names often diverge from wiki titles
(e.g. on-disk `2354 Prelude for The Journey of Sunlight` vs wiki
`11:54PM, Prelude for The Journey of Sunlight`).

## Engine (C++)

| Area | Role |
|---|---|
| `src/libs/tier_list.{h,cpp}` | Load JSON; `lookup` / `lookup_first` / `tier_for_first` |
| `box_song.{h,cpp}` | Resolve `tier_oni` / `tier_ura` from chart levels; C++ badge draw (skins that don't override boards) |
| `box_lua_bindings.cpp` | Lua: `box:tier()`, `tier_oni`, `tier_ura`, `tier_for`, `course_info(d).tier` |
| `navigator.{h,cpp}` | Tier Sort collection; scan caches `song_tier_keys` (titles then folder); stats + filter use `lookup_first` |
| `tier_sort.{h,cpp}` | Two-stage UI: pick ★6–★10, then that level's letters |
| `song_select` / `player` | Open/confirm Tier Sort with `(level, letter)` |

Badge letter while browsing: `SongBox::tier()` — ura letter when ura-mode + ura tier exists, else oni.

## Nijiiro skin (Lua)

`Skins/YataiDONNijiiro/Scripts/song_select/song_select.lua` replaces C++ board drawing, so the badge is drawn in Lua:

- `SongSelect:tier_badge` / `draw_tier_badge` via `box:tier()`
- Closed board: above title; open board: above Oni/Ura plate
- Avoid new main-chunk `local`s (file is at Lua's 200-local limit) — constants are inlined

Details: [`tier_list_lua_hook.md`](tier_list_lua_hook.md)

## How to test (manual)

1. Build from `build/` (`cmake --build . -j8`), then copy `bin/YataiDON.exe` → repo root.
2. Open Tier Sort: ★ level → letter → the wheel filters.
3. ESE song with a wiki entry (e.g. Namco Original *11:54PM…*): gold letter on closed/open board
4. Toggle ura when both Oni/Ura are ranked — badge switches with ura mode

## Build

- Toolchain: MSYS2 MinGW (cmake + ninja + g++)
