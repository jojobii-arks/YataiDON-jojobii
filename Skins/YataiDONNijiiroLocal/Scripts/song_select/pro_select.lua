-- Pro select v2 — skin drawing for the pro song-select screen.
--
-- Loaded by `require` from Scripts/song_select/song_select.lua (which stays at
-- Lua's 200-local limit, so everything lives here). The engine calls back into
-- `SongSelect:draw_pro(model)` while the pro screen is live; `model` is the
-- ProList usertype (engine side: src/objects/song_select/pro/pro_list.*).
--
-- Every number below comes off the Penpot page `Spec — pro select` (SPEC 1-5,
-- FLOWS 1-9), not off taste:
--   card 1396x84 @ x460, pitch 84, rows FLUSH; the genre's own two tones — the
--   saturated band as the frame, the band's light panel (panel_of) as the plate
--   fills, which is the arcade board's own two-tone treatment (Joseph 2026-10-13);
--   the cursor row's own rim turns gold at 5px (SPEC 3).
--   CHART card: three light-panel plates SCORE(11,118) | LEVEL(135,250) | NAME(391,994),
--     6px seams, 14px inner padding.
--   FOLDER / BACK / FILTER card: ONE plate (11,11,1374,62), name or an icon +
--     label (icon at 33, label at 83) — FLOW 1's folder card, FLOW 2's pair.
--   The visible list is the design's block of 9 rows, y 196..952, so the cursor
--     row sits at y 532 (centre 574) — FLOW 1/2's composition.
--   SELECTED state: the cursor chart's card is NOT replaced. It stays in its slot
--     and keeps its gold cursor rim, and the five-entry DON strip (BACK /
--     SETTINGS / PLAY / PRACTICE / CHANGE DIFF — or the CHANGE DIFF picker while
--     it is open) draws as its own panel in the card slot directly BELOW it, y
--     616..700 — one row's footprint, so the list keeps its flush 9-row rhythm.
--   Left column: count pill (48,196) 380x64, the FLOW 2 chip stack at
--     y 280/318/356, chart details (48,366) 380x336.
--   Filter/Sort dialog: shell (360,190) 1200x700 (FLOW 5).
--
-- Don-chan and the player card are NOT drawn here — they are engine objects
-- (SongSelectPlayer's `chara` and `nameplate`), drawn by the screen through
-- SongSelectPlayer::draw at the skin_config stations song_select_chara_1p /
-- song_select_nameplate_1p, exactly as the wheel draws them.
--
-- The card base is drawn FLAT from the band-colour table. It is deliberately
-- NOT sliced out of `box/bar_genre`: that texture's outer 40px corner columns
-- are transparent and its interior is a pale tint of the band, so slicing it
-- leaves the card's rim showing the backdrop and fills the card with the tint
-- instead of the band the design calls for. The table below is sampled from
-- that same texture's band columns (and matches BOARD_OUTLINE in
-- song_select.lua), so the colours are the arcade's own.

local M = {}

-- ---------------------------------------------------------------- constants
local CARD_X, CARD_W, CARD_H = 460, 1396, 84
local TOP_ROW = 196                          -- design: the list block starts here
local CURSOR_ROW = 4                         -- 5th of 9 rows -> cursor row at y 532
local MIN_OFF, MAX_OFF = -4, 4               -- 9 rows, y 196..952 (design block)
local LX = 48                                -- left column x
local RIM_T = 3                              -- the design's ink rim
local CURSOR_T = 5                           -- cursor gold rim thickness (SPEC 3)

-- genre_index -> board frame, exactly the engine's GENRE_TO_REF_FRAME (enums.h)
local REF = {
    [0] = 0, [1] = 1, [2] = 2, [3] = 8, [4] = 7, [5] = 6, [6] = 5, [7] = 3,
    [8] = 4, [9] = 0, [10] = 0, [11] = 0, [12] = 9, [13] = 10, [14] = 0, [15] = 0,
}

-- The band colour inside each board frame — sampled from box/bar_genre's band
-- columns (x 60 / x 900), so these ARE the arcade colours.
local BAND = {
    [0] = {254, 230, 0}, [1] = {66, 192, 210}, [2] = {255, 144, 211},
    [3] = {204, 138, 234}, [4] = {255, 112, 39}, [5] = {201, 192, 0},
    [6] = {29, 200, 59}, [7] = {254, 192, 0}, [8] = {205, 207, 223},
    [9] = {93, 209, 187}, [10] = {60, 100, 182}, [11] = {231, 224, 218},
    [12] = {255, 90, 109},
}
local BACK_BAND  = {82, 90, 107}             -- back card band, #525A6B (FLOW 2)
local FILTER_BAND = BAND[12]                 -- filter card band, #FF5A6D (FLOW 2)

-- dark rim colours for text (the skin's own BOARD_OUTLINE table)
local RIM = {
    [0] = {18, 72, 76}, [1] = {18, 72, 76}, [2] = {155, 24, 99},
    [3] = {78, 29, 118}, [4] = {150, 31, 0}, [5] = {101, 67, 42},
    [6] = {20, 79, 20}, [7] = {165, 50, 0}, [8] = {38, 52, 73},
    [9] = {18, 72, 76}, [10] = {0, 25, 95}, [11] = {100, 6, 22},
    [12] = {119, 26, 45},
}

-- tier pill colours (taiko.wiki clear-difficulty palette)
local TIER_C = {
    SSS = {185, 63, 234}, SS = {232, 52, 143}, ["S+"] = {219, 31, 0},
    S = {239, 48, 89}, A = {235, 117, 53}, B = {230, 180, 57},
    C = {96, 206, 55}, D = {55, 176, 203}, E = {65, 97, 216},
    F = {127, 127, 127}, X = {173, 173, 173},
}

local INK    = {26, 17, 8}                  -- #1A1108
local TAN    = {247, 194, 160}              -- #F7C2A0
local TAN_D  = {233, 169, 125}              -- #E9A97D — the dialog's cell fills
local GOLD   = {255, 210, 74}               -- #FFD24A
local WHITE  = {255, 255, 255}
local DIV    = {219, 174, 136}              -- #DBAE88, the details panel's dividers

-- Bright 'nijiiro' fills for the left column's status pill and chips. The
-- screen's dark plates (the ink #1A1108 and the navy #232C3D) sat flat next to
-- the vivid genre bands beside them, so the user asked for the arcade's own
-- bright band hues here instead. White text with an ink stroke — the skin's
-- W() house style for anything on a coloured plate — keeps every string
-- legible on them, exactly as it already does on the bright tier pills.
local NIJI_COUNT = {66, 192, 210}          -- #42C0D2 cyan  — the count pill
local NIJI_CHART = {255, 144, 211}         -- #FF90D3 pink  — the ★ / courses chip
local NIJI_TIER  = {204, 138, 234}         -- #CC8AEA violet — the TIER chip
local NIJI_SORT  = {255, 112, 39}          -- #FF7027 orange — the SORT chip

-- Native texture sizes. draw_texture's x2/y2 are ADDITIVE deltas on the natural
-- size (see draw_board in song_select.lua), so art drawn at a design station has
-- to say how much to shrink by. Without this every piece drew at its native
-- size — the course icon is 96x96 and the tier pill 184x40 — whatever box it was
-- placed in.
local NAT = {
    star = {40, 40}, level_number = {48, 48}, branch = {40, 40},
    rank = {40, 40}, crown = {40, 40}, course = {96, 96}, pill = {184, 40},
    back_icon = {128, 128}, option_icon = {128, 128},
}

local COURSE_NAME = { "EASY", "NORMAL", "HARD", "ONI", "URA" }
local CONTROL_NAME = { "BACK", "SETTINGS", "PLAY", "PRACTICE", "CHANGE DIFF" }
local CONTROL_ICON = { true, true, false, false, true }   -- which tiles carry a mark
-- The mark each of those tiles wears. CHANGE DIFF reuses the `course` id at its
-- URA frame (the purple oni) rather than loading diff_sort/diff a second time.
local CONTROL_ART = { "back_icon", "option_icon", nil, nil, "course" }

local COLLECTION_FRAME = { search = 12, recent = 9, favorite = 0, all = 0 }

-- ------------------------------------------------------------------ helpers
local function rgba(c, a) return { c[1], c[2], c[3], a or 255 } end

-- The arcade boards are two-tone: a saturated band with a lighter panel inside it.
-- Sampled off the wheel's own board render (frame 9 band #5DD1BB -> panel #B6EAE0),
-- the panel is the band mixed 55% toward white — consistent to a fraction on every
-- channel, and that is the tint the pro cards' plates wear.
local function panel_of(c)
    return { c[1] + (255 - c[1]) * 0.55, c[2] + (255 - c[2]) * 0.55, c[3] + (255 - c[3]) * 0.55 }
end

local function fmt_num(n)
    local s = tostring(math.floor(n))
    local out = s:reverse():gsub("(%d%d%d)", "%1 "):reverse()
    return (out:gsub("^%s+", ""))
end

local function get_any(sub, name)
    return tex.get_texture("song_select/" .. sub .. "/" .. name)
        or tex.get_texture("song_select/" .. sub .. "/" .. name .. "_en")
        or tex.get_texture("song_select/" .. sub .. "/" .. name .. "_ja")
end

local txt_cache, txt_n = {}, 0
local function T(s, size, fill, outline, thick)
    if not s or s == "" then return nil end
    local sc = tex.get_screen_scale()
    thick = (thick or 3) / sc
    local key = s .. "|" .. size .. "|" .. fill[1] .. "," .. fill[2] .. "," .. fill[3]
             .. "|" .. outline[1] .. "," .. outline[2] .. "," .. outline[3] .. "|" .. thick
    local t = txt_cache[key]
    if not t then
        if txt_n > 500 then txt_cache, txt_n = {}, 0 end
        t = text.create_raw_text(s, size, rgba(fill), rgba(outline), false, thick, 2.0) or false
        txt_cache[key] = t
        txt_n = txt_n + 1
    end
    return t or nil
end

-- draw a text object left-aligned at (x, cy), squeezing to max_w if needed
local function draw_text_at(t, x, cy, fade, max_w)
    if not t then return end
    local w = t.width
    local x2 = 0
    if max_w and w > max_w then
        w = math.max(max_w, w * 0.75)
        x2 = w - t.width
    end
    t:draw({ x = math.floor(x + 0.5), y = math.floor(cy - t.height / 2 + 0.5),
             x2 = x2, fade = fade })
end

-- white-filled text with the appropriate stroke — the skin's own house style for
-- anything sitting on a tan plate (see the wheel's board titles)
local function W(s, size, stroke, thick)
    return T(s, size, WHITE, stroke or INK, thick)
end

-- centred white text inside a box
local function draw_text_box(s, size, fill, outline, thick, x, y, w, h, fade)
    local t = T(s, size, fill, outline, thick)
    if not t then return end
    draw_text_at(t, x + (w - t.width) / 2, y + h / 2, fade, w - 4)
end

-- one art piece at its design station, scaled from its native size
local function draw_art(ids, key, cx, cy, w, h, extra)
    local id = ids[key]
    if not id then return end
    local n = NAT[key]
    extra = extra or {}
    extra.x = math.floor(cx - w / 2 + 0.5)
    extra.y = math.floor(cy - h / 2 + 0.5)
    if n then
        extra.x2 = w - n[1]
        extra.y2 = h - n[2]
    end
    tex.draw_texture(id, extra)
end

-- tex.draw_rect is the ONE draw path the engine routes through
-- virtual_to_screen_x/y — it rescales by (window size / the skin's declared
-- 1920x1080) — while tex.draw_texture and OutlinedText::draw both take RAW
-- pixels. The wheel uses tex.draw_rect ZERO times, for exactly this reason:
-- mixing the two puts a screen's plates on a different scale from its own text
-- and art, and it only shows once the window is not the declared size. It is
-- identity at 1920x1080, which is why every headless shot looked correct and a
-- borderless run at 2560x1440 had the plates grown while the text stayed put.
-- `song_select/pro_ui/fill` is this skin's own 8x8 solid white plate (declared in
-- its texture.json), stretched with x2/y2 and tinted with `color`, which gives an
-- exact fill on the same raw path as everything else. It has to be a WHITE base —
-- every solid-colour texture the stock skins ship is black, and a tint multiplies,
-- so a black plate can never become a colour. (An earlier attempt reused
-- `diff_sort/level_box`, which is a ROUNDED white pill — it pilled every card.)
local FILL_NAT_W, FILL_NAT_H = 8, 8
local FILL_TEX = nil            -- resolved in M._ids()

local function fill_rect(x, y, w, h, c, a)
    if not FILL_TEX then return end
    tex.draw_texture(FILL_TEX, {
        x  = math.floor(x + 0.5),
        y  = math.floor(y + 0.5),
        x2 = math.floor(w + 0.5) - FILL_NAT_W,
        y2 = math.floor(h + 0.5) - FILL_NAT_H,
        color = { c[1], c[2], c[3], 255 },
        fade  = (a or 255) / 255,
    })
end

-- the same fill, for call sites that carry a loose r, g, b, a
local function fill_rgb(x, y, w, h, r, g, b, a)
    fill_rect(x, y, w, h, { r, g, b }, a)
end

-- The tier pill: a flat plate in the tier colour with a 2px ink rim, drawn the
-- same way as the card base. Deliberately NOT `modifier/mod_box` — that art is
-- packed into a sheet and drawn at its native 184x40 by the options panel, and
-- asking the engine to scale it down to the design's 58x40 renders nothing at
-- all. A flat plate is what the design's pill is anyway.
local function draw_tier_pill(ids, tier, cx, cy, w, h, text_size, fade)
    local c = TIER_C[tier]
    if not c then return end
    fill_rect(cx - w / 2, cy - h / 2, w, h, INK, 255 * fade)
    fill_rect(cx - w / 2 + 2, cy - h / 2 + 2, w - 4, h - 4, c, 255 * fade)
    local t = W(tier, text_size or 20, INK, 2)
    if t then draw_text_at(t, cx - t.width / 2, cy, fade) end
end

-- The card base: the genre's own saturated band colour, flat — the board's outer
-- tone. The plates inside carry the band's LIGHT panel tone (panel_of), so a card
-- reads exactly like the arcade board it comes from: saturated band frame with a
-- light panel inside it (Joseph 2026-10-13: the card + details follow the genre,
-- two-tone like the raw Nijiiro board assets).
local function card_base(band, x, y, fade)
    fill_rect(x, y, CARD_W, CARD_H, band, 255 * fade)
end

-- the three plates of a CHART card: SCORE(11,118) | LEVEL(135,250) |
-- NAME(391,994), 6px seams, 11px inset — the SPEC 2 component contract. `fill`
-- is the band's light panel tone, so the plate reads light while the border ring
-- and the 6px seams between the plates stay the saturated band.
local function chart_plates(x, y, fade, fill)
    fill_rect(x + 11, y + 11, 118, 62, fill, 255 * fade)
    fill_rect(x + 135, y + 11, 250, 62, fill, 255 * fade)
    fill_rect(x + 391, y + 11, 994, 62, fill, 255 * fade)
end

-- the single plate a FOLDER / BACK / FILTER card carries (11,11,1374,62); `fill`
-- is the light panel of the card's band (folder = its genre, BACK / FILTER = their
-- own colours), so the card's border ring stays the saturated band.
local function one_plate(x, y, fade, fill)
    fill_rect(x + 11, y + 11, 1374, 62, fill, 255 * fade)
end

-- ink-outline placeholder for a missing value (2px box, 30% ink)
local function placeholder(x, y, w, h, fade)
    local a = 77 * fade
    fill_rgb(x, y, w, 2, INK[1], INK[2], INK[3], a)
    fill_rgb(x, y + h - 2, w, 2, INK[1], INK[2], INK[3], a)
    fill_rgb(x, y, 2, h, INK[1], INK[2], INK[3], a)
    fill_rgb(x + w - 2, y, 2, h, INK[1], INK[2], INK[3], a)
end

-- The screen's cursor mark: the gold C-CLAMP. A C-shaped bracket of gold with a
-- 3px ink outline, hugging each end of whatever the cursor is on — and it is the
-- ONE idiom for the cursor across the whole screen: the browse list's card, the
-- selected chart's controls strip (BACK / SETTINGS / PLAY / PRACTICE / CHANGE
-- DIFF), the CHANGE DIFF picker, and every cell of the Filter/Sort dialog. Nothing
-- else is a clamp, and nothing else wears gold for the cursor.
--
-- The rule at each scale, so a glance at any surface reads the same way:
--   * the clamp hugs the CURSOR ELEMENT's own box — one cap on each edge. It never
--     brackets a row of cells and never a nesting level, so the thing a cap touches
--     is always the thing it means;
--   * the arms reach inward by a quarter of the element's width, floored at 14 and
--     capped at 33 (the card's own arm) — a 72px star cell still shows two clear
--     brackets, while a 1396px card does not grow a bar;
--   * the spine is a fixed 9px of gold (the card's own), because the clamp's WEIGHT
--     is half of what makes it unmistakable, and it does not shrink with the target;
--   * it adapts VERTICALLY: the spine runs the element's full height plus a 4-5px
--     overflow at each end, so a 44px tier pill, a 64px toggle and a 76px CONFIRM
--     button all read as a clamp and never as a rim.
-- Placement: every surface below has the spine flush on the element's edge and the
-- ink outline 3px proud of it, which fits the 4-6px gutters these rows are packed
-- with. The card is the one exception — the design board its mark comes from puts
-- the spine 13px OUTSIDE the card (see draw_cursor_caps), which is why the list
-- draws it last, after every row.
--
-- Why ink-and-gold never collides with the screen's OTHER gold: a gold FILL is an
-- option that is ON (a dialog toggle, its chosen star / tier / sort) and it is a
-- SOLID plate, while the clamp is a HOLLOW bracket whose ink outline runs on both
-- sides of its gold — so even a clamp landing on a gold fill keeps a line of ink
-- between the two golds and still reads as a bracket. The focused DIFFICULTY
-- toggle can be ON and clamped at the same time, and the two stay distinct.
local CLAMP_ARM_MAX, CLAMP_ARM_MIN = 33, 14
local CLAMP_SW, CLAMP_OV_MIN, CLAMP_OV_MAX = 9, 4, 5

-- one cap of the clamp: a C-bracket opening inward, ink outline then gold on top of
-- it. `x_edge` is the element edge the cap hugs; open_right = true hugs the LEFT
-- edge (the bracket's arms point right, into the element), false hugs the RIGHT edge
-- (arms point left). `ox` is how far outside the element the gold spine sits (the
-- card's own design uses 13; every other surface uses 0).
local function clamp_cap(x_edge, y, h, open_right, fade, arm, ov, ox)
    local a = 255 * fade
    local sw = CLAMP_SW
    local spine = open_right and (x_edge - ox) or (x_edge + ox - sw)
    local arm_x = open_right and spine or (spine - (arm - sw))
    local y0, h0 = y - ov, h + 2 * ov
    local y1 = y0 + h0 - sw                      -- the lower arm, flush with the spine
    -- ink first, gold 3px inside it on every side (the card's own 15/9, 39/33 step)
    fill_rect(spine - 3, y0 - 3, sw + 6, h0 + 6, INK, a)
    fill_rect(arm_x - 3, y0 - 3, arm + 6, sw + 6, INK, a)
    fill_rect(arm_x - 3, y1 - 3, arm + 6, sw + 6, INK, a)
    fill_rect(spine, y0, sw, h0, GOLD, a)
    fill_rect(arm_x, y0, arm, sw, GOLD, a)
    fill_rect(arm_x, y1, arm, sw, GOLD, a)
end

-- the clamp on an element of any size: one cap on each edge, flush on them
local function draw_clamp(x, y, w, h, fade)
    local arm = math.max(CLAMP_ARM_MIN, math.min(CLAMP_ARM_MAX, math.floor(w / 4 + 0.5)))
    local ov  = math.max(CLAMP_OV_MIN, math.min(CLAMP_OV_MAX, math.floor(h / 16 + 0.5)))
    clamp_cap(x, y, h, true, fade, arm, ov, 0)
    clamp_cap(x + w, y, h, false, fade, arm, ov, 0)
end

-- ------------------------------------------------------------------- ids
local function resolve_ids(t)
    t.background   = tex.get_texture("song_select/box/background")
    t.star         = get_any("yellow_box", "star")
    t.level_number = get_any("yellow_box", "level_number")
    t.rank         = get_any("yellow_box", "s_crown_outline")
    t.crown        = get_any("diff_select", "difficulty_crown")
    t.course       = get_any("diff_sort", "diff")
    t.branch       = get_any("yellow_box", "branch_indicator")
    t.level_box    = get_any("pro_ui", "fill")           -- our own 8x8 solid white fill plate
    t.back_icon    = get_any("diff_select", "back")     -- the BACK badge, 128x128
    t.option_icon  = get_any("diff_select", "option")   -- the SETTINGS badge, 128x128
end

function M._ids()
    if not M._ids_t then
        M._ids_t = {}
        M._retry = 0
    end
    local t = M._ids_t
    local complete = t.background and t.star and t.level_number and t.rank
                 and t.crown and t.course and t.branch
                 and t.back_icon and t.option_icon and t.level_box
    -- A folder that is not resident yet resolves to nil on the first frames, and
    -- caching that nil would blank the art for the whole session. Retry the
    -- misses for a bounded number of frames instead.
    if not complete and M._retry < 300 then
        M._retry = M._retry + 1
        tex.load_folder("song_select", "box")
        tex.load_folder("song_select", "yellow_box")
        tex.load_folder("song_select", "diff_select")
        tex.load_folder("song_select", "diff_sort")
        tex.load_folder("song_select", "pro_ui")
        resolve_ids(t)
    end
    -- the fill plate is drawn on the raw path, so it has to be visible to
    -- fill_rect before anything paints; re-read each frame so a late folder wins
    FILL_TEX = t.level_box
    return t
end

-- ------------------------------------------------------------------ pieces

-- The cursor: the row's OWN rim turns cursor gold at 5px (SPEC 3, FLOW 2's
-- selected card — its board ring at a 5px inset). No halo, no gap change.
local function draw_cursor_rim(x, y, fade)
    local a = 255 * fade
    fill_rgb(x, y, CARD_W, CURSOR_T, GOLD[1], GOLD[2], GOLD[3], a)
    fill_rgb(x, y + CARD_H - CURSOR_T, CARD_W, CURSOR_T, GOLD[1], GOLD[2], GOLD[3], a)
    fill_rgb(x, y, CURSOR_T, CARD_H, GOLD[1], GOLD[2], GOLD[3], a)
    fill_rgb(x + CARD_W - CURSOR_T, y, CURSOR_T, CARD_H, GOLD[1], GOLD[2], GOLD[3], a)
end

-- The cursor caps on the list's card: the same clamp as everywhere else, at the
-- card's own design numbers — the board is 1428x100 over a 1396x84 card, so the
-- spine sits 13px outside the card, the arms reach 33px in and the gold runs 5px
-- past the card top and bottom (100 tall). The design's 13px outer-corner radius
-- is NOT drawn: the engine exposes no rounded variant of the fill path, so the
-- brackets are straight-edged.
local function draw_cursor_caps(x, y, fade)
    clamp_cap(x, y, CARD_H, true, fade, CLAMP_ARM_MAX, 5, 13)
    clamp_cap(x + CARD_W, y, CARD_H, false, fade, CLAMP_ARM_MAX, 5, 13)
end

-- one chart card — the SCORE / LEVEL / NAME sections at their settled stations
local function draw_chart_card(ids, info, x, y, fade)
    local frame = REF[info.genre_index] or 0
    card_base(BAND[frame] or BAND[0], x, y, fade)
    chart_plates(x, y, fade, panel_of(BAND[frame] or BAND[0]))

    local stroke = RIM[frame] or RIM[0]
    local cy = y + CARD_H / 2

    -- SCORE section — rank art then crown, 14px inside an 11px plate
    local sx = x + 25
    if info.played and info.rank and info.rank > 0 then
        draw_art(ids, "rank", sx + 20, cy, 40, 40, { frame = info.rank - 1, fade = fade })
    else
        placeholder(sx, cy - 20, 40, 40, fade)
    end
    local cx2 = sx + 50
    if info.played and info.crown and info.crown > 0 then
        draw_art(ids, "crown", cx2 + 20, cy, 40, 40, { frame = info.crown, fade = fade })
    else
        placeholder(cx2, cy - 20, 40, 40, fade)
    end

    -- LEVEL section: course icon 56 @ x+149, branch mark, star @ +230,
    -- level number 48 @ +279, tier pill 58x40 @ +342 (SPEC 2 stations)
    local icon_x, icon_y = x + 149, y + 14
    draw_art(ids, "course", icon_x + 28, cy, 56, 56, { frame = info.course or 0, fade = fade })
    if info.branching then
        draw_art(ids, "branch", icon_x + 44, icon_y + 44, 24, 24, { fade = fade })
    end
    draw_art(ids, "star", x + 230, cy, 40, 40, { frame = info.course or 0, fade = fade })
    local lv = math.max(1, math.min(11, info.level or 1))
    draw_art(ids, "level_number", x + 279, cy, 48, 48,
             { frame = (info.course or 0) * 11 + (lv - 1), fade = fade })
    if info.tier and info.tier ~= "" then
        draw_tier_pill(ids, info.tier, x + 342, cy, 58, 40, 20, fade)
    else
        placeholder(x + 313, cy - 20, 58, 40, fade)
    end

    -- NAME section: title from x+405 (14px inside the 391 plate), favourite at the far end
    local name_x, name_w = x + 405, 966
    if info.favorite then name_w = name_w - 46 end
    draw_text_at(W(info.title, 30, stroke, 5), name_x, cy, fade, name_w)
    if info.favorite then
        local star = W("★", 30, INK, 5)
        local sw = star and star.width or 30
        local t = T("★", 30, GOLD, INK, 5)
        if t then draw_text_at(t, x + 1371 - sw, cy, fade) end
    end
end

-- one root folder / collection card — FLOW 1: band + ONE plate + the name
local function draw_folder_card(ids, row, x, y, fade)
    local frame
    if row.kind == "folder" then frame = REF[row.genre_index] or 0
    else frame = COLLECTION_FRAME[row.kind] or 0 end
    card_base(BAND[frame] or BAND[0], x, y, fade)
    one_plate(x, y, fade, panel_of(BAND[frame] or BAND[0]))
    local stroke = RIM[frame] or RIM[0]
    draw_text_at(W(row.name, 30, stroke, 5), x + 30, y + CARD_H / 2, fade, 1300)
end

-- BACK / FILTER-SORT pair rows — FLOW 2: band + ONE plate, icon at 33, label at 83
local function draw_pair_card(ids, kind, x, y, fade)
    local is_back = (kind == "back")
    local band = is_back and BACK_BAND or FILTER_BAND
    card_base(band, x, y, fade)
    one_plate(x, y, fade, panel_of(band))
    local cy = y + CARD_H / 2
    -- the design's icon slot (33,25) 34x34. The skin ships exactly two generic
    -- action badges — diff_select/back_en and diff_select/option_en, the same
    -- pair the controls strip wears for BACK and SETTINGS — so those are what
    -- these two rows carry; there is no filter-specific art to draw.
    draw_art(ids, is_back and "back_icon" or "option_icon", x + 50, cy, 34, 34, { fade = fade })
    local label = is_back and "BACK" or "FILTER/SORT"
    draw_text_at(W(label, 30, INK, 5), x + 83, cy, fade, 800)
end

-- The DON controls strip: plate (11,4,1374,74), five 262x60 tiles at a 276 pitch.
-- Colours and stations off the Penpot strip render: the plate is the dialog's
-- TAN_D and every tile is TAN (the old ink plate + TAN_D tiles sat a step too
-- dark, so the strip read as a row of chocolate buttons). This strip is no longer
-- drawn in the selected card's own slot — it is its own panel BELOW that card (see
-- draw_pro) — so the current tile is marked with the screen's cursor clamp and NOT
-- with a gold fill: with the strip standing alone a gold tile would read as an
-- option that is ON, which nothing here is. The label keeps the size/thickness
-- step that says which entry the cursor is on, and the clamp is drawn AFTER the
-- row (its ink sits 3px proud of the tile, which the next tile must not paint
-- over). BACK / SETTINGS / CHANGE DIFF carry their real badge art — a 32x32 mark
-- on the centre the flat square used to occupy — and their labels centre at the
-- design's tx+138, not the tile centre: an icon tile pushes its label ~7px right
-- (PLAY and PRACTICE, which have no icon, centre on the tile at tx+131).
local function draw_controls(ids, st, x, y, fade)
    local cy = y + 11 + 30                     -- the tile row's own centre (not the card's)
    fill_rect(x + 11, y + 4, 1374, 74, TAN_D, 255 * fade)
    for i = 1, 5 do
        local tx = x + 17 + (i - 1) * 276
        local is_cur = ((st.control or 1) + 1) == i
        fill_rect(tx, y + 11, 262, 60, TAN, 255 * fade)
        local label_x, label_w = tx, 262
        if CONTROL_ICON[i] then
            -- the badge art, scaled from its native 128 or 96, at the design's
            -- 32x32 station; the outline is the tile's own colour so the label
            -- reads as bare ink (the design shows no halo on the tan fill)
            local extra = { fade = fade }
            if CONTROL_ART[i] == "course" then extra.frame = 4 end   -- URA = the purple oni
            draw_art(ids, CONTROL_ART[i], tx + 33, cy, 32, 32, extra)
            label_x, label_w = tx + 66, 145
        end
        draw_text_box(CONTROL_NAME[i], is_cur and 26 or 24, INK, TAN,
                      is_cur and 4 or 3, label_x, y + 11, label_w, 60, fade)
    end
    -- the cursor: the clamp on the current tile, after the whole row
    local cur = (st.control or 1) + 1
    if cur >= 1 and cur <= 5 then
        draw_clamp(x + 17 + (cur - 1) * 276, y + 11, 262, 60, fade)
    end
end

-- The CHANGE DIFF picker: plate (11,4,1374,74), five 262x60 segments at a 276
-- pitch — the same row geometry as the controls strip, so its cursor wears the same
-- clamp. Every station below is measured off the 1396x84 design artboard, where
-- the five segments are identical to the pixel: course icon centred at +34,
-- level number centred at +80, the label's LEFT edge at +104, the 56x30 tier
-- pill at +192..+248. They hang off the SEGMENT's centre (y+41, 7px inside the
-- plate), not the card's centre — the design's pill sits at y 26..55.
local function draw_picker(ids, model, st, x, y, fade)
    local cells = model:picker_cells()
    local seg_top = y + 11
    local cy = seg_top + 30                       -- the segment's own centre, y+41
    fill_rect(x + 11, y + 4, 1374, 74, INK, 255 * fade)
    local cur_x
    for i = 1, 5 do
        local sx = x + 17 + (i - 1) * 276
        local cell = cells[i] or {}
        local present = cell.has_course and true or false
        local is_cur = present and ((st.picker or 0) + 1) == i
        -- A present segment wears the tan plate; an ABSENT one is washed out to
        -- ink so it reads as disabled — the cursor never lands on it (skipped in
        -- move_picker), and the commit refuses it.
        fill_rect(sx, seg_top, 262, 60, present and TAN_D or INK, 255 * fade)
        if is_cur then cur_x = sx end            -- the clamp, drawn after the row:
                                                 -- on the ink plate its outline is
                                                 -- invisible and the gold bracket
                                                 -- alone marks the segment, and the
                                                 -- tier pill keeps its own colour
                                                 -- (a gold FILL would bury it)
        if present then
            draw_art(ids, "course", sx + 34, cy, 40, 40, { frame = i - 1, fade = fade })
            local lv = math.max(1, math.min(11, cell.level or 1))
            draw_art(ids, "level_number", sx + 80, cy, 36, 36,
                     { frame = (i - 1) * 11 + (lv - 1), fade = fade })
            -- left-aligned at the station, not centred in a slot: all five
            -- labels start on the same 104 line in the design
            draw_text_at(T(COURSE_NAME[i], 22, INK, TAN, 3), sx + 104, cy, fade, 84)
            if cell.tier and cell.tier ~= "" then
                draw_tier_pill(ids, cell.tier, sx + 220, cy, 56, 30, 18, fade)
            else
                placeholder(sx + 192, cy - 15, 56, 30, fade)
            end
        else
            -- a difficulty this song does not have: kept visible so the song's
            -- shape still reads, but unmistakably NOT selectable — the plate is
            -- inked out above, the placeholder and label are dimmed, and no clamp
            -- can ever mark it (the cursor skips it).
            placeholder(sx + 14, cy - 20, 40, 40, fade * 0.45)
            draw_text_at(T(COURSE_NAME[i], 22, { 150, 130, 112 }, INK, 2),
                         sx + 104, cy, fade * 0.4)
        end
    end
    if cur_x then draw_clamp(cur_x, seg_top, 262, 60, fade) end
end

-- count pill (48,196) 380x64 — bright nijiiro plate inside the design's ink rim
local function draw_count_pill(ids, n, x, y, fade)
    fill_rect(x, y, 380, 64, INK, 255 * fade)
    fill_rect(x + RIM_T, y + RIM_T, 380 - 2 * RIM_T, 64 - 2 * RIM_T, NIJI_COUNT, 255 * fade)
    local t = W(fmt_num(n), 44, INK, 3)
    if t then draw_text_at(t, x + 22, y + 32, fade) end
    local label = W("CHARTS", 20, INK, 2)
    if label then draw_text_at(label, x + 380 - 16 - label.width, y + 36, fade) end
end

-- one 380x30 status chip: bright nijiiro plate inside an ink rim, white text
local function draw_chip(x, y, text2, colour, fade)
    if not text2 or text2 == "" then return end
    fill_rect(x, y, 380, 30, INK, 255 * fade)
    fill_rect(x + RIM_T, y + RIM_T, 380 - 2 * RIM_T, 30 - 2 * RIM_T, colour, 255 * fade)
    local t = W(text2, 18, INK, 2)
    if t then draw_text_at(t, x + 12, y + 15, fade, 356) end
end

-- The CHART chip's difficulty row (Joseph: "for the ANY CHART pill, we should
-- just show the icons of the easy,normal,hard,oni,ura inline"). The words are
-- gone; the chip now carries the five difficulty TOGGLES inline, in canonical
-- order EASY..URA, each a miniature of the Filter/Sort dialog's own tile:
--   * the SAME art (`diff_sort/diff`, frame = course) the dialog toggles, the
--     chart cards and the CHANGE DIFF picker all draw, so the chip and the tiles
--     can never disagree about what a difficulty looks like;
--   * the SAME vocabulary -- a gold tile is ON, a dimmed tan tile OFF (the
--     dialog's "a gold FILL is an option that is ON"), so the chip and the dialog
--     cannot disagree about what "on" means either.
-- ALL FIVE ON  -> five gold tiles: the old "ANY CHART" state, now read off the art
--                 rather than a phrase.
-- A SUBSET ON  -> the ON difficulties stay gold, the OFF ones dim -- the chip
--                 always shows the whole set, and which of them are lit IS the
--                 filter. Nothing to parse.
-- The ★ filter is a separate axis with no icon of its own, so when one is set it
-- rides as a compact "★N" just right of the row. It was the other half of this
-- same chip before ("★N · ONI + URA"); no difficulty word is reintroduced.
-- Tiles are 26x26 inside the chip's 30px plate with 2px of air (the plate's 3px
-- ink rim swallows the outer pixel), and the art draws at 24 -- the largest the
-- chip's fixed 30px geometry allows. The glyphs are single bold shapes (the oni
-- is a solid demon head), so 24px reads; anything smaller would not.
local DIFF_TILE, DIFF_GAP = 26, 4
local function draw_chart_chip(x, y, d, star, fade)
    fill_rect(x, y, 380, 30, INK, 255 * fade)
    fill_rect(x + RIM_T, y + RIM_T, 380 - 2 * RIM_T, 30 - 2 * RIM_T, NIJI_CHART, 255 * fade)
    local ids = M._ids()
    local tx = x + 11
    local iy = y + math.floor((30 - DIFF_TILE) / 2 + 0.5)
    for i = 1, 5 do
        local on = d[i] and true or false
        local ix = tx + (i - 1) * (DIFF_TILE + DIFF_GAP)
        fill_rect(ix, iy, DIFF_TILE, DIFF_TILE, on and GOLD or TAN_D, (on and 255 or 130) * fade)
        draw_art(ids, "course", ix + DIFF_TILE / 2, iy + DIFF_TILE / 2,
                 DIFF_TILE - 2, DIFF_TILE - 2,
                 { frame = i - 1, fade = on and fade or fade * 0.45 })
    end
    if (star or 0) > 0 then
        local t = W("★" .. star, 18, INK, 2)
        if t then draw_text_at(t, tx + 5 * DIFF_TILE + 4 * DIFF_GAP + 12, y + 15, fade) end
    end
end

-- the left-column status chips — FLOW 1 (root) shows one combined strip,
-- FLOW 2/5 (inside a folder) stack ★ · courses / TIER / SORT
local function draw_chips(st, x, fade)
    local d = st.diff or {}
    local parts = {}
    local names = {}
    for i = 1, 5 do if d[i] then names[#names + 1] = COURSE_NAME[i] end end
    if (st.star or 0) > 0 then parts[#parts + 1] = "★" .. st.star end
    if #names < 5 then parts[#parts + 1] = table.concat(names, " + ") end

    local sort_disp = (st.sort == "A-Z") and "A–Z" or (st.sort or "A–Z")
    local tier_disp = (st.tier == "" or st.tier == nil) and "ALL"
                   or (st.tier == "NONE" and "NONE" or st.tier)

    if st.context == "root" then
        -- filter and sort never touch the root list — one chip says so
        local txt
        if #parts == 0 and st.sort == "A-Z" then
            txt = "NO FILTER · NO SORT"
        else
            local p2 = parts
            if #p2 == 0 then p2[#p2 + 1] = "NO FILTER" end
            p2[#p2 + 1] = "SORT " .. sort_disp
            txt = table.concat(p2, " · ")
        end
        draw_chip(x, 280, txt, NIJI_CHART, fade)
        return 1
    end

    -- the CHART chip: the difficulty SET as inline icons, never words. The ★,
    -- when one is set, rides along as a compact "★N" (see draw_chart_chip).
    -- The ROOT branch above is deliberately left alone: its single strip is a
    -- "filter/sort says so" summary for a list that is never filtered, and it
    -- keeps its words.
    draw_chart_chip(x, 280, st.diff, st.star, fade)
    draw_chip(x, 318, "TIER: " .. tier_disp, NIJI_TIER, fade)
    draw_chip(x, 356, "SORT: " .. sort_disp, NIJI_SORT, fade)
    return 3
end

-- chart details panel (380x336): the board's own band as the rim, its light panel
-- inset 11, text at +26 — FLOW 2's chart details board, at its own y stations.
-- Every line is white-filled with the genre's own rim colour as the stroke (the
-- wheel's house style on a light board panel) and the panel wears the genre's two
-- tones like the card (2026-10-13); the dividers are the band at two-thirds.
local function draw_details(ids, model, st, x, y, fade)
    local h = 336
    local row = model:row(0)
    local info = model:info(0)
    local is_chart = row and row.kind == "chart" and info ~= nil

    local frame
    if is_chart then frame = REF[info.genre_index] or 0
    elseif row and row.kind == "folder" then frame = REF[row.genre_index] or 0
    else frame = COLLECTION_FRAME[row and row.kind or "all"] or 0 end
    local band = BAND[frame] or BAND[0]
    local stroke = RIM[frame] or RIM[0]

    fill_rect(x, y, 380, h, band, 255 * fade)                    -- the panel's band rim
    fill_rect(x + 11, y + 11, 358, h - 22, panel_of(band), 255 * fade)  -- its light panel

    local tx = x + 26
    if is_chart then
        draw_text_at(W("CHART DETAILS", 16, stroke, 2), tx, y + 30, fade)
        draw_text_at(W(info.title, 24, stroke, 3), tx, y + 66, fade, 332)
        draw_text_at(W(info.subtitle, 16, stroke, 2), tx, y + 94, fade, 332)
        fill_rect(tx, y + 122, 332, 2, rgba(band, 170), 255 * fade)

        -- course / star / level / branch / tier (@ +134)
        local ay = y + 134
        draw_art(ids, "course", tx + 22, ay + 22, 44, 44, { frame = info.course or 0, fade = fade })
        if info.branching then
            draw_art(ids, "branch", tx + 46, ay + 36, 20, 20, { fade = fade })
        end
        draw_art(ids, "star", tx + 90, ay + 22, 24, 24, { frame = info.course or 0, fade = fade })
        local lv = math.max(1, math.min(11, info.level or 1))
        draw_art(ids, "level_number", tx + 131, ay + 21, 42, 42,
                 { frame = (info.course or 0) * 11 + (lv - 1), fade = fade })
        if info.tier and info.tier ~= "" then
            draw_tier_pill(ids, info.tier, tx + 186, ay + 20, 52, 36, 18, fade)
        else
            placeholder(tx + 160, ay + 2, 52, 36, fade)
        end

        fill_rect(tx, y + 196, 332, 2, rgba(band, 170), 255 * fade)
        draw_text_at(W("SCORE", 14, stroke, 2), tx, y + 206, fade)
        if info.played then
            draw_text_at(W(fmt_num(info.score), 30, stroke, 3), tx, y + 237, fade, 230)
            draw_text_at(W("GOOD", 16, stroke, 2), tx, y + 258, fade)
            draw_text_at(W(tostring(info.good or 0), 16, stroke, 2), tx + 90, y + 258, fade)
            draw_text_at(W("OK", 16, stroke, 2), tx, y + 278, fade)
            draw_text_at(W(tostring(info.ok or 0), 16, stroke, 2), tx + 90, y + 278, fade)
            draw_text_at(W("MISS", 16, stroke, 2), tx, y + 298, fade)
            draw_text_at(W(tostring(info.miss or 0), 16, stroke, 2), tx + 90, y + 298, fade)
            if info.rank and info.rank > 0 then
                draw_art(ids, "rank", x + 320, y + 224, 40, 40, { frame = info.rank - 1, fade = fade })
            else
                placeholder(x + 300, y + 204, 40, 40, fade)
            end
            if info.crown and info.crown > 0 then
                draw_art(ids, "crown", x + 320, y + 272, 40, 40, { frame = info.crown, fade = fade })
            else
                placeholder(x + 300, y + 252, 40, 40, fade)
            end
        else
            placeholder(tx, y + 220, 230, 34, fade)
            draw_text_at(W("NO SCORE", 20, stroke, 2), tx + 244, y + 237, fade)
        end
    else
        draw_text_at(W("FOLDER", 16, stroke, 2), tx, y + 30, fade)
        draw_text_at(W(row and row.name or "", 26, stroke, 3), tx, y + 66, fade, 332)
        local sub
        if row and row.kind == "folder" then
            sub = fmt_num(row.count or 0) .. " charts under this folder"
        elseif row and row.kind == "all" then
            sub = fmt_num(row.count or 0) .. " charts in the library"
        else
            sub = "a folder, not a chart"
        end
        draw_text_at(W(sub, 16, stroke, 2), tx, y + 94, fade, 332)
    end
end

-- ---- the Filter/Sort dialog (FLOW 5, control rework) ----------------------
-- shell (360,190) 1200x700 -> tan plate inset 3 -> a 62px header band.
-- TWO focus levels (the rework):
--   ENTRY — kat walks the entries top-to-bottom: DIFFICULTY cy 318, STAR cy 418,
--           TIER cy 508, SORT cy 622, and CONFIRM down beside the footer. The
--           entry in focus wears the clamp around its own label tile; don goes
--           into that entry's group. Don on CONFIRM applies and closes.
--   GROUP — kat walks the entry's values and don commits. DIFFICULTY toggles
--           and STAYS (its own exit is the checkmark cell beside the toggles);
--           STAR / TIER / SORT commit and drop back to ENTRY. The value in
--           focus wears the clamp on its own cell.
-- The vocabulary is single and stays that way: a gold FILL is an option that is
-- ON; the gold CLAMP is where the cursor is, and nothing else wears one.
local DLG_X, DLG_Y, DLG_W, DLG_H = 360, 190, 1200, 700

-- The dialog's NESTED clamps. The top level clamps the entry's label tile, the
-- value inside a group clamps its own cell, and entering a group does NOT take the
-- entry's clamp away: both are on screen (DIFFICULTY's label clamp at DLG_X+28..~230,
-- its value clamp further right at DLG_X+290..), so you can always see WHICH entry
-- you are in as well as WHICH value. They cannot be confused with each other: each
-- cap touches exactly the one element it marks (nothing brackets a row or a level),
-- the two sit in their own columns with a clear gap, and only the DIFFICULTY group
-- holds the cursor at all (STAR / TIER / SORT commit and drop straight back to
-- ENTRY). The checkmark cell beside the toggles is the difficulty group's own exit
-- and is a VALUE of that group (val 5), so it takes the same clamp as a toggle —
-- the entry clamp on the label is what says you are still inside DIFFICULTY.
local function dlg_cell(x, y, w, h, label, fillc, text_fill, text_size, outline, fade, fill_a)
    fill_rect(x, y, w, h, fillc or TAN_D, (fill_a or 255) * fade)
    local t = T(label, text_size or 24, text_fill or WHITE, outline or INK,
                (text_size or 24) >= 24 and 3 or 2)
    if t then draw_text_at(t, x + (w - t.width) / 2, y + h / 2, fade, w - 4) end
end

-- a pixel checkmark, drawn from unit cells so it needs no glyph in the font.
-- 7 x 5 cells: the down stroke steps to the bottom then the up stroke climbs.
local function dlg_check(x, y, w, h, colour, fade)
    local u = 7
    local pts = { { 0, 2 }, { 1, 3 }, { 2, 4 }, { 3, 3 }, { 4, 2 }, { 5, 1 }, { 6, 0 } }
    local x0 = x + (w - 7 * u) / 2
    local y0 = y + (h - 5 * u) / 2
    for _, p in ipairs(pts) do
        fill_rgb(x0 + p[1] * u, y0 + p[2] * u, u, u,
                 colour[1], colour[2], colour[3], 255 * fade)
    end
end

-- an entry's label; the ENTRY-level cursor (and the entry you are inside, while a
-- group is open) clamps the label tile, which is how the top level shows where you
-- are. 48 tall rather than the old rim's 44 so the clamp's arms clear the text.
local function dlg_entry_label(text2, cy, active, fade)
    local t = W(text2, 26, INK, 3)
    draw_text_at(t, DLG_X + 40, cy, fade)
    if active then
        draw_clamp(DLG_X + 28, cy - 24, (t and t.width or 160) + 24, 48, fade)
    end
end

local function draw_filter_dialog(model, st, fade)
    local ids = M._ids()   -- the difficulty toggles draw the real course art
    local focus = st.filter_focus or "entry"
    local cur = st.filter_cursor or 0     -- entry 0..4, and the entered entry while in a group
    local val = st.filter_value or 0      -- index inside the entered group
    local entry_focus = (focus == "entry")
    local group_focus = (focus == "group")

    fill_rect(0, 0, 1920, 1080, INK, 175 * fade)                       -- scrim
    fill_rect(DLG_X, DLG_Y, DLG_W, DLG_H, INK, 255 * fade)             -- shell
    fill_rect(DLG_X + 3, DLG_Y + 3, DLG_W - 6, DLG_H - 6, TAN, 255 * fade)
    fill_rect(DLG_X + 3, DLG_Y + 3, DLG_W - 6, 62, TAN_D, 255 * fade)  -- header
    draw_text_at(W("FILTER/SORT", 30, INK, 3), DLG_X + 36, DLG_Y + 34, fade)

    -- DIFFICULTY — a gold tile means ON, a tan tile OFF. The clamp is on the
    -- entry's LABEL whenever DIFFICULTY is the focused entry (at both focus levels
    -- — see the nested-clamp note above), and a second clamp goes on the value in
    -- focus inside the group.
    dlg_entry_label("DIFFICULTY", DLG_Y + 128, cur == 0, fade)
    local d = st.diff or {}
    for i = 1, 5 do
        local x = DLG_X + 290 + (i - 1) * 162
        local y = DLG_Y + 96
        local on = d[i] and true or false
        fill_rect(x, y, 158, 64, on and GOLD or TAN_D, 255 * fade)
        -- the toggle's real art (frame = course index) so the tile says WHICH
        -- course it is; whether it is on is the tile fill behind the icon
        draw_art(ids, "course", x + 30, y + 32, 34, 34, { frame = i - 1, fade = fade })
        local t = T(COURSE_NAME[i], 22, INK, on and GOLD or TAN_D, 3)
        if t then draw_text_at(t, x + 50 + (100 - t.width) / 2, y + 32, fade) end
    end
    -- The difficulty group's own exit: a checkmark cell to the side of the
    -- toggles. It is the only way out of the group (don toggles and stays), and
    -- don on it returns to the top level with the cursor on DIFFICULTY.
    local ckx, cky = DLG_X + 1104, DLG_Y + 96
    fill_rect(ckx, cky, 72, 64, TAN_D, 255 * fade)
    dlg_check(ckx, cky, 72, 64, INK, fade)
    -- the group's value clamp, drawn after every tile AND the checkmark cell, so
    -- nothing paints over it (the clamps sit in the row's 4-8px gutters)
    if group_focus and cur == 0 then
        if val == 5 then
            draw_clamp(ckx, cky, 72, 64, fade)
        elseif val >= 0 and val <= 4 then
            draw_clamp(DLG_X + 290 + val * 162, DLG_Y + 96, 158, 64, fade)
        end
    end

    -- STAR — ALL, then 1..10 (no 10+ bucket: this library tops out at ★10). The
    -- chosen star is the gold fill; the clamp is the cursor alone.
    dlg_entry_label("STAR", DLG_Y + 228, cur == 1, fade)
    local star = st.star or 0
    for n = 0, 10 do
        local x = (n == 0) and (DLG_X + 290) or (DLG_X + 367 + (n - 1) * 77)
        local label = (n == 0) and "ALL" or tostring(n)
        dlg_cell(x, DLG_Y + 196, 72, 64, label,
                 (star == n) and GOLD or TAN_D, WHITE, nil, INK, fade)
    end
    if group_focus and cur == 1 and val >= 0 and val <= 10 then
        local x = (val == 0) and (DLG_X + 290) or (DLG_X + 367 + (val - 1) * 77)
        draw_clamp(x, DLG_Y + 196, 72, 64, fade)
    end

    -- TIER — at a chosen star, that level's own set (never generated: the sets
    -- are not monotonic). With STAR=ALL there is no set to offer — a tier needs
    -- a star level — so only ALL and NONE show.
    dlg_entry_label("TIER", DLG_Y + 318, cur == 2, fade)
    local tier_cells = { "ALL" }
    if star > 0 then
        for _, tv in ipairs(model:tier_set(star)) do tier_cells[#tier_cells + 1] = tv end
    end
    tier_cells[#tier_cells + 1] = "None"
    local cx = DLG_X + 290                     -- flowing: ALL, the level's letters, None last
    local tier_sel_x, tier_sel_w
    for idx, tv in ipairs(tier_cells) do
        local w = (tv == "ALL" or tv == "None") and 76 or 62
        local x = cx
        cx = cx + w + 4
        local shown = (tv == "None") and "NONE" or tv
        local is_cur = (st.tier == "" and tv == "ALL")
                    or (st.tier == "NONE" and tv == "None")
                    or (st.tier == tv)
        -- ALL and None have no colour of their own, so they take the same yellow
        -- fill every other row uses for its chosen value. A graded tier CANNOT:
        -- its fill IS its identity (SSS purple, B mustard), and B's #E6B439 is
        -- near enough to the cursor gold #FFD24A that a yellow fill would read as
        -- one more on-state. So the chosen tier instead keeps full strength while
        -- the unchosen ones dim against the plate.
        local fillc, cell_a
        if tv == "ALL" or tv == "None" then
            fillc, cell_a = is_cur and GOLD or TAN_D, 255
        else
            fillc, cell_a = TIER_C[tv] or TAN_D, is_cur and 255 or 150
        end
        dlg_cell(x, DLG_Y + 296, w, 44, shown, fillc, WHITE, 22, INK, fade, cell_a)
        -- the group's value cursor tracks the same order the model builds the
        -- tier list in (ALL, the level's letters, NONE), so idx - 1 is the index
        if group_focus and cur == 2 and val == idx - 1 then
            tier_sel_x, tier_sel_w = x, w
        end
    end
    if tier_sel_x then draw_clamp(tier_sel_x, DLG_Y + 296, tier_sel_w, 44, fade) end

    -- SORT — gold is the live sort value, the clamp is the cursor (same split).
    -- LEVEL is the tier ladder ACROSS levels: ★10 hardest-tier-first down to ★1.
    dlg_entry_label("SORT", DLG_Y + 432, cur == 3, fade)
    local sort_disp = (st.sort == "A-Z") and "A–Z" or (st.sort or "A–Z")
    local sort_cells = { "SCORE", "CROWN", "A–Z", "LEVEL" }
    local sort_sel_x
    for i, sv in ipairs(sort_cells) do
        local x = DLG_X + 290 + (i - 1) * 172
        dlg_cell(x, DLG_Y + 396, 166, 64, sv, (sv == sort_disp) and GOLD or TAN_D,
                 WHITE, 24, INK, fade)
        if group_focus and cur == 3 and val == i - 1 then sort_sel_x = x end
    end
    if sort_sel_x then draw_clamp(sort_sel_x, DLG_Y + 396, 166, 64, fade) end

    -- footer — the live match count (a short list reads as filtered, not broken),
    -- and the CONFIRM entry beside it: don here applies and closes the dialog
    fill_rect(DLG_X + 40, DLG_Y + 500, 700, 76, TAN_D, 255 * fade)
    local match
    if st.context == "root" then
        match = "filter and sort apply inside a folder"
    elseif st.building then
        match = "counting…"
    else
        match = fmt_num(st.charts or 0) .. " charts match"
    end
    draw_text_at(W(match, 24, INK, 3), DLG_X + 64, DLG_Y + 538, fade, 700 - 48)

    local conf_x, conf_y, conf_w, conf_h = DLG_X + 800, DLG_Y + 500, 360, 76
    dlg_cell(conf_x, conf_y, conf_w, conf_h, "CONFIRM", TAN_D, WHITE, 26, INK, fade)
    if entry_focus and cur == 4 then draw_clamp(conf_x, conf_y, conf_w, conf_h, fade) end
end

-- NO footer control hints. FLOW 1 / FLOW 5 carried two grey pills here -- a
-- left "SELECT   ← →" and a right "DECIDE   DON" -- and they were the ONLY
-- SELECT/DECIDE on this screen (see the note at the end of M.draw). Joseph:
-- "select and decide are still visible at the bottom. we should remove those
-- hints." They are gone; the wheel's own control guide is untouched (this
-- function was pro-only).

-- -------------------------------------------------------------------- draw
function M.draw(self, model)
    if not model then return false end
    local ids = M._ids()
    local st = model:state()

    -- backdrop: focused genre (root follows the cursor's folder)
    local bg_frame = 0
    if st.context == "root" then
        local r0 = model:row(0)
        if r0 and r0.kind == "folder" then bg_frame = REF[r0.genre_index] or 0 end
    else
        bg_frame = REF[st.genre_index or 0] or 0
    end
    if ids.background then
        tex.draw_texture(ids.background, { frame = bg_frame, x = 0, y = 0 })
    else
        fill_rgb(0, 0, 1920, 1080, 40, 40, 44, 255)
    end

    -- header: art band (the design's own placeholder slot) + the open context's name
    fill_rect(56, 24, 352, 80, INK, 70)
    draw_text_at(T("Select a song", 40, { 232, 64, 49 }, WHITE, 4), 60, 64, 1.0, 400)
    draw_text_at(W("SONG SELECT — " .. (st.label or "root"), 26, INK, 3), 60, 131, 1.0, 900)

    if st.building and not st.filter_open then
        draw_text_at(W("BUILDING LIST…", 36, INK, 3), LX, 700, 1.0)
        return true
    end

    -- left column
    local focused = model:row(0)
    local pill_n = st.charts or 0
    if st.context == "root" and focused then pill_n = focused.count or 0 end
    draw_count_pill(ids, pill_n, LX, 196, 1.0)
    local nchips = draw_chips(st, LX, 1.0)
    draw_details(ids, model, st, LX, (nchips >= 3) and 394 or 366, 1.0)

    -- the list: the cursor row's own slot is the vertical centre of the block
    local cursor_top = TOP_ROW + CURSOR_ROW * CARD_H          -- = 532
    local cursor_drawn = false                                -- did the cursor row paint?
    for off = MIN_OFF, MAX_OFF do
        local row = model:row(off)
        if row then
            local x = CARD_X
            local y = cursor_top + off * CARD_H
            local is_cursor = (off == 0)
            if row.kind == "chart" then
                -- A CHART row ALWAYS draws its own card, selected or not. The
                -- selected state's submenu is a SEPARATE panel in the card slot
                -- below (see the block after this loop), so the chart you are
                -- acting on keeps its slot and its look — the same card, the same
                -- gold cursor rim — and the list never loses it in place.
                -- `info` is resolve_display, so a committed CHANGE DIFF swap shows
                -- on the card here exactly as it already does in the details panel.
                local info = model:info(off)
                if info then draw_chart_card(ids, info, x, y, 1.0) end
            elseif row.kind == "folder" or row.kind == "search" or row.kind == "recent"
                or row.kind == "favorite" or row.kind == "all" then
                draw_folder_card(ids, row, x, y, 1.0)
            else
                draw_pair_card(ids, row.kind, x, y, 1.0)
            end
            if is_cursor then
                draw_cursor_rim(x, y, 1.0)
                cursor_drawn = true
            end
        end
    end

    -- The selected chart's submenu — BACK / SETTINGS / PLAY / PRACTICE / CHANGE
    -- DIFF, or the CHANGE DIFF picker while it is open — drawn as its own panel in
    -- the card slot DIRECTLY BELOW the cursor card (the card itself stays put
    -- above). "Below" is read as the next slot of the list, not as a gap and not as
    -- a push:
    --   * the rows are FLUSH at an 84px pitch with no gap to sit in (the design's
    --     block of 9), so a gap reading does not exist;
    --   * pushing the rows below down would shove the bottom row of the block into
    --     the footer hints, and it would break the list's own composition;
    --   * the slot below is the row nearest the card you are acting on, and the
    --     panel fills that WHOLE slot (the ink footprint below, then the strip's own
    --     plate inset 11 inside it), so the panel occupies exactly one row's
    --     footprint: the list keeps its flush 9-row rhythm, the card keeps its
    --     slot, and only the single adjacent row is covered. The plate is inset
    --     (11,4) inside that footprint, so without the ink fill behind it the row
    --     below would show through in an 11px column down each side.
    -- Room: the cursor row is PINNED — TOP_ROW + CURSOR_ROW * CARD_H is a constant,
    -- so the card is always at y 532 and this panel always at 616..700, well clear
    -- of the screen bottom, wherever the cursor is in the list. The guard below is
    -- for a future layout that moves the pinned row (or a hand-authored contrast):
    -- it flips the panel ABOVE the card rather than letting it run off-screen.
    if st.selected then
        local sub_y = cursor_top + CARD_H
        if sub_y + CARD_H > 1014 then sub_y = cursor_top - CARD_H end
        fill_rect(CARD_X, sub_y, CARD_W, CARD_H, INK, 255)
        if st.picker_open then
            draw_picker(ids, model, st, CARD_X, sub_y, 1.0)
        else
            draw_controls(ids, st, CARD_X, sub_y, 1.0)
        end
    end

    -- The caps overflow the cursor row: the design's board is 100 tall over an
    -- 84px card, so they reach 8px above AND below it. This loop paints rows in
    -- order, which means the row below the cursor lands AFTER it and clips their
    -- bottom arms — so the caps are drawn here, once, after every row. They still
    -- sit under the footer and the dialog, which are painted below. They belong
    -- to the plain card alone: the Filter/Sort dialog covers the list, and in the
    -- selected state the submenu panel occupies the card slot immediately below,
    -- which the caps' own bottom arms (8px past the card) would land on — so the
    -- selected state suppresses them and the card's gold cursor rim is what marks it.
    -- They show for every other kind of cursor row — chart, BACK, FILTER — in both
    -- the browse and the filtered/zero-result states.
    if cursor_drawn and not st.selected and not st.filter_open then
        draw_cursor_caps(CARD_X, cursor_top, 1.0)
    end

    -- No control-hint footer (removed at Joseph's request). The SELECT/DECIDE
    -- pills that used to sit at y 1014 lived HERE, in this pro-only draw -- not in
    -- the wheel's `SongSelect:draw_overlays`, which this screen never calls
    -- (ProSongSelectScreen::draw runs script->draw_pro and returns; song_select.cpp
    -- calls draw_overlays only from SongSelectScreen::draw). The wheel's own
    -- guide_labels are therefore inert on pro by construction, and its hints stay
    -- exactly as they were on the wheel.
    if st.filter_open then draw_filter_dialog(model, st, 1.0) end
    return true
end

return M
