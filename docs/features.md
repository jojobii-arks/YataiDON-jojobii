# Fork features

This fork of [YataiDON](https://github.com/Yonokid/YataiDON) adds a set of
community features on top of upstream. Everything here is additive: upstream's
screens, input model and skins are untouched unless a feature is explicitly
switched on.

Skin-side work lives in a small overlay skin, `Skins/YataiDONNijiiro-jojobii`,
whose `Graphics/skin_config.json` declares a `"parent"` skin. The parent
supplies every texture, font and sound; the overlay overrides only the scripts
and graphics it needs. The parent skins are external (see
[Building and running](#building-and-running)).

---

## Pro song select

A full-screen chart list that replaces the song wheel, reached from its own
main-menu entry `entry_pro`.

- Every row is a chart, and the cursor sits on one chart — moving left/right
  walks the song's own rows (Easy → Ura) and then steps to the neighbouring
  song, so the list reads as a continuous ribbon.
- Confirming opens the picker already focused on that row's course, Ura
  included. Courses a song does not have are skipped by the cursor and drawn
  greyed out rather than being reachable.
- **Filter / Sort dialog** with a two-level control model: the filter entries
  (STAR, DIFFICULTY, TIER) and the sort ladder (SCORE, CROWN, A-Z, LEVEL) in one
  window. A gold *fill* means an option is on; a gold *C-clamp* marks the
  cursor, at both the entry level and the value level.
- Cards take the genre's own colours: a saturated band frame with the band
  mixed 55% toward white as the plate. The selected chart's DON section unfolds
  out of its card as part of it.
- **Hover preview** — resting the cursor on a chart plays that song's preview
  and loops it. Keyed on the song, so two rows of the same song never restart
  it.
- **Return to position** — re-entering the screen restores the folder, filter /
  sort state and cursor row.
- **Structure (Dan Dojo)** folders render era sets as folder cards with their
  course counts, and confirming a course hands off to the dojo focused on it;
  backing out returns to where you were.

`STAR = ALL` offers only `ALL` / `NONE` for TIER, because a tier is a property
of a song + course + star triple and means nothing without a star.

## Community clear-tier list (ESE / taiko.wiki)

Gold tier badges on song boards plus a level-aware Tier Sort.

- A tier is per **(song, course, star level)** — ★6–★10 each carry their own
  letter scale.
- Matching tries the TJA `TITLE` / `TITLEEN` (and other `TITLE*` languages)
  first, falling back to the song's folder name. Titles win because on-disk
  folder names often diverge from wiki titles.
- The data is community-curated (taiko.wiki / ESE) and is **not shipped in this
  repository**. Fetch it once after cloning: `python tools/fetch_tiers.py` writes
  `data/tier_data.json`. Without that file, tier badges and Tier Sort are simply
  disabled. The game never touches the network at runtime.

Details: [`tier_list.md`](tier_list.md) and the skin hook in
[`tier_list_lua_hook.md`](tier_list_lua_hook.md).

## Difficulty progress pad

Arcade-style rank and crown totals, aggregated in the engine so any skin can
draw them.

- For every chart under the song root, for each course with a level 1–10:
  count the chart, then bucket the player's **best** score into an exclusive
  rank bucket and an exclusive crown bucket (Clear / Full Combo / DFC).
- `CourseStats` carries `total`, `rank_counts[8]`, `crown_counts[4]`, and the
  derived `clears` / `full_combos` / `dfcs`.
- Two granularities are published: the exact current course + ★, and the same
  course across ★1–10. Oni and Ura merge for browsing (OpenTaiko-style) but
  never inside the sort window.

The engine still computes and publishes these totals; the always-on in-game
card was removed once the companion app below became the progress UI. Per-song
rank and crown icons on the boards are unchanged.

Design notes: [`difficulty_progress_stats.md`](difficulty_progress_stats.md).

## Hidden note modifier ("doron")

Hides a percentage of the approach lane before the judge, with a soft fade —
the arcade's "doron" modifier.

- Stored per player (`players.modifier_hidden`); `0` disables it. The helper
  steps through 0, 10, 15, 20, 25, 30, 35, 40, 50, 60.
- The **fade smoothness is global, not per player** — `config.toml`
  `[hidden] fade_percent` (default 25): `0` is a hard cut, `100` fades across
  the whole hide zone.
- **F3** opens the tuner, which adjusts distance and fade and works live
  mid-song. F3 is no longer a settings shortcut.

## Progress companion (`taiko-stats`)

**Deprecated** — an early side project, since superseded by a newer taiko
progress app. Kept in this repository for reference; it still runs.

A desktop side panel that reads YataiDON's `scores.db` and shows the same rank
/ crown math with filters the in-game pad does not have: course (including an
Oni+Ura merge), ★1–10 or all, and community tier.

- Electron shell, React UI, and a Node (Hono) API over a **read-only** SQLite
  connection. The game owns the database; the companion never writes to it.
- Refreshes after a result, not mid-song.

Run it with `cd progress-companion && pnpm install && launch.bat`. Plan,
endpoints and the known gaps: [`companion_progress_app.md`](companion_progress_app.md)
and `progress-companion/README.md`.

---

## Building and running

Toolchain: MSYS2 MinGW (cmake + ninja + g++). Configure and build under
`build/`, then copy `bin/YataiDON.exe` to the repository root — the executable
expects its data, shader and skin folders beside it.

**The parent skins are not in this repository.** They are git submodules on an
external host (`ese.tjadataba.se`), because their assets are not redistributable
here. The `.gitmodules` entries are commit references only:

```
git submodule update --init Skins/PyTaikoGreen Skins/YataiDONRed Skins/YataiDONNijiiro
```

A clone that cannot reach that host still builds the engine; it just cannot run
the Nijiiro-based skins. The `YataiDONNijiiro-jojobii` overlay in this repository
carries the fork's own scripts and graphics, but a complete, runnable skin also
needs `skin_config.json`, its `Sounds/`, and the skin selected in `config.toml`.
