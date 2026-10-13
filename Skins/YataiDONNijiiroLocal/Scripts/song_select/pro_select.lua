-- Pro select v2 — skin drawing for the pro song-select screen.
--
-- Loaded by `require` from Scripts/song_select/song_select.lua (which stays at
-- Lua's 200-local limit, so everything lives here). The engine calls back into
-- `SongSelect:draw_pro(model)` while the pro screen is live; `model` is the
-- ProList usertype (engine side: src/objects/song_select/pro/pro_list.*).
--
-- The settled design (vault `Handoff - YataiDON Pro Select v2.md`, chart card
-- note, and the Penpot page `Spec — pro select`, FLOWS 1-9) drives every number
-- below:
--   card 1396x84 @ x460, rows flush at 84, cursor row pinned at the vertical
--   centre; 11px inset band frame, 6px seams; three sections
--   SCORE(11,118) | LEVEL(135,250) | NAME(391,994); 14px inner padding.
--   Cursor = the row's OWN rim in cursor gold at 5px (SPEC 3) — never a halo.
--   TAB / the FILTER/SORT row opens the dialog at (360,190) 1200x700 (FLOW 5).

local M = {}

-- ---------------------------------------------------------------- constants
local CARD_X, CARD_W, CARD_H = 460, 1396, 84
local CENTER_TOP = 498                      -- top of the cursor row (centre 540)
local LX = 48                               -- left column x
local CURSOR_T = 5                          -- cursor gold rim thickness (SPEC 3)

-- genre_index -> board/backdrop frame (mirrors the engine's GENRE_TO_REF_FRAME,
-- with dan -> 10 and search handled by the caller)
local REF = {
    [0] = 0, [1] = 1, [2] = 2, [3] = 8, [4] = 7, [5] = 6, [6] = 5, [7] = 3,
    [8] = 4, [9] = 0, [10] = 0, [11] = 0, [12] = 9, [13] = 10, [14] = 0, [15] = 0,
}

-- band colours (Spec 4 / arcade art sampled) for the solid fills
local BAND = {
    [0] = {254, 230, 0}, [1] = {66, 192, 210}, [2] = {255, 144, 211},
    [3] = {204, 138, 234}, [4] = {255, 112, 39}, [5] = {201, 192, 0},
    [6] = {29, 200, 59}, [7] = {254, 192, 0}, [8] = {205, 207, 223},
    [9] = {93, 209, 187}, [10] = {60, 100, 182}, [11] = {231, 224, 218},
    [12] = {255, 90, 109},
}

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

local COURSE_NAME = { "EASY", "NORMAL", "HARD", "ONI", "URA" }
local CONTROL_NAME = { "BACK", "SETTINGS", "PLAY", "PRACTICE", "CHANGE DIFF" }
local CONTROL_ICON = { true, true, false, false, true }   -- which tiles carry a mark

local COLLECTION_FRAME = { search = 12, recent = 9, favorite = 0, all = 0 }

-- ------------------------------------------------------------------ helpers
local function rgba(c, a) return { c[1], c[2], c[3], a or 255 } end

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

-- centred text inside a box (x, y, w, h)
local function draw_text_box(s, size, fill, outline, thick, x, y, w, h, fade)
    local t = T(s, size, fill, outline, thick)
    if not t then return end
    draw_text_at(t, x + (w - t.width) / 2, y + h / 2, fade, w - 4)
end

local function draw_centered(id, cx, cy, w, h, extra)
    extra = extra or {}
    extra.x = cx - w / 2
    extra.y = cy - h / 2
    tex.draw_texture(id, extra)
end

local function fill_rect(x, y, w, h, c, a)
    tex.draw_rect(math.floor(x + 0.5), math.floor(y + 0.5), math.floor(w + 0.5),
                  math.floor(h + 0.5), c[1], c[2], c[3], a)
end

-- One 1396x84 card base: the bar_genre art sliced to w x h (rounded corners,
-- genre band and outline baked in). Centre rows are vertically uniform.
local function slice_card(id, frame, x, y, w, h, fade)
    if not id then return end
    local CORNER, BAND, PANEL = 40, 43, 794
    local src_y, src_h = 148, 52
    local cols
    local inner = w - 2 * CORNER
    local band = math.floor(inner * BAND / (2 * BAND + PANEL) + 0.5)
    local panel = inner - 2 * band
    if w >= 2 * CORNER then
        cols = {
            { 0, CORNER, x, CORNER },
            { CORNER + BAND / 2 - 4, 8, x + CORNER, band },
            { 960 / 2 - 4, 8, x + CORNER + band, panel },
            { 960 - CORNER - BAND / 2 - 4, 8, x + CORNER + band + panel, band },
            { 960 - CORNER, CORNER, x + w - CORNER, CORNER },
        }
    else
        cols = { { 960 / 2 - 4, 8, x, w } }
    end
    for _, c in ipairs(cols) do
        if c[4] > 0 then
            tex.draw_texture(id, { frame = frame, x = c[3], y = y, x2 = c[4] - 960, y2 = h - 352,
                                   fade = fade, src = { x = c[1], y = src_y, width = c[2], height = src_h } })
        end
    end
end

-- the three tan plates of a card: SCORE(11,118) | LEVEL(135,250) | NAME(391,994),
-- 6px seams, 11px inset — the SPEC 2 component contract.
local function card_plates(x, y, fade)
    tex.draw_rect(x + 11, y + 11, 118, 62, TAN[1], TAN[2], TAN[3], 255 * fade)
    tex.draw_rect(x + 135, y + 11, 250, 62, TAN[1], TAN[2], TAN[3], 255 * fade)
    tex.draw_rect(x + 391, y + 11, 994, 62, TAN[1], TAN[2], TAN[3], 255 * fade)
end

-- ink-outline placeholder for a missing value (2px box, 30% ink)
local function placeholder(x, y, w, h, fade)
    local a = 77 * fade
    tex.draw_rect(x, y, w, 2, INK[1], INK[2], INK[3], a)
    tex.draw_rect(x, y + h - 2, w, 2, INK[1], INK[2], INK[3], a)
    tex.draw_rect(x, y, 2, h, INK[1], INK[2], INK[3], a)
    tex.draw_rect(x + w - 2, y, 2, h, INK[1], INK[2], INK[3], a)
end

-- ------------------------------------------------------------------- ids
function M._ids()
    if M._ids_t then return M._ids_t end
    tex.load_folder("song_select", "box")
    tex.load_folder("song_select", "yellow_box")
    tex.load_folder("song_select", "diff_select")
    tex.load_folder("song_select", "diff_sort")
    tex.load_folder("song_select", "modifier")
    local t = {}
    t.background   = tex.get_texture("song_select/box/background")
    t.bar_genre    = tex.get_texture("song_select/box/bar_genre")
    t.star         = get_any("yellow_box", "star")
    t.level_number = get_any("yellow_box", "level_number")
    t.rank         = get_any("yellow_box", "s_crown_outline")
    t.crown        = get_any("diff_select", "difficulty_crown")
    t.course       = get_any("diff_sort", "diff")
    t.branch       = get_any("yellow_box", "branch_indicator")
    t.pill         = get_any("modifier", "mod_box")
    M._ids_t = t
    return t
end

-- ------------------------------------------------------------------ pieces

-- The cursor: the row's OWN rim turns cursor gold at 5px (SPEC 3, and FLOW 2's
-- selected card — its board ring at a 5px band inset). No halo, no gap change.
local function draw_cursor_rim(x, y, fade)
    local a = 255 * fade
    tex.draw_rect(x, y, CARD_W, CURSOR_T, GOLD[1], GOLD[2], GOLD[3], a)
    tex.draw_rect(x, y + CARD_H - CURSOR_T, CARD_W, CURSOR_T, GOLD[1], GOLD[2], GOLD[3], a)
    tex.draw_rect(x, y, CURSOR_T, CARD_H, GOLD[1], GOLD[2], GOLD[3], a)
    tex.draw_rect(x + CARD_W - CURSOR_T, y, CURSOR_T, CARD_H, GOLD[1], GOLD[2], GOLD[3], a)
end

-- one chart card — the SCORE / LEVEL / NAME sections at their settled stations
local function draw_chart_card(ids, info, x, y, fade)
    local frame = REF[info.genre_index] or 0
    slice_card(ids.bar_genre, frame, x, y, CARD_W, CARD_H, fade)
    card_plates(x, y, fade)

    local rim = RIM[frame] or RIM[0]
    local cy = y + CARD_H / 2

    -- SCORE section — rank art then crown, 14px inside an 11px plate
    local sx = x + 25
    if info.played and info.rank and info.rank > 0 then
        draw_centered(ids.rank, sx + 20, cy, 40, 40, { frame = info.rank - 1, fade = fade })
    else
        placeholder(sx, cy - 20, 40, 40, fade)
    end
    local cx2 = sx + 50
    if info.played and info.crown and info.crown > 0 then
        draw_centered(ids.crown, cx2 + 20, cy, 40, 40, { frame = info.crown, fade = fade })
    else
        placeholder(cx2, cy - 20, 40, 40, fade)
    end

    -- LEVEL section: course icon 56 @ x+149, branch mark, star @ +230,
    -- level number 48 @ +279, tier pill 58x40 @ +342 (SPEC 2 stations)
    local icon_x, icon_y = x + 149, y + 14
    if info.course and ids.course then
        draw_centered(ids.course, icon_x + 28, cy, 56, 56, { frame = info.course, fade = fade })
    end
    if info.branching and ids.branch then
        draw_centered(ids.branch, icon_x + 56 - 12, icon_y + 56 - 12, 24, 24, { fade = fade })
    end
    if ids.star then
        draw_centered(ids.star, x + 230, cy, 40, 40, { frame = info.course or 0, fade = fade })
    end
    local lv = math.max(1, math.min(11, info.level or 1))
    if ids.level_number then
        draw_centered(ids.level_number, x + 279, cy, 48, 48,
                      { frame = (info.course or 0) * 11 + (lv - 1), fade = fade })
    end
    if info.tier and info.tier ~= "" then
        local c = TIER_C[info.tier]
        if c and ids.pill then
            draw_centered(ids.pill, x + 342, cy, 58, 40, { color = rgba(c), fade = fade })
            draw_text_box(info.tier, 20, { 255, 255, 255 }, INK, 2, x + 313, y + 22, 58, 40, fade)
        end
    else
        placeholder(x + 313, cy - 20, 58, 40, fade)
    end

    -- NAME section: title from x+405 (14px inside the 391 plate), favourite at the far end
    local name_x, name_w = x + 405, 966
    if info.favorite then name_w = name_w - 46 end
    local title = T(info.title, 30, { 255, 255, 255 }, rim, 5)
    draw_text_at(title, name_x, cy, fade, name_w)
    if info.favorite then
        local star = T("★", 30, GOLD, INK, 5)
        local sw = star and star.width or 30
        draw_text_at(star, x + 1371 - sw, cy, fade)
    end
end

-- one root folder / collection card
local function draw_folder_card(ids, row, x, y, fade)
    local frame
    if row.kind == "folder" then frame = REF[row.genre_index] or 0
    else frame = COLLECTION_FRAME[row.kind] or 0 end
    slice_card(ids.bar_genre, frame, x, y, CARD_W, CARD_H, fade)
    card_plates(x, y, fade)
    local rim = RIM[frame] or RIM[0]
    local cy = y + CARD_H / 2

    local t = T(row.name, 30, { 255, 255, 255 }, rim, 5)
    draw_text_at(t, x + 25, cy, fade, 1200)

    local meta
    if row.kind == "search" then meta = "find a chart"
    elseif row.kind == "recent" then meta = "recently played"
    elseif row.kind == "favorite" then meta = "your stars"
    else meta = fmt_num(row.count or 0) .. " charts" end
    local mt = T(meta, 24, { 60, 50, 40 }, rim, 3)
    if mt then
        draw_text_at(mt, x + 1371 - mt.width, cy, fade)
    end
end

-- BACK / FILTER-SORT pair rows
local function draw_pair_card(ids, kind, x, y, fade)
    local frame = (kind == "back") and 0 or 12
    slice_card(ids.bar_genre, frame, x, y, CARD_W, CARD_H, fade)
    card_plates(x, y, fade)
    local rim = RIM[frame] or RIM[0]
    local cy = y + CARD_H / 2
    local label = (kind == "back") and "BACK" or "FILTER/SORT"
    local t = T(label, 30, { 255, 255, 255 }, rim, 5)
    draw_text_at(t, x + 25, cy, fade, 900)
    local sub = (kind == "back") and "leave this folder" or "opens the filter + sort dialog"
    local st2 = T(sub, 22, { 60, 50, 40 }, rim, 3)
    if st2 then draw_text_at(st2, x + 1371 - st2.width, cy, fade) end
end

-- the DON controls strip: plate (11,4,1374,74), five 262x60 tiles at a 276 pitch
local function draw_controls(ids, st, x, y, fade)
    fill_rect(x + 11, y + 4, 1374, 74, INK, 255 * fade)
    for i = 1, 5 do
        local tx = x + 17 + (i - 1) * 276
        local is_cur = ((st.control or 1) + 1) == i
        fill_rect(tx, y + 11, 262, 60, is_cur and GOLD or TAN_D, 255 * fade)
        local label_x, label_w = tx, 262
        if CONTROL_ICON[i] then
            -- marked tile: a 34x34 mark at +16, the label in the remaining 204
            fill_rect(tx + 16, y + 24, 34, 34, INK, 200 * fade)
            label_x, label_w = tx + 58, 204
        end
        draw_text_box(CONTROL_NAME[i], is_cur and 26 or 24, INK, TAN,
                      is_cur and 4 or 3, label_x, y + 11, label_w, 60, fade)
    end
end

-- the CHANGE DIFF picker: plate (11,4,1374,74), five 262x60 segments at a 276
-- pitch — mark @ +14, level @ +62, label @ +104, tier pill 56x30 @ +192
local function draw_picker(ids, model, st, x, y, fade)
    local cells = model:picker_cells()
    local cy = y + CARD_H / 2
    fill_rect(x + 11, y + 4, 1374, 74, INK, 255 * fade)
    for i = 1, 5 do
        local sx = x + 17 + (i - 1) * 276
        local cell = cells[i] or {}
        local is_cur = ((st.picker or 0) + 1) == i
        fill_rect(sx, y + 11, 262, 60, is_cur and GOLD or TAN_D, 255 * fade)
        if cell.has_course then
            if ids.course then
                draw_centered(ids.course, sx + 51, cy, 40, 40, { frame = i - 1, fade = fade })
            end
            local lv = math.max(1, math.min(11, cell.level or 1))
            if ids.level_number then
                draw_centered(ids.level_number, sx + 97, cy, 36, 36,
                              { frame = (i - 1) * 11 + (lv - 1), fade = fade })
            end
            draw_text_box(COURSE_NAME[i], 22, INK, TAN, 3, sx + 104, y + 11, 86, 60, fade)
            if cell.tier and cell.tier ~= "" then
                local c = TIER_C[cell.tier]
                if c and ids.pill then
                    draw_centered(ids.pill, sx + 237, cy, 56, 30, { color = rgba(c), fade = fade })
                end
            else
                placeholder(sx + 209, cy - 15, 56, 30, fade)
            end
        else
            -- a difficulty this song does not have: the empty state
            placeholder(sx + 14, cy - 20, 40, 40, fade)
            draw_text_box(COURSE_NAME[i], 20, { 150, 130, 112 }, INK, 2,
                          sx + 100, y + 11, 130, 60, fade * 0.8)
        end
    end
end

-- count pill (48,196) 380x64
local function draw_count_pill(ids, n, x, y, fade)
    fill_rect(x, y, 380, 64, INK, 255 * fade)
    fill_rect(x + 3, y + 3, 374, 58, { 35, 44, 61 }, 255 * fade)
    local t = T(fmt_num(n), 44, GOLD, INK, 3)
    if t then draw_text_at(t, x + 22, y + 32, fade) end
    local label = T("CHARTS", 20, { 138, 147, 166 }, INK, 2)
    if label then draw_text_at(label, x + 380 - 16 - label.width, y + 36, fade) end
end

-- one 380x30 status chip: ink strip, coloured text
local function draw_chip(x, y, text2, colour, fade)
    if not text2 or text2 == "" then return end
    fill_rect(x, y, 380, 30, INK, 255 * fade)
    local t = T(text2, 18, colour, INK, 2)
    if t then draw_text_at(t, x + 12, y + 15, fade, 356) end
end

-- the left-column status chips — FLOW 1 (root) shows one combined strip,
-- FLOW 2/5 (inside a folder) stack ★ · courses / TIER / SORT
local function draw_chips(st, x, fade)
    local d = st.diff or {}
    local parts = {}
    local names = {}
    for i = 1, 5 do if d[i] then names[#names + 1] = COURSE_NAME[i] end end
    local diffs_on = #names
    if (st.star or 0) > 0 then parts[#parts + 1] = "★" .. st.star end
    if diffs_on < 5 then parts[#parts + 1] = table.concat(names, " + ") end

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
        draw_chip(x, 280, txt, GOLD, fade)
        return 1
    end

    local head
    if #parts == 0 then head = "ANY CHART" else head = table.concat(parts, " · ") end
    draw_chip(x, 280, head, GOLD, fade)
    draw_chip(x, 318, "TIER: " .. tier_disp, { 242, 244, 248 }, fade)
    draw_chip(x, 356, "SORT: " .. sort_disp, { 242, 244, 248 }, fade)
    return 3
end

-- chart details panel (380x336): ink shell -> band colour -> tan plate inset 11,
-- text at +26 — FLOW 2's chart details board
local function draw_details(ids, model, st, x, y, fade)
    local h = 336
    local row = model:row(0)
    local info = model:info(0)
    local is_chart = row and row.kind == "chart" and info ~= nil

    local band_frame
    if is_chart then band_frame = REF[info.genre_index] or 0
    elseif row and row.kind == "folder" then band_frame = REF[row.genre_index] or 0
    else band_frame = COLLECTION_FRAME[row and row.kind or "all"] or 0 end
    local band = BAND[band_frame] or BAND[0]

    fill_rect(x, y, 380, h, INK, 255 * fade)
    fill_rect(x + 3, y + 3, 374, h - 6, band, 255 * fade)
    fill_rect(x + 11, y + 11, 358, h - 22, TAN, 255 * fade)

    local tx = x + 26
    if is_chart then
        draw_text_at(T("CHART DETAILS", 16, { 138, 106, 74 }, INK, 2), tx, y + 38, fade)
        draw_text_at(T(info.title, 24, { 40, 30, 25 }, INK, 3), tx, y + 66, fade, 332)
        draw_text_at(T(info.subtitle, 16, { 122, 92, 66 }, INK, 2), tx, y + 94, fade, 332)

        -- course / star / level / branch / tier (@ +134)
        local ay = y + 134
        if info.course and ids.course then
            draw_centered(ids.course, tx + 22, ay + 22, 44, 44, { frame = info.course, fade = fade })
        end
        if info.branching and ids.branch then
            draw_centered(ids.branch, tx + 46, ay + 36, 20, 20, { fade = fade })
        end
        if ids.star then
            draw_centered(ids.star, tx + 90, ay + 22, 24, 24, { frame = info.course or 0, fade = fade })
        end
        local lv = math.max(1, math.min(11, info.level or 1))
        if ids.level_number then
            draw_centered(ids.level_number, tx + 131, ay + 21, 42, 42,
                          { frame = (info.course or 0) * 11 + (lv - 1), fade = fade })
        end
        if info.tier and info.tier ~= "" then
            local c = TIER_C[info.tier]
            if c and ids.pill then
                draw_centered(ids.pill, tx + 186, ay + 20, 52, 36, { color = rgba(c), fade = fade })
                draw_text_box(info.tier, 18, { 255, 255, 255 }, INK, 2, tx + 160, ay + 2, 52, 36, fade)
            end
        else
            placeholder(tx + 160, ay + 2, 52, 36, fade)
        end

        if info.played then
            draw_text_at(T("SCORE", 14, { 138, 106, 74 }, INK, 2), tx, y + 213, fade)
            draw_text_at(T(fmt_num(info.score), 30, { 40, 30, 25 }, INK, 3), tx, y + 237, fade, 230)
            draw_text_at(T("GOOD", 16, { 107, 74, 46 }, INK, 2), tx, y + 266, fade)
            draw_text_at(T(tostring(info.good or 0), 16, INK, TAN, 2), tx + 90, y + 266, fade)
            draw_text_at(T("OK", 16, { 107, 74, 46 }, INK, 2), tx, y + 286, fade)
            draw_text_at(T(tostring(info.ok or 0), 16, INK, TAN, 2), tx + 90, y + 286, fade)
            draw_text_at(T("MISS", 16, { 107, 74, 46 }, INK, 2), tx, y + 306, fade)
            draw_text_at(T(tostring(info.miss or 0), 16, INK, TAN, 2), tx + 90, y + 306, fade)
            if info.rank and info.rank > 0 then
                draw_centered(ids.rank, x + 320, y + 224, 40, 40, { frame = info.rank - 1, fade = fade })
            else
                placeholder(x + 300, y + 204, 40, 40, fade)
            end
            if info.crown and info.crown > 0 then
                draw_centered(ids.crown, x + 320, y + 272, 40, 40, { frame = info.crown, fade = fade })
            else
                placeholder(x + 300, y + 252, 40, 40, fade)
            end
        else
            draw_text_at(T("SCORE", 14, { 138, 106, 74 }, INK, 2), tx, y + 213, fade)
            placeholder(tx, y + 220, 230, 34, fade)
            draw_text_at(T("NO SCORE", 20, { 150, 130, 112 }, INK, 2), tx + 244, y + 237, fade)
        end
    else
        draw_text_at(T("FOLDER", 16, { 138, 106, 74 }, INK, 2), tx, y + 38, fade)
        draw_text_at(T(row and row.name or "", 26, { 40, 30, 25 }, INK, 3), tx, y + 66, fade, 332)
        local sub
        if row and row.kind == "folder" then
            sub = fmt_num(row.count or 0) .. " charts under this folder"
        elseif row and row.kind == "all" then
            sub = fmt_num(row.count or 0) .. " charts in the library"
        else
            sub = "a collection, not a folder"
        end
        draw_text_at(T(sub, 16, { 122, 92, 66 }, INK, 2), tx, y + 94, fade, 332)
    end
end

-- ---- the Filter/Sort dialog (FLOW 5) --------------------------------------
-- shell (360,190) 1200x700 -> tan plate inset 3 -> a 62px header band.
-- Rows: DIFFICULTY cy 318, STAR cy 418, TIER cy 508, SORT cy 618; footer cy 728.
-- kat moves the item cursor, don advances that item's value (SPEC 5 proposal).
local DLG_X, DLG_Y, DLG_W, DLG_H = 360, 190, 1200, 700

local function dlg_row_label(text2, cy, active, fade)
    if active then fill_rect(DLG_X + 34, cy - 22, 252, 44, GOLD, 255 * fade) end
    local t = T(text2, 26, INK, TAN, 3)
    if t then draw_text_at(t, DLG_X + 40, cy, fade) end
end

local function dlg_cell(x, y, w, h, label, fillc, text_fill, text_size, outline, fade)
    fill_rect(x, y, w, h, fillc or TAN_D, 255 * fade)
    draw_text_box(label, text_size or 24, text_fill or INK, outline or TAN,
                  (text_size or 24) >= 24 and 3 or 2, x, y, w, h, fade)
end

local function draw_filter_dialog(model, st, fade)
    local cur = st.filter_cursor or 0
    -- active row: 0-4 are the difficulty toggles, 5 star, 6 tier, 7 sort
    local row_active = (cur == 5) and 1 or (cur == 6) and 2 or (cur == 7) and 3 or 0

    fill_rect(0, 0, 1920, 1080, INK, 175 * fade)                       -- scrim
    fill_rect(DLG_X, DLG_Y, DLG_W, DLG_H, INK, 255 * fade)             -- shell
    fill_rect(DLG_X + 3, DLG_Y + 3, DLG_W - 6, DLG_H - 6, TAN, 255 * fade)
    fill_rect(DLG_X + 3, DLG_Y + 3, DLG_W - 6, 62, TAN_D, 255 * fade)  -- header
    draw_text_at(T("FILTER/SORT", 30, INK, TAN, 3), DLG_X + 36, DLG_Y + 34, fade)

    -- DIFFICULTY — on/off per course
    dlg_row_label("DIFFICULTY", DLG_Y + 128, row_active == 0, fade)
    local d = st.diff or {}
    for i = 1, 5 do
        local x = DLG_X + 290 + (i - 1) * 174
        local y = DLG_Y + 96
        local is_cursor = (cur == i - 1)
        fill_rect(x, y, 166, 64, is_cursor and GOLD or TAN_D, 255 * fade)
        fill_rect(x + 14, y + 15, 34, 34, TAN, 255 * fade)
        if d[i] then fill_rect(x + 19, y + 20, 24, 24, INK, 255 * fade) end
        draw_text_box(COURSE_NAME[i], 22, INK, TAN, 3, x + 52, y, 106, 64, fade)
    end

    -- STAR — ALL, then 1..10 (no 10+ bucket: this library tops out at ★10)
    dlg_row_label("STAR", DLG_Y + 228, row_active == 1, fade)
    local star = st.star or 0
    dlg_cell(DLG_X + 290, DLG_Y + 196, 72, 64, "ALL",
             (cur == 5 and star == 0) and GOLD or TAN_D, INK, nil, nil, fade)
    for n = 1, 10 do
        dlg_cell(DLG_X + 367 + (n - 1) * 77, DLG_Y + 196, 72, 64, tostring(n),
                 (cur == 5 and star == n) and GOLD or TAN_D, INK, nil, nil, fade)
    end

    -- TIER — that level's own set (never generated: the sets are not monotonic)
    dlg_row_label("TIER", DLG_Y + 318, row_active == 2, fade)
    local tier_cells = { "ALL" }
    if star > 0 then
        for _, tv in ipairs(model:tier_set(star)) do tier_cells[#tier_cells + 1] = tv end
    else
        for _, tv in ipairs(model:ladder()) do tier_cells[#tier_cells + 1] = tv end
    end
    tier_cells[#tier_cells + 1] = "None"
    local cx = DLG_X + 290                     -- flowing: ALL, the level's letters, None last
    for i, tv in ipairs(tier_cells) do
        local w = (tv == "ALL" or tv == "None") and 76 or 62
        local x = cx
        cx = cx + w + 4
        local shown = (tv == "None") and "NONE" or tv
        local is_cur = (st.tier == "" and tv == "ALL")
                    or (st.tier == "NONE" and tv == "None")
                    or (st.tier == tv)
        local fillc = TAN_D
        if tv ~= "ALL" and tv ~= "None" then fillc = TIER_C[tv] or TAN_D end
        dlg_cell(x, DLG_Y + 296, w, 44, shown, fillc,
                 (tv == "ALL" or tv == "None") and INK or { 255, 255, 255 },
                 tv == "None" and 18 or 22, tv == "None" and TAN or INK, fade)
        if is_cur then
            -- current value: a gold ring, so the tier's own colour still reads
            local a = 255 * fade
            tex.draw_rect(x, DLG_Y + 296, w, 3, GOLD[1], GOLD[2], GOLD[3], a)
            tex.draw_rect(x, DLG_Y + 337, w, 3, GOLD[1], GOLD[2], GOLD[3], a)
            tex.draw_rect(x, DLG_Y + 296, 3, 44, GOLD[1], GOLD[2], GOLD[3], a)
            tex.draw_rect(x + w - 3, DLG_Y + 296, 3, 44, GOLD[1], GOLD[2], GOLD[3], a)
        end
    end

    -- SORT
    dlg_row_label("SORT", DLG_Y + 432, row_active == 3, fade)
    local sort_disp = (st.sort == "A-Z") and "A–Z" or (st.sort or "A–Z")
    for i, sv in ipairs({ "SCORE", "CROWN", "A–Z", "TIER" }) do
        local x = DLG_X + 290 + (i - 1) * 216
        dlg_cell(x, DLG_Y + 396, 208, 64, sv, (sv == sort_disp) and GOLD or TAN_D,
                 INK, nil, nil, fade)
    end

    -- footer — the live match count (a short list reads as filtered, not broken)
    fill_rect(DLG_X + 40, DLG_Y + 500, DLG_W - 80, 76, TAN_D, 255 * fade)
    local match
    if st.context == "root" then
        match = "filter and sort apply inside a folder"
    elseif st.building then
        match = "counting…"
    else
        match = fmt_num(st.charts or 0) .. " charts match"
    end
    draw_text_at(T(match, 24, INK, TAN, 3), DLG_X + 64, DLG_Y + 538, fade, DLG_W - 128)
end

-- footer control hints (the FLOW 1 / FLOW 5 guides)
local function draw_footer(fade)
    fill_rect(LX, 1014, 210, 34, { 242, 244, 248 }, 235 * fade)
    draw_text_box("SELECT   ← →", 22, { 42, 49, 66 }, INK, 2, LX, 1014, 210, 34, fade)
    fill_rect(1143, 1014, 210, 34, { 242, 244, 248 }, 235 * fade)
    draw_text_box("DECIDE   DON", 22, { 42, 49, 66 }, INK, 2, 1143, 1014, 210, 34, fade)
end

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
        tex.draw_rect(0, 0, 1920, 1080, 40, 40, 44, 255)
    end

    -- header: art band, then the open context's own name
    fill_rect(56, 24, 352, 80, INK, 90)
    draw_text_at(T("Select a song", 40, { 232, 64, 49 }, { 255, 255, 255 }, 4), 60, 64, 1.0, 400)
    draw_text_at(T("SONG SELECT — " .. (st.label or "root"), 26, { 58, 42, 16 }, TAN, 4),
                 60, 131, 1.0, 900)

    if st.building and not st.filter_open then
        draw_text_at(T("BUILDING LIST…", 36, { 60, 40, 30 }, INK, 3), LX, 700, 1.0)
        return true
    end

    -- left column
    local focused = model:row(0)
    local pill_n = st.charts or 0
    if st.context == "root" and focused then pill_n = focused.count or 0 end
    draw_count_pill(ids, pill_n, LX, 196, 1.0)
    local nchips = draw_chips(st, LX, 1.0)
    draw_details(ids, model, st, LX, (nchips >= 3) and 394 or 366, 1.0)

    -- the list: cursor pinned to the centre row
    for off = -6, 6 do
        local row = model:row(off)
        if row then
            local x = CARD_X
            local y = CENTER_TOP + off * CARD_H
            local is_cursor = (off == 0)
            if row.kind == "chart" then
                if is_cursor and st.selected and st.picker_open then
                    draw_picker(ids, model, st, x, y, 1.0)
                elseif is_cursor and st.selected then
                    draw_controls(ids, st, x, y, 1.0)
                else
                    local info = model:info(off)
                    if info then draw_chart_card(ids, info, x, y, 1.0) end
                end
            elseif row.kind == "folder" or row.kind == "search" or row.kind == "recent"
                or row.kind == "favorite" or row.kind == "all" then
                draw_folder_card(ids, row, x, y, 1.0)
            else
                draw_pair_card(ids, row.kind, x, y, 1.0)
            end
            if is_cursor then draw_cursor_rim(x, y, 1.0) end
        end
    end

    draw_footer(1.0)
    if st.filter_open then draw_filter_dialog(model, st, 1.0) end
    return true
end

return M
