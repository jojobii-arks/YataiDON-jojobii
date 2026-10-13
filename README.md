
# **This is a fork of [Yonokid/YataiDON](https://github.com/Yonokid/YataiDON).** Everything upstream ships still works as it always has — this adds a set of song-select and progression features on top.

did i vibe code this shit yea, does it work? yeah

## What this fork adds

**Pro song select** — a full-screen chart list that replaces the song wheel, with its own main-menu entry. Every row is a chart and the cursor sits on one, so moving left/right steps through a song's own courses and then onto the neighbouring song. A filter/sort window covers star, difficulty and tier alongside score / crown / A–Z / level sorts, cards take their colours from the song's genre, and resting the cursor on a chart plays that song's preview on a loop. Leaving the screen and coming back restores your folder, filters and cursor position. Dan Dojo folders list their era sets with a direct hand-off into the dojo.

**Community clear-tier list** (ESE / taiko.wiki) — gold tier letters on the song boards plus a level-aware Tier Sort. A tier is per song, course *and* star level: ★6–★10 each carry their own letter scale, and titles are matched before folder names because on-disk folders often diverge from wiki titles. The list is community-curated data, so it is **not shipped in this repository** — fetch it once with:

```sh
python tools/fetch_tiers.py     # writes data/tier_data.json
```

The game never touches the network at runtime. Without the file, tier badges and Tier Sort are simply disabled — nothing breaks.

**Difficulty progress pad** — arcade-style rank and crown totals, aggregated in the engine: an exclusive bucket per performance grade plus Clear / Full Combo / DFC counts, tallied per course and star.

**Hidden note modifier ("doron")** — hides a percentage of the approach lane before the judge, with a soft fade. **F3** opens a live tuner for the hide distance and fade, and overall fade smoothness is the `[hidden] fade_percent` setting in `config.toml`.

**Progress companion (`taiko-stats`)** — **deprecated.** An early side project; left here for reference. It reads the game's `scores.db` read-only and shows the same rank/crown maths with filters the in-game pad does not have: course (including an Oni+Ura merge), ★1–10 or all, and community tier.

```sh
cd progress-companion && pnpm install && launch.bat
```

Full write-ups, including the engine surfaces each feature touches, are in [`docs/features.md`](docs/features.md).

### Building this fork

- **The parent skins are not in this repository.** They are git submodules on an external host. Fetch them with:

  ```sh
  git submodule update --init Skins/PyTaikoGreen Skins/YataiDONRed Skins/YataiDONNijiiro
  ```

  A clone that cannot reach that host still builds and runs the engine; only the Nijiiro-based skins need it.
- **The tier list is fetched, not committed** — see above.
- Pre-built binaries on the [releases page](https://github.com/Yonokid/YataiDON/releases) are upstream's, and do not include the features above. This fork's features need a build from source.
