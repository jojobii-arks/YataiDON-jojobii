# Difficulty progress stats (rank + crown totals)

Local feature research + design notes for an official-style **progress card**:
per-course (and optionally per-★) counts of performance grades and crowns
(Clear / FC / DFC).



## What arcade / Nijiiro shows

On difficulty sort / course pad, a small card shows:

| Row | Meaning |
|-----|---------|
| Course mark | Easy…Oni (cloud + flower) |
| Grade icons × N | Count of charts whose **best** score rank is that grade |
| Crowns × 3 | Clear / Full Combo / Donderful (DFC) counts |

The arcade reference screenshot is that card (all zeros until scores exist).

## What already existed in YataiDON

| Piece | Status |
|-------|--------|
| `Navigator::get_statistics(path)` | Scans song root → `Statistics` = `course → ★1..10 → CourseStats` |
| Old `CourseStats` | `total`, `clears` (`crown >= CLEAR`), `full_combos` (`crown >= FC`) only |
| Rank totals | **Missing** |
| Separate DFC | **Missing** (DFC folded into `full_combos`) |
| C++ panel | `DiffSortSelect::draw_statistics()` + Green `diff_sort/stat_*` (total + clear + FC) |
| Nijiiro Lua | `one_menu_sort` + `draw_sort_window` — only `SortWindow:song_num()`, no progress card |
| Per-song badges | Different API: `box:course_info(d).crown` / `.rank` (single chart, not library totals) |

Score DB already stores everything needed (`Crown`, `Rank` on `Score`).

## OpenTaiko reference (`git@github.com:0auBSQ/OpenTaiko.git`)

OpenTaiko already implements the full pad pipeline (C# / TJAPlayer3 lineage — not this engine):

1. **`BestPlayRecords.CBestPlayStats`** — global aggregates from unique best plays:
   - `ScoreRanks[diff][0..7]` — exclusive grade buckets
   - `ClearStatuses[diff][0..4]` — exclusive clear / FC / perfect (DFC)
   - Also `LevelClears` / `LevelFCs` / `LevelPerfects` keyed by ★ for unlocks
2. **`CSongDict.tRefreshScoreTables()`** — copies into `CScorePad` per player / course
3. **`CStageSongSelect`** — draws skin `SongSelect_Table` + 11 digit slots (7 grades + 4 crown tiers)

OpenTaiko’s visible pad is **per course across all ★**, rebuilt on save load.
YataiDON’s scan already buckets **course × ★** (closer to official sort filtering).

## Design (this fork)

### Aggregation rules

For each chart present under the song root, for each course with a level 1–10:

1. `total++`
2. If the active player has a best score for that hash+course:
   - **Exclusive** `rank_counts[(int)Rank]` when `rank > _NONE`
   - **Exclusive** `crown_counts[(int)Crown]` when `crown > NONE`  
     (`CLEAR` / `FC` / `DFC` — not cumulative)
3. Derived (for Green C++ panel / legacy):
   - `clears = crown_counts[CLEAR] + crown_counts[FC] + crown_counts[DFC]`
   - `full_combos = crown_counts[FC] + crown_counts[DFC]`
   - `dfcs = crown_counts[DFC]`

2P: use `scores_manager.player_1` for 1P screen; extend later if 2P needs its own pad.

### Engine API

`CourseStats` fields (see `navigator.h`):

- `total`
- `rank_counts[8]` — index = `(int)Rank` (`_NONE`..`_RAINBOW`)
- `crown_counts[4]` — index = `(int)Crown` (`NONE`..`DFC`)
- `clears` / `full_combos` / `dfcs` — derived

`DiffSortSelect` Lua `SortWindow` extras:

| Binding | Meaning |
|---------|---------|
| `stats()` | **Exact** current course + ★ (no Oni/Ura merge; matches song count) |
| `stats_course()` | Same course, ★1–10 summed (still no Oni/Ura merge) |

Each table:

```lua
{
  course = 0..4,          -- EASY..URA
  star = 1..10,           -- stats() only
  merged_ura = false,     -- sort pad never merges
  total = N,
  clears = N, full_combos = N, dfcs = N,  -- derived
  ranks = { [1]=white, …, [7]=rainbow },  -- 1-based Rank enum
  crowns = { [1]=clear, [2]=fc, [3]=dfc },
}
```

### Skin wiring (Nijiiro only)

Scope: **YataiDONNijiiro** — not PyTaikoGreen / Red C++ `draw_statistics`.

**Browsing (always-on pad):** `SongSelect:draw_overlays` → `tex.progress_stats(course)`.
Course defaults to `last_difficulty` (Oni if unset); during course select it follows
`_mark_diff`. Oni+Ura are **merged**; both course marks are drawn.

**After diff-sort applies:** while the filtered DIFFICULTY inline list is open,
`tex.diff_sort_filter()` returns `{course, star}` and the pad stays on that exact
course + ★ (same as the sort window), including the ★ icon. Closing the list
clears the filter and returns to merged browsing.

**Diff-sort window:** redraws the pad from `SortWindow:stats()` for the **exact**
course + ★ currently selected (Easy / Normal / Hard / Oni / Ura separately). Shows
star icon + `N` in the card’s top-right.

Assets / layout:

- Course mark: `song_select/diff_sort/diff`
- Rank chips: `song_select/yellow_box/score_rank` (`frame = (rank-1)*5 + course`)
- Crowns: `song_select/yellow_box/crown_{clear,fc,dfc}` (`frame = course`)
- Counts: plain `text.create_raw_text` (not arcade digit strip)
- Backdrop: generic white `tex.draw_rect` card (~250×176) just below the top-left
  control guide (`y ≈ 300`), thin dark border for contrast
- Course marks: shrunk `diff_sort/diff` (native 96 → ~22 px via `x2`/`y2`); browse
  Oni+Ura merge draws **both** marks; sort draws the single selected course
- Sort-only: `★N` label top-right of the card

Engine: `SongSelectScreen` publishes `cached_stats` via `set_song_select_progress_stats`
as soon as the async scan finishes (no need to open difficulty sort).

**Oni + Ura (browse only):** `sum_course_stats` / `tex.progress_stats` merges courses
3 and 4 (OpenTaiko-style). Diff-sort intentionally does **not** merge.

### Non-goals / later

- Green / Red C++ `draw_statistics` polish (out of scope)
- Assisted-clear (OpenTaiko only)
- Pixel-perfect arcade progress art (compose from existing chips)

## Files touched

| Area | Path |
|------|------|
| Docs | `docs/difficulty_progress_stats.md` (this file) |
| Stats | `src/objects/song_select/file_navigator/navigator.{h,cpp}` |
| Sort Lua API | `diff_sort.{h,cpp}`, `box_lua_bindings.cpp` |
| Nijiiro UI | `Skins/YataiDONNijiiro/Scripts/song_select/song_select.lua` |

## Manual test

1. Build, then open the game
2. Open むずかしさからえらぶ / one-menu sort
3. Change course / ★ — totals update; empty library → all zeros
4. Clear / FC / DFC a chart — recount after re-entering song select (stats future on screen start)
