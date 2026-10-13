# Tier badge — Nijiiro Lua hook

See the full feature write-up: [`tier_list.md`](tier_list.md).

Wired into `Skins/YataiDONNijiiro/Scripts/song_select/song_select.lua`
(`SongSelect:tier_badge` / `draw_tier_badge`, called from `draw_song_board`).

## SongBox fields (C++ Lua bindings)

| Binding | Meaning |
|---|---|
| `box:tier()` | Primary letter (ura if ura-mode + ura tier, else oni) |
| `box:tier_oni` | Oni letter or `""` |
| `box:tier_ura` | Ura letter or `""` |
| `box:tier_for(diff)` | Letter for course 3/4 |
| `box:course_info(d).tier` | Same as `tier_for(d)` |

## Where it draws

1. **Closed wheel board** — gold letter centred just above the song title (`by - 36`).
2. **Open board** — gold letter centred above the Oni/Ura difficulty plate.

Only songs present in `data/tier_data.json` (ESE community list) show a badge.
Empty / missing bindings are skipped (`pcall` + empty-string guard).
