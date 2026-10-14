-- YataiDON-HSS song select: reference cabinet layout at native 1080p.
-- Started from YataiDON-HSS/Scripts/song_select/song_select.lua (the horizontal_song_select
-- path) and re-laid-out against the arcade lumen placements (see song_select/MAPPING.md).
--
-- COORDINATE NOTE: every child texture is loaded at scale 1.0 (1080p pixels) and every
-- child skin_config entry is read unscaled, so this script works in 1080p pixels and never
-- multiplies its own constants by tex.get_screen_scale(). Box positions (box:box_x/box_y)
-- and engine animations (open_anim = 150*screen_scale px) are already in screen pixels.
--
-- ROUND 2 (arcade chrome, from song_select_song_disp_data.lua / song_select_genre_disp_data.lua /
-- song_select_ui.lua / song_select_course_*.lua — see MAPPING.md "Round 2"):
--   * NEW badge (instance_icon @ icon_new) only on CLOSED boards (board label 'on' hides it),
--   * genre boards: event/feature boards (beige 'event1' + pattern strip) and sort_select (red),
--     title rises to -94 when open, pattern strip follows, intro/count text at the centre,
--   * song counter plate (number_of_songs) under the timer at (1754,270),
--   * diff select: title/subtitle inside the backboard header, course names on the cards,
--     drum guide (control_guide_s 's_menfuchi_kr') at (960,978).
-- ROUND 4 (user capture comparison, see MAPPING.md "ROUND 4"):
--   * no 「曲をえらぶ」/「むずかしさをえらぶ」 header (the arcade shows only the control-guide panel),
--   * no floor band (draw_footer empty): the genre backdrop runs to the bottom, フリープレイ on it,
--   * song_kanban_info chips: course name (text_course_* font 18) under the icon inside each plate,
--     chips centred on the board (kanban_info 'default' = 3 chips at -182/0/182, 'oni' = 4 at ±91/±273),
--   * diff select: main_diff_mark (translucent course mark, 680x680 @ (200,408) / (1720,408)) behind
--     the buttons; option wrench kept (the engine cursor can reach MODIFIER/NEIRO: song_select/player.cpp
--     move_left/right walk NEIRO -> MODIFIER -> BACK) but drawn small, no "NEIRO" label.

local STATE_BROWSING      = 0
local STATE_SONG_SELECTED = 1
local STATE_DIFF_SORTING  = 2
local STATE_SEARCHING     = 3
local STATE_DAN_SELECTED  = 4

local DIFF_BACK, DIFF_MODIFIER, DIFF_NEIRO = -3, -2, -1
local DIFF_EASY, DIFF_NORMAL, DIFF_HARD, DIFF_ONI, DIFF_URA = 0, 1, 2, 3, 4
local CROWN_NONE, CROWN_CLEAR, CROWN_FC, CROWN_DFC = 0, 1, 2, 3

-- Arcade board geometry (common_song_select_main_song_board / *_board_top/center/bottom)
local BOARD_W      = 960
local CAP_H        = 56     -- top / bottom cap rows of the 960x352 board texture
local CEN_SRC_H    = 240    -- centre slice rows (56..296) in the texture
local CEN_CLOSED_H = 52     -- arcade closed board: centre scaled 0.22 (240*0.22)
local CEN_OPEN_H   = 240
local GLOW_CAP_H   = 80     -- bar_genre_overlay (cursor glow) 1024x400: 80 / 240 / 80
local GLOW_W       = 1024

-- Feature ("event") folders get the arcade event board (folder_graphic frame 11 + pattern strip) and
-- the search / difficulty-sort folders the red sort_select board (frame 12). The engine does not
-- expose the folder's collection/genre_index to Lua (genre_frame() folds DEFAULT/RECOMMENDED/
-- FAVORITE/DAN/DIFFICULTY into frame 0), so they are matched by display name (box.text_name).
local EVENT_FOLDER_NAMES = { ["ドドンと！新曲祭り！"] = true, ["New Songs"] = true, ["New"] = true,
                             ["おすすめ曲"] = true, ["Recommended"] = true, ["推薦歌曲"] = true }
local SORT_FOLDER_NAMES  = { ["曲検索"] = true, ["Search Song"] = true, ["搜尋歌曲"] = true,
                             ["むずかしさで探す"] = true, ["Difficulty Sort"] = true, ["難易度排序"] = true,
                             -- ROUND 6: the names this skin's own Songs/16 Difficulty Sort/box.def ships
                             -- (measured in game: without these the board fell through to genre frame 0)
                             ["むずかしさからえらぶ"] = true, ["Diff Sort"] = true }
local FOLDER_FRAME_EVENT, FOLDER_FRAME_SORT, FOLDER_FRAME_DANI = 11, 12, 10
-- global/indicator/background is the arcade control_guide 'all' loop baked every GUIDE_STEP arcade
-- frames (round 4: step 2 = 30 fps, 325 cells); cells 0..209 = arcade frames 0..419 = rim hit + arrow
-- (right stick 0..209, left stick 210..419), 7.0 s. Keep in sync with global/indicator.lua FRAME_STEP.
local GUIDE_STEP = 2
local GUIDE_RIM_FRAMES = math.floor(420 / GUIDE_STEP)

SongSelect = {}
SongSelect.__index = SongSelect

-- tex.get_texture tries "<name>" then "<name>_<language>"; for languages without an art
-- variant (e.g. zh) fall back to the _en one so we never hand draw_texture a nil id.
local function get_any(subset, name)
    return tex.get_texture("song_select/" .. subset .. "/" .. name)
        or tex.get_texture("song_select/" .. subset .. "/" .. name .. "_en")
        or tex.get_texture("song_select/" .. subset .. "/" .. name .. "_ja")
end

-- draw a w x h texture centred on (cx, cy)
local function draw_centered(id, cx, cy, w, h, extra)
    extra = extra or {}
    extra.x = cx - w / 2
    extra.y = cy - h / 2
    tex.draw_texture(id, extra)
end

-- ROUND 6 (2P): a few textures now carry a non-zero texture.json x/y because the ENGINE draws
-- them in 2P (SongSelectPlayer::draw_selector); the engine ADDS that base to params.x/y, so when
-- this script draws the same texture it has to subtract the base again.
local function draw_centered_b(id, base, cx, cy, w, h, extra)
    extra = extra or {}
    extra.x = cx - w / 2 - base[1]
    extra.y = cy - h / 2 - base[2]
    tex.draw_texture(id, extra)
end
local SEL_BASE_BALLOON = { 683, 308 }   -- 1p/2p_balloon(_half)      = cursor bubble (card 1 @ 745,370)

-- Title centred on (bx, cy). max_w (optional): arcade EditText boxes squeeze long text HORIZONTALLY down to
-- the box width (song board title/sub box ~860 px); never below min_scale (default 0.75).
-- ROUND 11 (text sharpness): the destination x/y are SNAPPED TO WHOLE PIXELS. An
-- OutlinedText is an already-rasterised texture drawn 1:1 with TEXTURE_FILTER_BILINEAR;
-- centring an odd-width label on an integer centre lands it on a .5 and the GPU blends
-- every texel 50/50 with its neighbour. Measured cost of that half-pixel alone:
-- +34 % edge width on the current (resampled) text, +63..69 % on sharp text
-- (scratchpad/r11ts/compare.py). `w` is rounded too so `x2` keeps the quad integral.
local function draw_title(text, bx, cy, fade, max_w, min_scale)
    if not text then return end
    local w = text.width
    if max_w and w > max_w then
        w = math.floor(math.max(max_w, w * (min_scale or 0.75)) + 0.5)
    end
    text:draw({ x = math.floor(bx - w / 2 + 0.5), y = math.floor(cy - text.height / 2 + 0.5),
                x2 = w - text.width, fade = fade })
end
-- ROUND 6: control-guide pill labels (`common/control_guide_s` DefineEditText id 5).
-- Box centres measured from the lumen (see draw_overlays); GUIDE_LABEL_SPACING is the `spacing`
-- passed to text.create_text in guide_labels(), which MeasureTextEx also adds after the last glyph.
local GUIDE_SELECT_CX, GUIDE_DECIDE_CX, GUIDE_LABEL_CY = 904, 1077, 980
local GUIDE_LABEL_SPACING = 2.0
local function draw_guide_label(t, cx, cy, fade)
    if not t then return end
    t:draw({ x = math.floor(cx - (t.width - GUIDE_LABEL_SPACING) / 2 + 0.5),   -- ROUND 11: whole px
             y = math.floor(cy - t.height / 2 + 0.5), fade = fade })
end
local SONG_TEXT_BOX_W = 860   -- main_song_board text_title / text_sub EditText box width
local DS_TEXT_BOX_W   = 1000  -- main_diff text_title_song / text_song_title_sub EditText box width

-- ROUND 6 (item 15): the wheel's song-board title is drawn by this script from `box.text_name`
-- instead of the engine's `box:name()`.  Measured from the lumen `CS/main_song_board`
-- DefineEditText id 82 (`text_song_title`): ＭＳ 明朝 **42**, `color="#ffffff"`, outline field
-- **5.0** — i.e. exactly the same white-fill / black-edge / 5 px shape as id 79 (もどる, 42/5.0)
-- and id 85 (the subtitle, 24/3.5) which round 5 already ported.  The engine builds `name()` as
-- `OutlinedText(text_name, song_box_name.font_size, WHITE, fore_color, …)` and subtracts
-- 10·screen_scale = 15 px from the font for titles of >= 30 characters (`BaseBox::horizontal_name`,
-- box_base.h) — the arcade never changes the font size, it squeezes the EditText box horizontally,
-- which `draw_title(…, SONG_TEXT_BOX_W)` already does.  So this drops BOTH the genre-coloured
-- outline and the >= 30-char shrink.  The squeeze is passed `min_scale = 0` (squeeze all the way
-- down to the 860 px box) instead of the 0.75 floor round 5 used for the subtitle: with the floor,
-- a 45-character title (`My Muscle Heart ～…～`) still came out 1095 px wide and hung ~120 px off
-- BOTH ends of the 960 px board.  The diff-select header gets the same treatment against its own
-- 1000 px box (`main_diff` `text_title_song` / `text_song_title_sub`).
-- ROUND 16 (r16-outline) -- OUTLINE THICKNESS IS NOT IN STAGE PIXELS.
-- `OutlinedText`'s ctor multiplies whatever it is given by `screen_scale`
-- (src/libs/text.cpp:200), which is 1.5 in this skin, and then draws a 16-point
-- circle of that radius -- the same construction as the cabinet's own
-- `RenderBorderText` (research/lumen_text.md S4).  A Lumen DefineEditText
-- `border` is ALREADY in 1080p stage px: measured on the cabinet's own
-- renderer, `main_song_board` field 82 (border 5.0) pads its glyph mask by
-- exactly 5 px on all four sides (`scratchpad/r16ol/arc_sb_g.png`).  So the
-- value to hand `create_*text` is `border / screen_scale`, and every constant
-- below is written as the record's own border run through `ol()`.  Passing the
-- border straight through drew the ring 50 % too thick.
local function ol(border) return border / tex.get_screen_scale() end
local SONG_TITLE_OUTLINE = 5.0                    -- lumen EditText id 82 outline field 5.0 (stage px)
local SONG_TITLE_SIZE = nil                       -- resolved once from skin_config song_box_name
local SONG_TITLE_CACHE_MAX = 128                  -- bound the per-title texture cache

-- ROUND 6 (board text colours).  The claim above that "the outline colour is renderer-fixed and is
-- BLACK" is WRONG for the boards, and this table replaces it.  Established from the data, not by eye:
--
--   * `CSS/song_select_song_disp_data.lua` `SetSongText`/`SetSongColor` and
--     `CSS/song_select_genre_disp_data.lua` `SetFolderText` issue **zero** SetTextColor /
--     SetTextBorderColor calls.  What they do is `GotoAndStop("main/board/text_kanban_title_song",
--     <genreIdStr>)` — the text field is wrapped in a clip whose frames are LABELLED PER GENRE.
--   * Each of those frames re-places the inner text clip with a different PlaceObject **colour-ADD**
--     transform (`main_song_board` sprites 89 `text_kanban_title_song` / 92 `text_song_title_sub` /
--     95 `text_song_title_ruby`; `main_genre_board` sprites 70 `text_kanban_title_genre` /
--     30 `text_kanban_intro_genre`).  All five carry the SAME add table; colour-MUL is identity.
--   * The EditText fill is `#ffffff` (style-table entry 74/67 = 255,255,255,255).  White + add clamps
--     to white, so the add lands only on the outline.
--   * The outline's own colour was the last unknown (`AppLumenRenderer+17632`, listed "not extracted"
--     in the reference research notes research/lumen_text.md §5.2).  It is now extracted:
--     `App::AppLumenRenderer::set_font` @0x14037C180 does
--         `this[1102] = (fill.rgb != 0) ? xmmword_140CE64A0 : xmmword_140CE64B0`
--     and reading those two constants out of the reference build gives (0,0,0,1) = BLACK and (1,1,1,1) = WHITE.
--     So the baked outline is BLACK whenever the fill is not black (and WHITE when it is — which is
--     why the arcade's black `text_course_*` labels have a white edge).
--   * BLACK outline + the per-genre colour add ⇒ **the board title / subtitle / intro outline is the
--     genre colour**, exactly the values below.  Fill stays white.
--
-- Indexed by the skin's own genre frame (`box:genre_frame()` / `folder_frame`, 0..12), which is the
-- same numbering `box/bar_genre` and `box/folder_graphic` already use.
local BOARD_OUTLINE = {
    [0]  = {  18,  72,  76, 255 },   -- default / tutorial / howto / favorite (arcade 'howto' = pops)
    [1]  = {  18,  72,  76, 255 },   -- pops
    [2]  = { 155,  24,  99, 255 },   -- anime
    [3]  = {  78,  29, 118, 255 },   -- game
    [4]  = { 150,  31,   0, 255 },   -- namco
    [5]  = { 101,  67,  42, 255 },   -- classic
    [6]  = {  20,  79,  20, 255 },   -- variety
    [7]  = { 165,  50,   0, 255 },   -- kids
    [8]  = {  38,  52,  73, 255 },   -- vocaloid
    [9]  = {  18,  72,  76, 255 },   -- recent (arcade 'recent' shares the pops entry)
    [10] = {   0,  25,  95, 255 },   -- dani
    [11] = { 100,   6,  22, 255 },   -- event
    [12] = { 119,  26,  45, 255 },   -- sort
}
local BOARD_OUTLINE_BACK = { 76, 50, 20, 255 }   -- main_song_board 'back' label add (もどる board)

local function board_outline(frame)
    return BOARD_OUTLINE[frame or 0] or BOARD_OUTLINE[0]
end

function SongSelect.new()
    local self = setmetatable({}, SongSelect)
    -- Animations already loaded by Screen via load_screen_textures (not a Lua binding).
    tex.load_folder("song_select", "global")
    tex.load_folder("song_select", "box")
    tex.load_folder("song_select", "yellow_box")
    tex.load_folder("song_select", "diff_select")

    self.text_fade_in = tex.get_animation(4)
    self.text_fade_in:start()

    -- global
    self._id_footer            = tex.get_texture("song_select/global/footer")            or 0
    self._id_song_select       = get_any("global", "song_select")       or 0   -- 352x80 header at (30,30)
    self._id_difficulty_select = get_any("global", "difficulty_select") or 0
    -- Arcade control_guide (white drum panel, common/control_guide 'main' @ 'default': panel at
    -- (0,34)-(296,290)) is drawn HERE, under the header, from the global/indicator bake (see
    -- Scripts/global/indicator.lua: 130 frames, every 5th arcade frame, frames 0..86 = rim-hit loop).
    -- The engine's own Indicator is parked off-screen by skin_config song_select_indicator.x = -2000
    -- because it is drawn after draw_overlays and would cover the header; the arcade hides the
    -- guide during course select (song_select_all.lua: SetVisibleControlGuide(false) on
    -- change_courseselect), so it is only drawn while browsing.
    self._id_guide_panel = tex.get_texture("global/indicator/background")
    self._guide_t0 = tex.get_current_ms()

    -- box
    self._id_bar_genre      = tex.get_texture("song_select/box/bar_genre")          -- song boards, 960x352, frame = genre
    self._id_bar_folder     = tex.get_texture("song_select/box/folder_graphic")     -- genre (folder) boards, same layout
    self._id_bar_back       = tex.get_texture("song_select/box/bar_genre_back")     -- "back" board (brown)
    self._id_glow           = tex.get_texture("song_select/box/bar_genre_overlay")  -- yellow cursor glow 1024x400
    self._id_background     = tex.get_texture("song_select/box/background")         -- 2880x1080 scrolling bg, frame = genre
    self._id_box_chara      = tex.get_texture("song_select/box/box_chara")          -- 1920x480 genre characters (L at 510, R at 1410)
    self._id_pattern        = tex.get_texture("song_select/box/genre_overlay_large") -- 960x120 event pattern strip (name reused; new texture names need an engine rebuild)
    self._id_song_counter   = tex.get_texture("song_select/yellow_box/song_count_back") -- 280x72 number_of_songs plate (name reused)

    -- yellow_box
    self._id_crown_dfc    = tex.get_texture("song_select/yellow_box/crown_dfc")     -- 72x72, frame = diff
    self._id_crown_fc     = tex.get_texture("song_select/yellow_box/crown_fc")
    self._id_crown_clear  = tex.get_texture("song_select/yellow_box/crown_clear")
    self._id_score_rank   = tex.get_texture("song_select/yellow_box/score_rank")    -- 72x72, frame = (rank-1)*5 + diff
    self._id_plate        = tex.get_texture("song_select/yellow_box/frame_score")   -- 184x96 diff plate, frame = diff
    self._id_level_number = tex.get_texture("song_select/yellow_box/level_number")  -- 48x48, frame = diff*11 + (level-1), 11 = "10+"
    self._id_star         = tex.get_texture("song_select/yellow_box/star")          -- 40x40 tinted star, frame = diff
    self._id_branch       = tex.get_texture("song_select/yellow_box/branch_indicator")       -- 40x40
    self._id_branch_diff  = tex.get_texture("song_select/yellow_box/branch_indicator_diff")  -- 40x40 (diff select)
    self._id_new_icon     = get_any("yellow_box", "ex_data_new_song")       -- 120x96 NEW icon

    -- diff_select
    self._id_backboard   = tex.get_texture("song_select/diff_select/difficulty_back")        -- 1272x784, frame = genre
    self._id_diff_board  = tex.get_texture("song_select/diff_select/difficulty_bar")         -- 200x360, frame = diff (4 = ura)
    self._id_course_frame  = tex.get_texture("song_select/diff_select/difficulty_select_bar")  -- 248x408 yellow glow frame
    self._id_course_frame2 = tex.get_texture("song_select/diff_select/difficulty_select_bar2") -- 248x408 outline variant
    self._id_bubble_1p   = tex.get_texture("song_select/diff_select/1p_balloon")             -- 124x124 cursor bubble + 1P
    self._id_bubble_2p   = tex.get_texture("song_select/diff_select/2p_balloon")
    self._id_btn_back    = get_any("diff_select", "back")                   -- 168x168 (localised lookup)
    self._id_btn_option  = get_any("diff_select", "option")
    -- ROUND 4c: with the engine's opt-in "option_neiro_row" the 音色 row lives inside
    -- the 演奏オプション panel, so the NEIRO slot is unreachable and is not drawn.
    local neiro_row = tex.get_skin_config("option_neiro_row")
    self._neiro_in_options = (neiro_row ~= nil) and (neiro_row.x >= 1)
    self._id_btn_glow    = tex.get_texture("song_select/yellow_box/s_crown_clear")            -- 168x168 button cursor plate (moved off diff_select: every 1p/2p_* name there is drawn by the engine in 2P)
    self._id_small_crown = tex.get_texture("song_select/diff_select/difficulty_crown")       -- 40x40, frame = crown (0 none)
    self._id_small_rank  = tex.get_texture("song_select/yellow_box/s_crown_outline")          -- 40x40, frame = rank-1 (moved off 1p_outline_back)
    self._id_small_star  = tex.get_texture("song_select/diff_select/difficulty_star")        -- 40x40
    self._id_dot_bar     = tex.get_texture("song_select/diff_select/ura_oni_plate")          -- 172x24 dotted level bar
    self._id_dot_star    = tex.get_texture("song_select/diff_select/disable")                -- 24x24 star for the dots
    self._id_guide_s     = tex.get_texture("song_select/diff_select/diff_tower")             -- 372x96 control_guide_s, 210-cell loop sheet (name reused)
    self._id_diff_mark   = tex.get_texture("song_select/diff_select/diff_tower_shadow")      -- 680x680 main_diff_mark 'wait' (alpha 154/256), frame = course (name reused)
    -- main_diff_mark 'course_decide_start': once the course is locked in the mark pops to its
    -- opaque look (mark_course over a white backing, a white copy flashing on top).  Opaque
    -- 680x680 shapes of mark_course (course_1..5) and mark_course_white, origin = centre.
    tex.load_folder("song_select", "diff_mark_anim")
    self._id_mark, self._id_mark_w = {}, {}
    for i = DIFF_EASY, DIFF_URA do
        self._id_mark[i]   = tex.get_texture("song_select/diff_mark_anim/mark_" .. i)
        self._id_mark_w[i] = tex.get_texture("song_select/diff_mark_anim/white_" .. i)
    end
    -- ROUND 4b: oni<->ura switch = CS/main_diff_board oni_main 'change_ura' / 'change_oni' (90 arcade frames,
    -- 1.5 s) baked every 2nd frame into 45 cells of 340x400 (origin 170,210 = card centre). Names reused
    -- (1p/2p_outline_half are vertical-path-only). The engine's own ura_switch is a transparent texture.
    self._id_ura_to_ura  = tex.get_texture("song_select/yellow_box/s_crown_dfc")
    self._id_ura_to_oni  = tex.get_texture("song_select/yellow_box/s_crown_fc")

    self._neiro_label = nil
    self._back_label  = nil
    self._count_cache = {}
    self._folder_titles = {}
    self._song_titles   = {}   -- ROUND 6: text_name -> OutlinedText (arcade song-board title)
    self._song_titles_n = 0
    self._tier_badges   = {}   -- tier letter -> OutlinedText (ESE community clear-tier)
    self._tier_badges_n = 0
    self._course_labels = {}
    self._chip_labels   = {}
    self._mark_diff     = {}   -- per player: last cursor column for main_diff_mark (set by draw_selector)
    self._mark_decided  = {}   -- per player: { t0 = ms, course } once player:difficulty_decided()
    self._ura_prev      = nil  -- box.is_ura seen last frame (toggle detection; C++ flips it instantly)
    self._ura_anim      = nil  -- { t0 = ms, to_ura = bool } while the arcade change animation plays
    self._guide_labels  = nil
    -- ROUND 5: 演奏オプション header title drawn from Lua (modifier/top.png is baked without text).
    -- Lua only sees player:modifier_active() (panel exists), so the slide is re-timed here from the
    -- frame it turns true (animation.json id 28: MOD_SLIDE px, MOD_SLIDE_MS, ease-out quadratic).
    self._mod_title     = nil
    self._mod_seen      = {}   -- per player: modifier_active() last frame (set by draw_selector)
    self._mod_t0        = {}   -- per player: ms the panel appeared (fallback timing only)
    self._mod_off       = {}   -- per player: panel y offset this frame (nil = no panel), set by draw_selector
    return self
end

function SongSelect:update(current_ms)
    self.text_fade_in:update(current_ms)
end

function SongSelect:restart_text_fade()
    self.text_fade_in:start()
end

function SongSelect:draw_footer()
    -- ROUND 4: the arcade song select has no floor band; the genre backdrop runs to the bottom.
end

-- ROUND 9 - 「M曲」, the blue right half of the number_of_songs plate
-- (LUA_CAPABILITIES item 17). Unblocked by the new `tex.song_limit()` binding:
-- until now general.song_limit had no binding and `io` is not opened, so the
-- right half of the plate had to stay empty. Arcade: plate 280x72 centred
-- (1754,270) -> white half 1614..1754, blue half 1754..1894, centre 1824.
-- Nothing is drawn when song_limit <= 0 (no limit configured = the arcade's
-- free-play cabinet, which prints no total either).
local SONG_MAX_CX = 1824

function SongSelect:draw_song_max()
    if not tex.song_limit then return end
    local limit = tex.song_limit()
    if not limit or limit <= 0 then return end
    if self._song_max_n ~= limit then
        local sc = tex.get_skin_config("song_num") or {}
        self._song_max_y = sc.y or 252
        self._song_max   = text.create_raw_text((tex.skin_text("song_num_max"):gsub("{0}", tostring(limit))),
            -- `common_song_select_number_of_songs` id 3 `text_total_songs`: size 24, border 3.5
            sc.font_size or 24, { 255, 255, 255, 255 }, { 0, 0, 0, 255 }, false, ol(3.5), 2.0)
        self._song_max_n = limit
    end
    local t = self._song_max
    if not t then return end
    -- ink centre = (width - spacing)/2, the same correction every label in this
    -- file uses (raylib adds the spacing after the last glyph too).
    t:draw({ x = math.floor(SONG_MAX_CX - (t.width - 2.0) / 2 + 0.5),          -- ROUND 11: whole px
             y = math.floor(self._song_max_y + 0.5) })
end

-- 段位道場 shutter doors. draw_overlays is the only place the skin can see
-- SongSelectState, and DAN_SELECTED (4) is set on the same frame song_select.cpp does
-- `dan_transition.emplace(); dan_transition->start()`; ROUND 11 latched a private clock
-- off that because DanTransition's own animation is invisible from Lua (C-dan-2).
-- ROUND 14 keeps the arm() call only as the fallback for an engine without draw_top —
-- when draw_top runs it supplies DanTransition::progress() and that wins.
local _dd_ok, dan_doors = pcall(require, "song_select/dan_doors")

-- ROUND 14: the top-most skin slot on SONG_SELECT, above the song counter, the select
-- timer, the allnet chip, the coin overlay and the control-guide indicator, and the last
-- draw before the engine's own (deliberately zero-height) DAN_TRANSITION quad.
-- `dan_progress` is DanTransition::progress() in 0..1, or -1 when none is running.
function SongSelect:draw_top(dan_progress)
    if not _dd_ok then return end
    dan_doors.set_progress(dan_progress)
    dan_doors.draw()
end

function SongSelect:draw_overlays(state)
    self:draw_decided_bridge()   -- decided genre board while its songs load (defined below)
    if _dd_ok then dan_doors.arm(state == STATE_DAN_SELECTED) end
    -- number_of_songs plate: arcade song_of_number_instance at stage (1754,270), 280x72 centred.
    -- The engine draws its own song_num_bg (made transparent in this skin) and the "N曲目" text
    -- (skin_config song_num, font 32, right edge 1722 / top 248) on top of this.
    if self._id_song_counter then
        tex.draw_texture(self._id_song_counter, { x = 1754 - 140, y = 270 - 36 })
    end
    self:draw_song_max()
    local label_fade = self.text_fade_in.attribute
    -- control_guide panel (browse only; fades with the header swap)
    if self._id_guide_panel then
        local panel_fade = (state == STATE_SONG_SELECTED) and (1 - label_fade) or 1.0
        if panel_fade > 0 then
            local elapsed = tex.get_current_ms() - self._guide_t0
            local frame = math.floor(elapsed / 1000 * 60 / GUIDE_STEP) % GUIDE_RIM_FRAMES
            tex.draw_texture(self._id_guide_panel, { frame = frame, x = 0, y = 0, fade = panel_fade })
        end
    end
    -- ROUND 4: no 「曲をえらぶ」/「むずかしさをえらぶ」 header art (arcade: control-guide panel only).
    if state == STATE_SONG_SELECTED then
        -- course-select drum guide (main_diff instance_guide = common/control_guide_s 's_menfuchi_kr')
        -- at stage (960,978), on top of the footer band (drawn here because draw_box runs before the footer)
        if self._id_guide_s then
            -- ROUND 4d: 210 cells = every 2nd arcade frame of the 420-frame select loop (decide loop 210)
            -- -> 30 cells/s; stage (960,978) = root(960,540) + board(0,-132) + instance_guide(0,570).
            -- ROUND 6 (text alignment): the label position is the EditText BOX, not the "free plate area".
            -- `common/control_guide_s` has exactly one DefineEditText (id 5; the rect index is record word[5]):
            -- box = bounds[2] = (-2,-2,82,46) -> 84x48, font 24, outline 0, align=center (word[8] low byte 2),
            -- text #000000.  It is placed inside `text_guide_instance` (MC 6) which sits at +58 in each
            -- instance, so the box lands at instance + (+35..+119, -22..+26): pill-centre + (-98..-14)
            -- for select_instance (-133) and + (+75..+159) for decide_instance (+40)  ->  box centres
            --   select  = 960 - 133 + 77 = 904,  decide = 960 + 40 + 77 = 1077,  y = 978 + 2 = 980.
            -- Lumen dynamic text is ALWAYS centred in its rect on BOTH axes
            -- (FontImpl::GetAlignOffsetY = (boxH - textH)/2 for every align value the renderer emits;
            --  research/lumen_text.md §3a), so the label texture is centred on the box centre.
            -- raylib MeasureTextEx adds the glyph spacing after the LAST glyph too, so the ink centre of an
            -- OutlinedText is (width - spacing)/2, not width/2 (Graphics/entry/MAPPING.md ROUND 4).
            local frame = math.floor(tex.get_current_ms() / 1000 * 30) % 210
            local g = self:guide_labels()
            -- The pill must sit ABOVE the フリープレイ credit text, which the engine draws later
            -- (coin_overlay.draw after script draw_overlays): hand the draw to coin_overlay.lua via a
            -- shared-state global (one sol::state), consumed + cleared there every frame.
            local id, fade = self._id_guide_s, label_fade
            YD_DRAW_ABOVE_CREDIT = function()
                draw_centered(id, 960, 978, 372, 96, { frame = frame, fade = fade })
                draw_guide_label(g.select, GUIDE_SELECT_CX, GUIDE_LABEL_CY, fade)
                draw_guide_label(g.decide, GUIDE_DECIDE_CX, GUIDE_LABEL_CY, fade)
            end
        end
        self:draw_modifier_title()
    else
        self._mod_seen = {}; self._mod_off = {}
    end
end

-- 演奏オプション header of the C++ option panel (modifier/top.png 420x538 at json (5,1080), open top 532):
-- arcade text_option_title = EditText font 32 centred on the board header; here centred on the panel
-- (x 215, header centre = json y 1080 + 49 + panel offset), 2P at +option_p2.x. The offset comes from
-- player:modifier_offset() (see draw_selector), so the title rides the panel in, while open and out
-- (engine: in = 548 px / 333 ms ease-out quadratic from json y; out = restart + song_select_offset.x).
local MOD_SLIDE, MOD_SLIDE_MS, MOD_JSON_Y, MOD_HEADER_CY, MOD_CX = 548, 333.33, 1080, 49, 215
function SongSelect:draw_modifier_title()
    if self._mod_title == nil then
        self._mod_title = tex.get_skin_config("modifier_title")
            -- `song_select_main_diff_option` id 55 `text_option_title`: size 32, border 4.0
            and text.create_text("modifier_title", { 255, 255, 255, 255 }, { 0, 0, 0, 255 }, false, ol(4.0), 2.0) or false
    end
    if not self._mod_title then return end
    local p2x = tex.get_skin_config("option_p2"); p2x = p2x and p2x.x or 1490
    for p = 1, 2 do
        local off = self._mod_off[p]
        if off then
            draw_title(self._mod_title, MOD_CX + (p - 1) * p2x, MOD_JSON_Y + MOD_HEADER_CY + off, 1.0)
        end
    end
end


--------------------------------------------------------------------------------
-- 演奏オプション / 音色 board — LUA_CAPABILITIES items 19, 20, 21.
-- Measurements + arcade source: Graphics/song_select/MAPPING_option.md.
--
-- The engine calls this INSTEAD of ModifierSelector::draw() / NeiroSelector::draw()
-- (SongSelectScript::draw_option_panel, added round 14): defining the method turns
-- the C++ board off, the same contract draw_background / draw_box use. It has to be
-- an all-or-nothing takeover — the arcade paints the changed value box, the greyed
-- row and the row cursor UNDER the row texts, and `draw_overlays` runs above them.
--
-- Everything is drawn through the same texture.json bases and skin_config keys the
-- C++ board used (modifier/* at (5,1080) etc., modifier_offset.y = 61 row pitch,
-- modifier_text (300,1178), modifier_offset_2 (44,140), option_p2.x = 1490), so the
-- geometry is exactly what round 4/5 measured; what is new is the STATE:
--   * value box turns yellow when the row is not on its default (arcade
--     option_select_change_mc label "change": cmul (0,0,0,256) + cadd (255,255,0,0)),
--   * the row cursor pulses (arcade MC5 `cursor`: yellow SH3, alpha 256 -> 51 over
--     29 frames and back over 30, a 60-frame = 1000 ms triangle),
--   * a row the engine will ignore is greyed (arcade MC17 frame "grayout": row bg
--     forced to flat RGB(166,168,171) plus a 50 % black scrim over the whole row —
--     today only 演奏スキップ with both drums in the enso),
--   * the arrow that was pressed nudges 5 px outward instead of bouncing forever.
local OPT_CURSOR_MS     = 1000                  -- arcade cursor MC5 loop = 60 frames @ 60 fps
local OPT_CURSOR_MIN    = 51 / 256              -- its alpha floor (frame 29)
local OPT_GREY          = { 166, 168, 171, 255 }
local OPT_CHANGED       = { 255, 255, 0, 255 }
local OPT_SCRIM         = { 0, 0, 0, 128 }
local OPT_ARROW_NUDGE   = 5                     -- arcade arrow label "select": 0 -> -5 -> 0 over 10 frames

function SongSelect:_opt_ids()
    if self._opt_ids_t then return self._opt_ids_t end
    -- MUST load the subsets here. `script_manager.tex` (script.h) is its OWN
    -- TextureWrapper, separate from the global `tex` the C++ screens use, so a
    -- subset the engine loaded in load_screen_textures is NOT in the Lua one:
    -- measured on this screen, `box` was resident (the wheel script loads it)
    -- while `modifier`, `neiro` and `diff_select` were not, and every
    -- draw_texture against them was a silent no-op (draw_texture returns early
    -- when the id is missing). tex.get_id only reads the generated enum, so it
    -- can never be used as a "is it loaded" test - get_texture_info can.
    tex.load_folder("song_select", "modifier")
    tex.load_folder("song_select", "neiro")
    local t = {}
    t.top    = get_any("modifier", "top")
    t.bottom = get_any("modifier", "bottom")
    t.bg     = get_any("modifier", "background")
    t.row    = get_any("modifier", "mod_bg")
    t.row_hl = get_any("modifier", "mod_bg_highlight")
    t.box    = get_any("modifier", "mod_box")
    t.arrow  = get_any("modifier", "blue_arrow")
    t.p      = { get_any("modifier", "1p"), get_any("modifier", "2p") }
    t.icon   = {
        auto     = get_any("modifier", "mod_auto"),
        speed    = get_any("modifier", "mod_baisaku"),
        sanbai   = get_any("modifier", "mod_sanbai"),
        yonbai   = get_any("modifier", "mod_yonbai"),
        -- one badge per speed value, like the cabinet (x1.1 .. x1.9, x2, x2.5, x3, x3.5, x4)
        speed_by_value = {
            [11] = get_any("modifier", "mod_speed_x1_1"), [12] = get_any("modifier", "mod_speed_x1_2"),
            [13] = get_any("modifier", "mod_speed_x1_3"), [14] = get_any("modifier", "mod_speed_x1_4"),
            [15] = get_any("modifier", "mod_speed_x1_5"), [16] = get_any("modifier", "mod_speed_x1_6"),
            [17] = get_any("modifier", "mod_speed_x1_7"), [18] = get_any("modifier", "mod_speed_x1_8"),
            [19] = get_any("modifier", "mod_speed_x1_9"), [20] = get_any("modifier", "mod_speed_x2"),
            [25] = get_any("modifier", "mod_speed_x2_5"), [30] = get_any("modifier", "mod_speed_x3"),
            [35] = get_any("modifier", "mod_speed_x3_5"), [40] = get_any("modifier", "mod_speed_x4"),
        },
        display  = get_any("modifier", "mod_doron"),
        inverse  = get_any("modifier", "mod_abekobe"),
        kimagure = get_any("modifier", "mod_kimagure"),
        detarame = get_any("modifier", "mod_detarame"),
    }
    self._opt_ids_t = t
    return t
end

-- Row label / value strings become OutlinedText exactly as ModifierSelector::make_text
-- built them (modifier_text.font_size, white fill, black edge, thickness 3, spacing 2),
-- cached by content: the value set is small and closed (on/off, 1.0..4.0, the neiro
-- names, きまぐれ / でたらめ).
function SongSelect:_opt_text(s)
    if s == nil or s == "" then return nil end
    if not self._opt_texts then
        self._opt_texts = {}
        local cfg = tex.get_skin_config("modifier_text")
        self._opt_font = (cfg and cfg.font_size ~= 0 and cfg.font_size) or 26
    end
    local t = self._opt_texts[s]
    if t == nil then
        -- `song_select_main_diff_option` ids 13/15 `text_item_name`/`text_option_name`: size 26, border 3.0
        t = text.create_raw_text(s, self._opt_font, { 255, 255, 255, 255 }, { 0, 0, 0, 255 }, false, ol(3.0), 2.0) or false
        self._opt_texts[s] = t
    end
    return t or nil
end

-- Arcade cursor pulse — ROUND 53 (r53-anim-lumen-sweep): sampled from the cabinet's own
-- timeline.  `Scripts/anim/option_cursor.lua` is MC5 `cursor` of
-- `enso_normal/song_select/song_select_main_diff_option.nulm` exported per-frame: alpha
-- 1.0 -> 0.1992 over f0..29 and back over f29..59 (60 f = 1000 ms loop).  The old hand
-- triangle put the floor at 500 ms; the cabinet's floor is f29 = 483.3 ms — the halves are
-- 29/30 frames, not 30/30.  Fail-soft: the NULL clip returns nil and the hand triangle
-- below is the fallback (ROUND 8 rule).  NB: this block sits ABOVE the file's shared
-- `glow_anim` require (~l.760), so it guards its own require — a later local is not in
-- scope up here and referencing it would kill the whole screen script at load.
local _oc_ok, _oc_anim = pcall(require, "anim/sampler")
local OPT_CURSOR_CLIP = (_oc_ok and _oc_anim.safe("anim/option_cursor"))
                        or { get = function() return nil end, first = function() return 0 end,
                             last = function() return 0 end }
local OPT_CURSOR_LEN  = math.max(1, OPT_CURSOR_CLIP:last() - OPT_CURSOR_CLIP:first() + 1)
local function opt_cursor_alpha(ms)
    local f = (ms * 0.06) % OPT_CURSOR_LEN
    local a = OPT_CURSOR_CLIP:get("#3@0", f, "a")
    if a then return a end
    local u = (ms % OPT_CURSOR_MS) / OPT_CURSOR_MS
    local tri = (u < 0.5) and (u * 2) or ((1 - u) * 2)
    return 1.0 - (1.0 - OPT_CURSOR_MIN) * tri
end

-- One board. `rows` is the modifier_rows() shape ({label, value, changed, greyed, name});
-- `cursor` is the 1-based row the cursor is on (nil = none, e.g. after the decide).
function SongSelect:draw_option_board(player, offset, rows, cursor, change)
    local ids = self:_opt_ids()
    if not ids.top or not ids.row or not ids.box then return end
    local p     = player:player_num()
    local p2cfg = tex.get_skin_config("option_p2")
    local x     = (p - 1) * ((p2cfg and p2cfg.x) or 1490)
    local pitch = tex.get_skin_config("modifier_offset")
    pitch       = (pitch and pitch.y ~= 0 and pitch.y) or 61
    local tcfg  = tex.get_skin_config("modifier_text")
    local ocfg  = tex.get_skin_config("modifier_offset_2")
    local text_x, text_y = (tcfg and tcfg.x) or 300, (tcfg and tcfg.y) or 1178
    local name_x         = (ocfg and ocfg.x) or 44
    local arrow_span     = (ocfg and ocfg.y) or 140
    local n              = #rows
    local ms             = tex.get_current_ms()

    tex.draw_texture(ids.top, { x = x, y = offset })
    if ids.p[p] then tex.draw_texture(ids.p[p], { x = x, y = offset }) end
    if ids.bottom then tex.draw_texture(ids.bottom, { x = x, y = offset + n * pitch }) end

    for i = 1, n do
        local row    = rows[i]
        local row_y  = offset + (i - 1) * pitch
        local is_cur = (cursor == i)

        if ids.bg then tex.draw_texture(ids.bg, { x = x, y = row_y }) end
        tex.draw_texture(ids.row, { x = x, y = row_y })
        if row.greyed then
            -- arcade "grayout": the row plate becomes an opaque flat grey. mod_bg.png
            -- carries the arcade's 154/256 white, so the grey is laid on top with the
            -- (233/256) highlight plate to get close to opaque without a new texture.
            if ids.row_hl then tex.draw_texture(ids.row_hl, { x = x, y = row_y, color = OPT_GREY }) end
        elseif is_cur and ids.row_hl then
            tex.draw_texture(ids.row_hl, { x = x, y = row_y, fade = opt_cursor_alpha(ms) })
        end
        tex.draw_texture(ids.box, { x = x, y = row_y, color = row.changed and OPT_CHANGED or nil })

        local label = self:_opt_text(row.label)
        if label then label:draw({ x = name_x + x, y = text_y + row_y }) end
        local value = self:_opt_text(row.value)
        if value then
            value:draw({ x = math.floor(text_x - value.width / 2 + 0.5) + x, y = text_y + row_y })
        end

        -- The engine's own row icon (the arcade board has none; this keeps the
        -- PyTaikoGreen affordance the skin already shipped).
        -- row.state is the raw engine value (modifier_speed / modifier_random /
        -- 0-1 for a bool), so the icon never depends on the localised `value` string.
        local icon
        local st = row.state or 0
        if row.name == "speed" then
            if st > 10 then
                -- exact badge for the value; nearest lower step for anything in between
                local best
                for v, id in pairs(ids.icon.speed_by_value or {}) do
                    if id and v <= st and (not best or v > best) then best = v end
                end
                icon = best and ids.icon.speed_by_value[best] or nil
            end
            if not icon then
                if st >= 40 then icon = ids.icon.yonbai
                elseif st >= 30 then icon = ids.icon.sanbai
                elseif st > 10 then icon = ids.icon.speed end
            end
        elseif row.name == "random" then
            if st == 2 then icon = ids.icon.detarame
            elseif st == 1 then icon = ids.icon.kimagure end
        elseif row.changed then
            icon = ids.icon[row.name]
        end
        if icon then tex.draw_texture(icon, { x = x, y = row_y }) end

        if is_cur and ids.arrow then
            local nl, nr = 0, 0
            if change and change.active then
                local amp = OPT_ARROW_NUDGE * math.max(0, math.min(1, change.fade))
                if change.dir > 0 then nl = amp else nr = amp end
            end
            tex.draw_texture(ids.arrow, { x = x - nl, y = row_y })
            tex.draw_texture(ids.arrow, { mirror = "horizontal", x = x + arrow_span + nr, y = row_y })
        end

        if row.greyed then tex.draw_texture(ids.row, { x = x, y = row_y, color = OPT_SCRIM }) end
    end
end

-- Engine entry point. kind: 1 = 演奏オプション, 2 = the standalone 音色 panel.
-- The arcade has no separate 音色 board — 音色 is selector_8 of the option board
-- (song_select_option_selector.lua sessionIndex 8) — so the standalone panel, which
-- the engine cursor can still reach (Difficulty::NEIRO), is drawn as the same board
-- with a single 音色 row.
function SongSelect:draw_option_panel(player, kind)
    if kind == 1 then
        local rows = player.modifier_rows and player:modifier_rows()
        local off  = player.modifier_offset and player:modifier_offset()
        if not rows or not off then return end
        local cursor = player.modifier_index and player:modifier_index() or nil
        if cursor and (cursor < 1 or cursor > #rows) then cursor = nil end
        if player.modifier_confirmed and player:modifier_confirmed() then cursor = nil end
        self._mod_off[player:player_num()] = off
        self:draw_option_board(player, off, rows, cursor,
                               player.modifier_change and player:modifier_change())
    else
        local names = player.neiro_names and player:neiro_names()
        local off   = player.neiro_offset and player:neiro_offset()
        if not names or not off then return end
        -- No row LABEL here: `tex.get_skin_config` returns geometry only, not the
        -- localised string, and NeiroSelector has no lua_rows() to take it from.
        -- The 音色 row inside the option board (kind 1) does carry its label.
        local idx  = (player.neiro_index and player:neiro_index()) or 1
        local rows = { { name = "neiro", state = idx - 1, value = names[idx] or "",
                         changed = (idx ~= 1), greyed = false } }
        self:draw_option_board(player, off, rows, 1, nil)
    end
end

--------------------------------------------------------------------------------
-- Scrolling genre backdrop (enso_normal/song_select/song_select_bg, 2880x1080 per genre)
--------------------------------------------------------------------------------
function SongSelect:draw_background(nav)
    self._frame_no = (self._frame_no or 0) + 1
    self:refresh_folder_context(nav)
    local info = tex.get_texture_info("box", "background")
    local width = info.width
    local move = nav:background_move().attribute % width
    local fade_change = nav:background_fade_change().attribute
    local last_frame = nav:last_bg_genre_frame()
    local curr_frame = nav:bg_genre_frame()
    for i = 0, width * 2 - 1, width do
        tex.draw_texture(self._id_background, { frame = last_frame, x = i - move })
        tex.draw_texture(self._id_background, { frame = curr_frame, x = i - move, fade = 1 - fade_change })
    end
end

--------------------------------------------------------------------------------
-- Board chrome: 3-slice (cap / centre / cap) from a 960x352 texture, centred on (bx,by).
-- cen_h = 52 closed, 240 open (arcade song_board: centre scaled 0.22 -> 1.0).
--------------------------------------------------------------------------------
-- ROUND 5: the engine loads textures with mipmaps + TRILINEAR filtering, so squashing the 240-row centre
-- slice to 52 rows (closed bar) made the GPU sample a coarse mip level -> the board's left/right ends
-- (outline, rounded panel edge) came out blurred. The centre rows of bar_genre / bar_genre_back are
-- vertically uniform, so a closed/partly-open board now takes a cen_h-row slice out of the middle of the
-- centre region and draws it 1:1 (crop_center = true, default). folder_graphic's open layout (title band
-- on top) is not uniform -> pass crop_center = false while it animates (scaled, cross-faded under the
-- closed look) and it is 1:1 anyway when fully open.
-- ROUND 61 (r61-genreanim-from-chn-source): `sx` is the board's HORIZONTAL scale about
-- its own centre, the channel the folder-ENTER animation drives (0.025 -> 1 over clip
-- f115..f128 of common_song_select_main_song_board sprite 222; see anim/genre_open).
-- Every draw_texture dest rect is `mapping.x2 * scale + params.x2` wide
-- (src/libs/texture.cpp:707), i.e. params.x2 is an ADDITIVE width delta on the natural
-- width -- exactly how the existing `y2 = h - 352` calls already drive the height.  So a
-- horizontal scale is `x2 = BOARD_W * (sx - 1)` plus a re-centred x, with no engine
-- change.  sx = nil/1 leaves every pre-r61 call byte-identical.
-- A furled board (sx < 1) is not a squashed copy of the 960-wide texture: squashing
-- averages the outline away and makes the GPU pick a small mip level (the LOD follows
-- the larger axis), which blurs every row.  The cabinet scales a vector shape, so its
-- edges stay sharp at any width.  Five columns get the same result from the bitmap:
-- the two rounded corners at 1:1 (40 px), and the coloured band / inner panel / band
-- between them scaled in proportion, each drawn from an 8 px flat slice of itself and
-- MAGNIFIED to its width.  Column layout of every bar_genre frame and the back board:
-- corner 0..40 | band 40..83 | panel 83..877 | band 877..920 | corner 920..960.
local function draw_board(bar_id, frame, bx, by, cen_h, fade, crop_center, sx)
    local w = BOARD_W
    if sx and sx ~= 1.0 then w = math.max(2, math.floor(BOARD_W * sx + 0.5)) end
    local x = math.floor(bx - w / 2 + 0.5)
    local top = math.floor(by - cen_h / 2 + 0.5) - CAP_H
    local src_y, src_h = CAP_H, CEN_SRC_H
    if crop_center ~= false and cen_h < CEN_SRC_H then
        src_h = math.max(1, math.floor(cen_h + 0.5))
        src_y = CAP_H + math.floor((CEN_SRC_H - src_h) / 2)
        cen_h = src_h
    end
    -- the three horizontal bands (top cap / centre / bottom cap) as {y, src_y, src_h, dst_h}
    local bands = { { top, 0, CAP_H, CAP_H }, { top + CAP_H, src_y, src_h, cen_h },
                    { top + CAP_H + cen_h, CAP_H + CEN_SRC_H, CAP_H, CAP_H } }
    if w == BOARD_W then
        for _, b in ipairs(bands) do
            tex.draw_texture(bar_id, { frame = frame, x = x, y = b[1], y2 = b[4] - 352, fade = fade,
                                       src = { x = 0, y = b[2], width = BOARD_W, height = b[3] } })
        end
        return
    end
    local CORNER, BAND, PANEL = 40, 43, 794          -- source widths; 40+43+794+43+40 = 960
    local cols
    if w >= 2 * CORNER then
        local corner = CORNER
        local inner  = w - 2 * corner
        local band   = math.floor(inner * BAND / (2 * BAND + PANEL) + 0.5)
        local panel  = inner - 2 * band
        -- columns: {src_x, src_w, dst_x, dst_w}
        cols = {
            { 0, CORNER, x, corner },
            { CORNER + BAND / 2 - 4, 8, x + corner, band },
            { BOARD_W / 2 - 4, 8, x + corner + band, panel },
            { BOARD_W - CORNER - BAND / 2 - 4, 8, x + corner + band + panel, band },
            { BOARD_W - CORNER, CORNER, x + w - corner, corner },
        }
    else
        -- narrower than both corners (the last frames of a furl / first of an unfurl): the
        -- cabinet's vector shape is a solid bar with its outline all round, so draw the
        -- two outline columns (texture x 13..20 / 940..947) at a few px each and fill the
        -- rest from the flat centre slice -- squeezing the rounded corners instead left
        -- two hollow strokes with nothing between them.
        local ol = math.max(2, math.min(8, math.floor(w / 6)))
        local mid = math.max(0, w - 2 * ol)
        cols = {
            { 13, 8, x, ol },
            { BOARD_W / 2 - 4, 8, x + ol, mid },
            { 940, 8, x + ol + mid, ol },
        }
    end
    for _, b in ipairs(bands) do
        for _, c in ipairs(cols) do
            if c[4] > 0 then
                tex.draw_texture(bar_id, { frame = frame, x = c[3], y = b[1], x2 = c[4] - BOARD_W, y2 = b[4] - 352,
                                           fade = fade, src = { x = c[1], y = b[2], width = c[2], height = b[3] } })
            end
        end
    end
end

-- Yellow cursor glow around the board (arcade cursor_top/center/bottom): the glow's caps
-- start 24px outside the board caps and its centre stretches with the board centre.
function SongSelect:draw_glow(bx, by, cen_h, fade)
    local x = math.floor(bx - GLOW_W / 2 + 0.5)
    local top = math.floor(by - cen_h / 2 + 0.5) - CAP_H - 24
    local src_h = math.max(1, math.min(240, math.floor(cen_h + 0.5)))   -- ROUND 5: 1:1 slice, no mip blur
    cen_h = src_h
    tex.draw_texture(self._id_glow, { x = x, y = top, y2 = GLOW_CAP_H - 400, fade = fade,
                                      src = { x = 0, y = 0, width = GLOW_W, height = GLOW_CAP_H } })
    tex.draw_texture(self._id_glow, { x = x, y = top + GLOW_CAP_H, y2 = cen_h - 400, fade = fade,
                                      src = { x = 0, y = GLOW_CAP_H + math.floor((240 - src_h) / 2), width = GLOW_W, height = src_h } })
    tex.draw_texture(self._id_glow, { x = x, y = top + GLOW_CAP_H + cen_h, y2 = GLOW_CAP_H - 400, fade = fade,
                                      src = { x = 0, y = GLOW_CAP_H + 240, width = GLOW_W, height = GLOW_CAP_H } })
end

-- ROUND 51: open_progress (the engine's raw 233 ms open_anim ramp) removed - every wheel
-- panel now runs the cabinet's own select_on/select_off timelines on the shared
-- wall-clock machinery below (wheel_open_t / wheel_close_step).

-- ROUND 8 — the cursor glow's pulse.  Round 5 wrote `0.75 + 0.25*sin(2*pi*t)`, a 1 Hz sine
-- between 0.75 and 1.0, and Graphics/song_select/MAPPING.md recorded it as "Pulses 0.75-1.0".
-- Both are wrong.  `common_song_select_main_song_board.nulm` char 16 (the cursor glow MC that
-- cursor_top/center/bottom all instance) is a 60-frame loop in which the pulsing gradient --
-- `#12@0`/`#12@1`, i.e. `cursor_center__shape12_g0`, which is exactly what
-- `bar_genre_overlay.png` is a bake of -- runs 1.0 -> 0.0 -> 1.0, quadratic in over 29 frames
-- and quadratic out over 30.  (The third leaf `#14@2` barely moves, but it is NOT in our
-- bake, so the plate must carry the full 1 -> 0 swing.)  Sampled from `anim/cursor_glow`.
-- ROUND 8: fail soft.  `pcall` the require and use `anim.safe`, which hands back a NULL
-- clip when a generated table is missing or malformed, so every reader below falls
-- through to its `or <constant>` default instead of taking the screen down.
local _ga_ok, glow_anim = pcall(require, "anim/sampler")
if not _ga_ok then glow_anim = { safe = function()
    -- sampler.lua itself is missing: hand back the same NULL clip shape it would
    local n = {}
    n.at = function() return nil end; n.get = function() return nil end
    n.label = function() return nil end; n.has = function() return false end
    n.first = function() return 0 end;  n.last = function() return 0 end
    return n
end } end
local GLOW_CLIP  = glow_anim.safe("anim/cursor_glow")
local GLOW_LEN   = math.max(1, GLOW_CLIP:last() - GLOW_CLIP:first() + 1)   -- 60 frames = 1 s
-- song-board oni/ura chip cross-fade loop (see draw_plates)
local URA_LOOP     = glow_anim.safe("anim/song_board_ura")
local URA_LOOP_F0  = URA_LOOP:first()                                      -- label 'ura' = f10
local URA_LOOP_LEN = math.max(1, URA_LOOP:last() - URA_LOOP_F0 + 1)        -- 180 frames = 3 s
local function glow_pulse()
    local f = (tex.get_current_ms() * 0.06) % GLOW_LEN
    return GLOW_CLIP:get("#12@0", f, "a") or 1.0
end

-- Arcade genre board open / close (CS/main_genre_board 'board' timeline, 60 fps, decoded frame by frame):
--   select_on (f5) -> hold 3 f -> f8..f13 board grows LINEARLY (centre 0.217 -> 1.0, caps +-26 -> +-120,
--   title 0 -> -94, cursor glow alpha 0 -> 1); characters slide +-340 -> +-450 and fade in with a
--   quadratic ease-out over the same 5 f, then settle 450 -> 440 (quadratic ease-in, f13..f16);
--   intro text fades in linearly f13..f18 (83 ms); 'on' = f20.
--   select_off (f30) -> f30..f35 characters fade out (alpha 1-u^2) and drift 440 -> 390 (50*u^2),
--   intro text fades out linearly; f33..f38 board shrinks linearly back to closed (title/glow follow).
-- The engine gives Lua open_anim (0 -> 1 linear over 233 ms, after the 133 ms slide) and no close
-- animation, so: open = timeline driven by t = p * 233 ms; close = own timer keyed by folder name,
-- started the frame a folder that was drawn open is drawn closed.
-- ROUND 7 — the four channels below are no longer hand-fitted closed forms: they are
-- SAMPLED out of the cabinet's own timeline, exported by scratchpad/anim_export.py from
-- `common_songselect/common_song_select_main_genre_board.nulm` sprite 73 (labels
-- init 0 / off 1 / select_on 5 / on 20 / select_off 30 / genre_deceide 45 / return 150).
-- See ANIM_PIPELINE.md.  What the export changed (everything else round 5/6 derived by
-- hand was confirmed exact, to the 1/256 the engine quantises alpha to):
--   * CLOSE used to hold the open board for 5 frames after `select_off` and then shrink
--     over 5 (FCLOSE_FADE = FCLOSE_SHRINK = 83 ms).  The cabinet holds for THREE frames
--     (f30..f33) and shrinks f33..f38, so ours ran 33 ms late and 33 ms long.
--   * the whole close is 8 frames (133 ms), not 10 (166 ms).
local folder_anim = glow_anim   -- ROUND 8: same guarded module (see line ~435)
local FOLDER_CLIP = folder_anim.safe("anim/folder_board")
local F_SELECT_ON  = FOLDER_CLIP:label("select_on")  or 5
local F_SELECT_OFF = FOLDER_CLIP:label("select_off") or 30
local FOLDER_CLOSE_END_F = 8            -- select_off + 8 = f38, the last frame that moves
local FOLDER_CLOSE_MS = FOLDER_CLOSE_END_F / 0.06
-- Duration the engine keeps the box in "closed" state between navigate() and expand_box()
-- completing.  Comes from the C++ call set_positions(false, 166) in SongSelectPlayer.
-- NOT a Lua constant — if the engine changes this, update NAV_REOPEN_MS to match.
local NAV_REOPEN_MS  = 166
-- Threshold used in draw_folder_closed to distinguish a transient navigation closure
-- (< NAV_REOPEN_MS) from a genuine closed state that should reset the open-animation
-- timer.  Must be > max(FOLDER_CLOSE_MS, NAV_REOPEN_MS).  Currently both are <233 ms.
local STALE_TIMER_MS = math.max(FOLDER_CLOSE_MS, NAV_REOPEN_MS) + 50
-- ROUND 39 (r39-genreanim-recheck): the cabinet does NOT start growing the newly-focused
-- board as soon as the cursor lands.  song_select_song_main.lua's `MoveCursor` (fired by
-- real INPUT_1P_LEFT/RIGHT, not an attract-only path -- ROUND 32's "attract-mode-only /
-- IsAllOnpuEnd" claim for this constant does not match the source; that string does not
-- appear anywhere in song_select_song_main.lua) sets state "Scroll", which sets
-- `openPanelWaitCount = kopenPanelWaitCount` (61, line 27) and returns to "Main". Every
-- Main tick calls `ProcKanbanWaitCountToSelectOn` (line 1394-1413), which counts
-- `openPanelWaitCount` down to 0 before it ever calls `GetGenreKanbanMc/GetSongKanbanMc
-- :GotoAndPlay("select_on")` (line 1408/1410) -- i.e. the board stays fully closed for
-- 61 script-logic ticks after the input, at the established Common.FPS = 120 script clock
-- (this session's ground truth; see ENGINE_BINDINGS.md), = 508.3 ms, before growth even
-- begins. ROUND 17's wall-clock timer instead started growth as soon as the C++
-- position-slide finished (NAV_REOPEN_MS = 166 ms after navigate()), ~342 ms too early --
-- the whole open animation finished around navigate()+400ms instead of the cabinet's
-- navigate()+741ms, reading as rushed/foreshortened rather than a bug-free replay of the
-- ROUND 17 growth curve (which is itself correct, confirmed by re-measurement this round).
local CABINET_OPEN_WAIT_MS = 61 / 120 * 1000                                   -- = 508.33 ms
local EXTRA_OPEN_HOLD_MS   = math.max(0, CABINET_OPEN_WAIT_MS - NAV_REOPEN_MS) -- = 342.33 ms
-- the centre band's scaleY at the two rest states, so `pb` stays the 0..1 the rest of this
-- file (and the parent skin's board crossfade) already speaks
local SY_CLOSED, SY_OPEN = 0.2168, 1.0
local function clamp01(v) return math.max(0.0, math.min(1.0, v)) end

-- ROUND 51 (r51-genreanim-chn-port): the song-board timeline, exported the same way as
-- the genre one (common_song_select_main_song_board.nulm sprite 222, labels identical to
-- sprite 73's: init 0 / off 1 / select_on 5 / on 20 / select_off 30 / genre_deceide 45).
-- Growth f8..f13 matches the genre board; song_kanban_info (diff plates/subtitle/BPM row)
-- fades in AFTER the growth (f13 a=0.5 -> f18 a=1); instance_icon (NEW) fades out f5..f10
-- on open and back in f38..f43 after the close shrink.
local SONG_CLIP = folder_anim.safe("anim/song_board")
local S_SELECT_ON  = SONG_CLIP:label("select_on")  or 5
local S_SELECT_OFF = SONG_CLIP:label("select_off") or 30
local SONG_CLOSE_END_F = 13                     -- f30..33 hold, f33..38 shrink, f38..43 NEW icon fade-back
local SONG_CLOSE_MS = SONG_CLOSE_END_F / 0.06   -- = 216.7 ms
local SONG_STALE_MS = math.max(SONG_CLOSE_MS, NAV_REOPEN_MS) + 50

-- ROUND 56 (r56-result-songselect-polish): the cabinet's folder enter/exit WHEEL
-- choreography — the "rotation-like" motion of the the reference build 分類開啟 the user reported.
-- Source: enso_normal/song_select/song_select_main_genre.nulm sprite 19 (the kanban
-- container that owns the 8 wheel slots), label `genre_deceide` f45..f150:
--   * f82..f99 every NON-focused slot slides exactly 3 slot pitches OUTWARD along the
--     wheel's tilted line (quadratic ease-in, outer slots first, ~13 f each) — since all
--     bars sweep together along the drum axis, the wheel reads as ROTATING away;
--   * f99..f128 hold off-screen while the slots swap content (genre -> folder songs);
--   * f128..f145 they ease back IN, inner slots first (`return` f150 = at rest).
-- No slot carries any rotation matrix (verified: every m = [a,0,0,d], rot = 0 across
-- f0..f150 of both sprite 19 and the board sprite 73) — the "rotation" is this
-- coordinated diagonal sweep.  This engine opens folders INLINE (the songs insert into
-- the live wheel), so the out-leg has no counterpart content to show; what maps 1:1 is
-- the second half: from the moment the folder context flips, boxes sit swept-out and
-- fly back in on the cabinet's own curves.  t0 -> clip f99 (hold), so the fly-in starts
-- 483 ms later and lasts 283 ms, exactly the cabinet's post-swap timing.
-- Slot geometry: this skin's wheel (song_select_row_pitch 135/120, curve 40, MAPPING.md
-- R29) matches the cabinet slot table (pitch 135/255-center, curve 40/50) 1:1, so clip
-- deltas are used unscaled.  FAIL-SOFT: missing table -> zero offset (pre-r56 look).
local WHEEL_FLY   = folder_anim.safe("anim/wheel_decide")
-- ROUND 61: was 99.  R56 anchored the folder-context flip on "bars fully swept out"
-- (sprite 19 f99), but the reference build pins the flip to the CONTENT SWAP, which OpenFolderState
-- (ai_song_select_song_main.lua:350) performs when the board clip reads frame 114 --
-- and f114 is inside sprite 19's own swap hold (f99..f128), so the two clips agree.
-- Anchoring at 99 started the wheel's return leg 15 frames (250 ms) early, which also
-- put it out of phase with the board unfurl that shares the same genre_deceide play.
-- ROUND 85: FLY_HOLD_F0 removed -- the fly rides the shared leg clock (see fly_offset).
local FLY_END_F   = WHEEL_FLY:label("return") or 150
local FLY_TRACKS_UP   = { "kanban_genre_2", "kanban_genre_4", "kanban_genre_6", "kanban_genre_8" }
local FLY_TRACKS_DOWN = { "kanban_genre_3", "kanban_genre_5", "kanban_genre_7" }
local FLY_CENTER_Y, FLY_ROW_GAP, FLY_ROW_PITCH = 540, 120, 135
do  -- the wheel geometry is skin_config-driven; follow it if restated
    local ok, pitch = pcall(tex.get_skin_config, "song_select_row_pitch")
    if ok and pitch and pitch.x and pitch.x > 0 then
        FLY_ROW_PITCH = pitch.x
        if pitch.y and pitch.y > 0 then FLY_ROW_GAP = pitch.y end
    end
end
-- one-binary A/B, same env-gate pattern as ROUND 45/48/55.  NB skin Lua runs
-- without sol::lib::os, so this reads the NEW r56 engine binding tex.get_env
-- (script.cpp); on an older exe the pcall fails soft -> gate off (arcade path).
local function read_env(name)
    local ok, v = pcall(function() return tex.get_env(name) end)
    if ok and v ~= nil and v ~= "" then return v end
    return nil
end
local R56_DISABLED = read_env("YATAIDON_R56_DISABLE") ~= nil
-- ROUND 63 (r63-duplicate-song-box): env-gated per-frame wheel trace, see draw_box.
local R63_DBG   = read_env("YATAIDON_R63_DEBUG") ~= nil
local _r63_ms, _r63_seen, _r63_n = nil, nil, 0

-- ROUND 63 (r63-duplicate-song-box) -- a wheel animation-timer key MUST be unique
-- per BOX.  `text_name` is not: TJAParser deliberately STRIPS "-New Audio-" /
-- "-Old Audio-" (and 「新曲」/「旧曲」) out of TITLE and raises
-- ex_data.new_audio / ex_data.old_audio instead (src/libs/parsers/tja.cpp:460),
-- so the two charts of one song carry the SAME display title; a library can also
-- hold two genuinely different charts under one title (「1・2・3」 vs
-- 「1・2・3 (2019)」).  When two boxes share a key they share ONE open/close clock,
-- and the focused twin's per-frame wheel_open_t() clears wheel_close_at/
-- wheel_close_start every frame, so the unfocused twin re-enters
-- wheel_close_step at e = 0 forever -- i.e. it is redrawn as a FULLY OPEN,
-- glowing board that never closes.  That is the user's 「同時選兩首歌」.
-- `box:path()` is the r63 engine binding (box_lua_bindings.cpp); the pcall keeps
-- an older binary on the pre-r63 title key instead of erroring.
local function box_id(box)
    local ok, p = pcall(function() return box:path() end)
    if ok and p and p ~= "" then return p end
    -- Pre-r63 binary: no path binding.  Fall back to the strongest identity the
    -- OLD bindings can build -- title + subtitle + ex_data flag.  That alone
    -- separates the -New Audio- / -Old Audio- pair the user reported (flag 1 vs
    -- 2), so the defect is fixed even on a binary that predates this round; it
    -- is only a weaker key for two charts that agree on all three.
    local id = box.text_name or ""
    local ok_s, sub = pcall(function() return box.text_subtitle end)
    if ok_s and sub then id = id .. "\1" .. sub end
    local ok_x, x = pcall(function() return box:ex_data_flag() end)
    if ok_x and x then id = id .. "\1" .. tostring(x) end
    return id
end
-- ROUND 61 (r61-genreanim-from-chn-source) -- the folder-ENTER leg, from the the reference build source.
--
-- What the five previous rounds on this element could not see: anim/folder_board and
-- anim/song_board were exported `--range 0,50`, and `genre_deceide` is label f45 on BOTH
-- clips.  Sprite 73 is 255 frames long and sprite 222 is 300; the exports therefore carry
-- five frames of a leg that runs to f255 / f150, and in those five frames nothing moves.
-- Every round since R17 has been tuning the WHEN of an animation whose WHAT was missing.
--
-- the reference build authority (the reference research notes, the reference-sourced
-- cabinet script; the non-AI song_select_song_main.tlb is structurally identical):
--   :1027 SelectGenreFolder -> GetGenreKanbanMc(kSelectNumber+openOffset)
--                              :GotoAndPlay("genre_deceide")   (+ the song board, + wheel)
--   :25   kOpenFolderChangeFrame  = 114
--   :20   kCloseFolderChangeFrame = 219
--   :350  OpenFolderState polls `GetGenreKanbanMc(..).current_frame == 114`, and only
--         THEN calls SetSongDataAll() -- the genre->song content swap -- and sends the
--         board to frame 219 (reveal) or "off".
-- The genre board's slab goes alpha 0 at f113, one frame before that test, so the swap is
-- hidden behind a blank board.  Our engine opens folders inline and `Navigator:
-- current_folder()` flips at exactly that swap, so our folder-context event IS clip f114.
--
-- The motion itself (anim/genre_open, sampled over the real extent):
--   the incoming song board is placed at f113 already at FULL HEIGHT but sx = 0.025 --
--   a vertical hairline -- and unfurls sideways to sx = 1 over f115..f128 (13 frames,
--   217 ms), ease-in.  Its title then fades in f132..f145.  Every content node is alpha 0
--   for the whole unfurl, so only the slab is drawn.
-- That horizontal unfurl is the "rotation-like" motion this element keeps being reported
-- for.  R56 checked for a rotation matrix and correctly found none (every m = [a,0,0,d]);
-- it looked for the wrong quantity, and only inside f0..f150 of sprite 19 / f0..f50 of
-- sprite 73 -- the sx sweep is on sprite 222 at f115..f128.
-- All of ROUND 61's state lives in ONE local: this file is already close to Lua's
-- 200-locals-per-function limit at chunk scope (adding nine plain locals here overflowed
-- it -- "too many local variables" at load time, i.e. the whole skin fails to parse).
local R61 = {
    -- one-binary A/B gate, same env pattern as R45/48/55/56.  Both names are
    -- honoured so an existing probe script keeps working.
    disabled = (read_env("YATAIDON_R85_DISABLE") or read_env("YATAIDON_R61_DISABLE")) ~= nil,
    dbg      = (read_env("YATAIDON_R85_DEBUG")   or read_env("YATAIDON_R61_DEBUG")) ~= nil,
    clip     = nil,      -- anim/genre_open, or nil -> every consumer falls back
    -- ROUND 85: the leg's own frame clock.  `t0` is the wall-clock ms at which
    -- frame `f0` was showing; `kind` is "open" or "close"; `swapped` says the
    -- engine has told us the content swap happened, which is the ONLY thing
    -- that lets the clip past its pre-swap hold.
    t0 = nil, f0 = 0, kind = nil, swapped = false,
    -- clip landmarks (overwritten from the generated table)
    open_f0 = 45, open_swap = 114, open_end = 145,
    close_f0 = 150, close_swap = 219, close_end = 250,
    open_furl = 100,     -- first frame the decided genre board furls shut (genre_board_sx < 1)
    -- event bookkeeping
    seq = nil,           -- last Navigator.wheel_event_seq we saw
    have_events = false, -- engine actually exposes the r85 channel
    snap_once = false, snap_ms = nil,     -- one-shot open modes, armed by events
    nohold_once = false, nohold_ms = nil,
}
do
    local ok, t = pcall(require, "anim/genre_open")
    if ok and type(t) == "table" and type(t.tracks) == "table" then
        R61.clip = t
        for _, k in ipairs({ "open_f0", "open_swap", "open_end",
                             "close_f0", "close_swap", "close_end" }) do
            if t[k] then R61[k] = t[k] end
        end
        local rows = t.tracks and t.tracks.genre_board_sx
        if rows then
            for i = 1, #rows do
                if rows[i][2] < 0.999 then R61.open_furl = rows[i][1]; break end
            end
        end
    end
end
-- Linear sampler over the generated {frame, v1, v2...} rows.  Fail-soft: an absent table
-- or track returns nil and every consumer falls back to its pre-r61 path.
local function go_get(name, f, col)
    if not R61.clip then return nil end
    local rows = R61.clip.tracks[name]
    if not rows or #rows == 0 then return nil end
    col = col or 1
    if f <= rows[1][1] then return rows[1][col + 1] end
    if f >= rows[#rows][1] then return rows[#rows][col + 1] end
    for i = 1, #rows - 1 do
        local a, b = rows[i], rows[i + 1]
        if f >= a[1] and f <= b[1] then
            local span = b[1] - a[1]
            if span <= 0 then return a[col + 1] end
            local u = (f - a[1]) / span
            return a[col + 1] + (b[col + 1] - a[col + 1]) * u
        end
    end
    return rows[#rows][col + 1]
end
-- ANCHORING -- ROUND 85, and this is the structural half of this round.
--
-- On the cabinet the swap is instantaneous: at clip f114 OpenFolderState calls
-- SetSongDataAll() and the boards the script is ALREADY holding simply change what they
-- display.  This engine loads the opened folder's songs on a loader thread
-- (Navigator::load_songs_inline_async), so the moment `current_folder()` flips there is
-- nothing to unfurl -- ROUND 61 proved it: a leg timed off the flip logged `armed` and
-- then ZERO drawn frames.  R61's workaround was to let the flip only ARM the leg and
-- start the clock on the first frame a song board happened to be drawn, i.e. the
-- animation's start was a function of loader latency.
--
-- It is now a function of a real engine event.  `Navigator` emits
-- WHEEL_EVENT_OPEN_BEGIN from begin_inline_load() (= SelectGenreFolder, clip f45) and
-- WHEEL_EVENT_OPEN_SWAP from flush_pending_boxes() at the instant the folder's boxes
-- exist, are sorted and are placed (= SetSongDataAll, clip f114); CLOSE_BEGIN/CLOSE_SWAP
-- are the same two points on the `return` leg (f150 / f219).  Between BEGIN and SWAP the
-- clip is CLAMPED one frame short of the swap, so the cabinet's own pre-swap window --
-- 69 frames / 1150 ms of hold, contents fade-out and the genre board's horizontal FURL --
-- is what covers the load, exactly as the brief's "hold the transition until the boxes
-- are ready" option.  The load is not serialised behind it: the loader still starts when
-- the engine's genre_bg move finishes (~1100 ms in, i.e. ~f111) and runs during the
-- clip's own blank stretch.  If the load is slower than the clip, the board simply waits
-- at f113 -- blank, which is what the cabinet shows there anyway.
--
-- Result: the unfurl ALWAYS begins on the swap frame and always lasts 13 clip frames.
local function leg_f()
    if R61.disabled or not R61.clip or not R61.t0 then return nil end
    local f = R61.f0 + (tex.get_current_ms() - R61.t0) * 0.06
    if not R61.swapped then
        -- hold until the engine says the swap happened.  The close leg holds one frame
        -- short of its swap; the OPEN leg holds one frame short of the FURL (f99): the
        -- cabinet loads a folder inside its 69-frame decide, this engine can take a
        -- couple of seconds, and parking on f113 (board already furled away, boxes
        -- flown out) left the screen empty for all of that time.  Parked on f99 the
        -- decided board stays up as the loading state and the furl -> unfurl pair
        -- plays back to back once the swap lands (see leg_event).
        local hold = (R61.kind == "close") and (R61.close_swap - 1) or (R61.open_furl - 1)
        if f > hold then f = hold end
        return f
    end
    local fin = (R61.kind == "close") and R61.close_end or R61.open_end
    if f >= fin then R61.t0 = nil; R61.kind = nil; return nil end
    return f
end

-- The engine's own box fade, except during the pre-swap half of the CLOSE leg: there the
-- engine fades the song boxes and the back board out in 166 ms, while the cabinet keeps
-- them solid until they fly out (f184..204) / the board furls (f205..217) -- those come
-- from the clip channels, so the engine fade is ignored until the swap.
local function box_fade(box)
    if R61.kind == "close" and R61.t0 and not R61.swapped then return 1.0 end
    local v = box:fade()
    -- The binding differs across binaries: master returns self.fade.get() (= a float),
    -- but older shipped binaries return the raw FadeAnimation* (a sol userdata).  That
    -- one needs .attribute to produce the number, exactly like text_fade_in.attribute
    -- and nav:background_move().attribute elsewhere in this file.
    if type(v) == "number" then return v end
    if v and v.attribute ~= nil then return v.attribute end
    return 1.0
end

-- Start / advance the leg.  `begin` puts the clock on the label frame; `swap` re-anchors
-- it on the cabinet's content-swap frame so the post-swap half is phase-exact no matter
-- how long the loader took.
local function leg_event(id)
    local now = tex.get_current_ms()
    if id == 3 then         -- OPEN_BEGIN
        R61.t0, R61.f0, R61.kind, R61.swapped = now, R61.open_f0, "open", false
        R61.dec_id, R61.dec_box = R61.last_open_id, R61.last_open_box   -- the decided board
    elseif id == 4 then     -- OPEN_SWAP
        -- the clock was parked on open_furl - 1 while the loader ran: resume from the
        -- furl so furl (f100..113) and unfurl (f114..) play back to back.  A swap that
        -- lands before the park point (tiny folder) just lets the clock keep running.
        local f = (R61.kind == "open" and R61.t0) and (R61.f0 + (now - R61.t0) * 0.06) or nil
        if not f or f >= R61.open_furl - 1 then
            R61.t0, R61.f0 = now, R61.open_furl
        end
        R61.kind, R61.swapped = "open", true
        R61.snap_once, R61.snap_ms = true, now      -- OpenFolderState ends GotoAndPlay("on")
    elseif id == 5 then     -- CLOSE_BEGIN
        R61.t0, R61.f0, R61.kind, R61.swapped = now, R61.close_f0, "close", false
    elseif id == 6 then     -- CLOSE_SWAP
        R61.t0, R61.f0, R61.kind, R61.swapped = now, R61.close_swap, "close", true
        R61.snap_once, R61.snap_ms = true, now      -- CloseFolderState ends GotoAndPlay("on")
    elseif id == 1 or id == 7 then                  -- SCENE_ENTRY / COURSE_BACK
        R61.nohold_once, R61.nohold_ms = true, now
    end
    -- id == 2 (CURSOR_MOVE) needs no flag: "hold" is open_start_mode's default.
    if R61.dbg then
        pcall(tex.log, string.format("[r85] event id=%d ms=%.1f", id, now))
    end
end

local _fly_dbg = read_env("YATAIDON_R56_DEBUG") ~= nil  -- log sampled offsets (verification aid)

-- ROUND 85: the wheel clip (song_select_main_genre.nulm sprite 19) carries the SAME two
-- labels at the SAME frames as both board clips -- `genre_deceide` f45, `return` f150
-- (label tables re-read this round) -- so one GotoAndPlay drives all three off ONE clock.
-- The fly therefore rides `leg_f()` directly and FLY_HOLD_F0 is gone: R61 moved it 99->114
-- to line the fly up with the swap, but with a real shared clock there is nothing to line
-- up by hand.  R56 only ever shipped the fly-IN half (this engine's INLINE folder model
-- has no content for the out-half, and the engine runs its own box slide before the swap),
-- so the offset stays zero until the swap event and then plays f114..f145 as before.
local function fly_offset(box)
    if R56_DISABLED then return 0.0, 0.0 end
    local f = leg_f()
    if not f then return 0.0, 0.0 end
    if R61.kind == "open" then
        if f >= FLY_END_F then return 0.0, 0.0 end
    elseif R61.kind == "close" then
        -- the CLOSE leg is the clip's `return` half: the song boxes fly out f184..204,
        -- wait off screen across the swap (f219) and the genre boxes fly back in
        -- f234..254; f255 (`genre_deceide2`) re-bases the clip, so stop just before it
        if f >= (WHEEL_FLY:label("genre_deceide2") or 255) - 1 then return 0.0, 0.0 end
    else
        return 0.0, 0.0
    end
    local st = box:draw_state()
    if st == "open" or st == "diff_select" then return 0.0, 0.0 end   -- the decided board stays
    local d = box:box_y() - FLY_CENTER_Y
    local ring = math.floor((math.abs(d) - FLY_ROW_GAP) / FLY_ROW_PITCH + 0.5)
    if ring < 1 then ring = 1 end
    local track
    if d < 0 then track = FLY_TRACKS_UP[math.min(ring, #FLY_TRACKS_UP)]
    else          track = FLY_TRACKS_DOWN[math.min(ring, #FLY_TRACKS_DOWN)] end
    local x, y = WHEEL_FLY:get(track, f, "tx"), WHEEL_FLY:get(track, f, "ty")
    if not x or not y then return 0.0, 0.0 end
    local rx = WHEEL_FLY:get(track, FLY_END_F, "tx") or 0.0
    local ry = WHEEL_FLY:get(track, FLY_END_F, "ty") or 0.0
    if _fly_dbg then pcall(tex.log, string.format("[r56fly] f=%.1f %s dx=%.1f dy=%.1f", f, track, x - rx, y - ry)) end
    return x - rx, y - ry
end

-- The decided genre board while the loader owns it (see draw_folder_open): drawn here,
-- after the wheel, whenever draw_box did not get to it this frame, until it has furled.
function SongSelect:draw_decided_bridge()
    if not R61.dec_box then return end
    local lf = (R61.kind == "open") and leg_f() or nil
    if lf and lf < R61.open_swap - 1 then
        if self._dec_frame ~= self._frame_no then
            pcall(self.draw_folder_open, self, R61.dec_box)
        end
    else
        R61.dec_id, R61.dec_box = nil, nil
    end
end

-- true while `box` is the genre board the player just decided on (open leg running)
local function is_decided(box)
    return R61.kind == "open" and R61.t0 ~= nil and R61.dec_id ~= nil and box_id(box) == R61.dec_id
end

-- ROUND 51: how a FRESH open animation starts is not one-size-fits-all.  From the real
-- the reference build song_select_song_main.lua state machine (scratchpad dec/, verified this round):
--   * cursor move        Scroll -> Main -> ProcKanbanWaitCountToSelectOn (61-tick hold,
--                        508 ms) then GotoAndPlay("select_on")            -> mode "hold"
--   * scene entry        SecondLoading plays select_on IMMEDIATELY (l.218) -> mode "nohold"
--   * folder enter/exit  OpenFolderState (l.369/371) and CloseFolderState (l.433) end with
--                        GotoAndPlay("on") - the focused board SNAPS fully open, no growth
--                                                                          -> mode "snap"
--   * back from course   ReLoading + CourseBackFlag (l.256-257) plays select_on
--     select             immediately                                       -> mode "nohold"
-- ROUND 17/39 played hold+growth in every case, which is why the board re-ran a 508 ms
-- dead hold + growth every time a folder was entered or left - visibly wrong vs cabinet.
-- ROUND 85: these one-shots are no longer armed by change-detection and TTL-bounded
-- guesses -- they are armed by the engine's real transitions (see leg_event / R61.snap_*).
-- The TTLs stay as a belt-and-braces bound only.
local _diff_seen_ms     = nil     -- last ms a diff_select box was drawn (legacy fallback)
local _last_box_draw_ms = nil     -- any-box draw clock (scene-entry / long-away detect)

-- One-shot flags expire so a mis-armed flag can never leak onto a much later
-- cursor-move open: the engine's folder transition and the scene intro both put the
-- next fresh open well inside these windows.
local OPEN_SNAP_TTL_MS   = 2000.0
local OPEN_NOHOLD_TTL_MS = 3000.0

local function open_start_mode(now)
    if R61.snap_once then
        R61.snap_once = false
        if R61.snap_ms and (now - R61.snap_ms) < OPEN_SNAP_TTL_MS then return "snap" end
    end
    if R61.nohold_once then
        R61.nohold_once = false
        if R61.nohold_ms and (now - R61.nohold_ms) < OPEN_NOHOLD_TTL_MS then return "nohold" end
    end
    -- Legacy fallback: only reachable on a binary with no r85 event channel.
    if not R61.have_events and _diff_seen_ms and (now - _diff_seen_ms) < 300.0 then
        return "nohold"
    end
    return "hold"
end

-- Shared wheel-panel open/close wall-clock timers (ROUND 17/39 folder machinery,
-- generalized to every panel kind this round).  Keys are "<kind>\0<name>" so a song
-- that happens to share a folder's title cannot collide.
local wheel_open_start = {}   -- key -> ms the open timer started
local wheel_open_mode  = {}   -- key -> "hold" | "nohold" | "snap"
local wheel_open_seen  = {}   -- key -> last ms drawn open
local wheel_close_at   = {}   -- key -> ms the close animation started
local wheel_close_start= {}   -- key -> ms the box first entered closed state this run
local wheel_last_draw  = {}   -- key -> last ms drawn open (gap/off-screen detection)
local wheel_last_fresh_key, wheel_last_fresh_ms = nil, nil   -- most recent fresh timer start

-- Returns t in ms into the select_on growth window (0..233, clamped), or nil while the
-- cabinet's 508 ms pre-growth hold is still running (draw fully closed).
local function wheel_open_t(key)
    local now = tex.get_current_ms()
    wheel_close_start[key] = nil     -- box is open; reset closed-state clock
    wheel_open_seen[key]   = now
    wheel_close_at[key]    = nil

    -- Gap detection (ROUND 17): off-screen for > NAV_REOPEN_MS with the animation already
    -- finished -> stale timer, reset so the replay starts from scratch.
    local last = wheel_last_draw[key]
    wheel_last_draw[key] = now
    if wheel_open_start[key] and last and (now - last) > NAV_REOPEN_MS then
        local hold = (wheel_open_mode[key] == "hold") and EXTRA_OPEN_HOLD_MS or 0.0
        if (now - wheel_open_start[key]) >= (hold + 233.0) then
            wheel_open_start[key] = nil
        end
    end

    if not wheel_open_start[key] then
        wheel_open_start[key] = now
        wheel_open_mode[key]  = open_start_mode(now)
        wheel_last_fresh_key, wheel_last_fresh_ms = key, now
    end
    local mode = wheel_open_mode[key]
    if mode == "snap" then return 233.0 end
    local elapsed = now - wheel_open_start[key]
    local hold = (mode == "hold") and EXTRA_OPEN_HOLD_MS or 0.0
    if elapsed < hold then return nil end
    return math.min(elapsed - hold, 233.0)
end

-- Closed-state bookkeeping + close animation.  Returns e in ms into the select_off
-- timeline while the close animation is running, else nil (draw at rest).
local function wheel_close_step(key, close_ms, stale_ms)
    local now = tex.get_current_ms()
    if not wheel_close_start[key] then
        wheel_close_start[key] = now
    end
    if wheel_open_start[key] and (now - wheel_close_start[key]) > stale_ms then
        wheel_open_start[key] = nil
    end
    local seen = wheel_open_seen[key]
    if seen and now - seen < 100 then      -- was drawn open a frame ago -> start closing
        wheel_close_at[key]  = now
        wheel_open_seen[key] = nil
    end
    local c = wheel_close_at[key]
    if c then
        local e = now - c
        if e < close_ms then return e end
        wheel_close_at[key] = nil
    end
    return nil
end

-- shared reader: `f` is an ABSOLUTE arcade frame on sprite 73's timeline.
-- pb = board progress 0..1, ch_alpha = character alpha, ch_off = character offset from the
-- centre, ta = intro ("N songs") text alpha.
local function folder_sample(f)
    local sy = FOLDER_CLIP:get("instance_board_center", f, "sy") or SY_CLOSED
    local pb = clamp01((sy - SY_CLOSED) / (SY_OPEN - SY_CLOSED))
    local ch_alpha = FOLDER_CLIP:get("instance_character_l", f, "a") or 0.0
    local ch_x     = FOLDER_CLIP:get("instance_character_l", f, "tx") or -340.0
    local ta       = FOLDER_CLIP:get("text_kanban_intro_genre", f, "a") or 0.0
    return pb, ch_alpha, -ch_x, ta
end

-- t = ms since the engine open started (p * 233).
local function folder_open_timeline(t)
    return folder_sample(F_SELECT_ON + t * 0.06)
end

local function folder_close_timeline(e)
    return folder_sample(F_SELECT_OFF + e * 0.06)
end

-- ROUND 51: same reader for the song board.  pb = board progress, ia = contents alpha
-- (song_kanban_info: diff plates / subtitle / BPM), ic = NEW-icon alpha (instance_icon).
local function song_sample(f)
    local sy = SONG_CLIP:get("instance_board_center", f, "sy") or SY_CLOSED
    local pb = clamp01((sy - SY_CLOSED) / (SY_OPEN - SY_CLOSED))
    local ia = SONG_CLIP:get("song_kanban_info", f, "a") or (pb >= 1.0 and 1.0 or 0.0)
    local ic = SONG_CLIP:get("instance_icon", f, "a") or (1.0 - pb)
    return pb, clamp01(ia), clamp01(ic)
end

local function song_open_timeline(t)  return song_sample(S_SELECT_ON  + t * 0.06) end
local function song_close_timeline(e) return song_sample(S_SELECT_OFF + e * 0.06) end



-- crown / rank badges (72x72 art) at arcade positions. closed: (±425, -27)/(+27) at 0.75;
-- open: (±425, -114)/(-42) at 1.0. p interpolates.
-- ROUND 56 (r56): rebuilt on the arcade rule (song_select_song_disp_data.lua SetCrown :311
-- + SearchAnyCrown :521, R49 audit row closed).  ONE badge column PER PLAYER — crown_1
-- hangs at -425, crown_2 at +425 — and each player's crown shows their CURRENTLY
-- SELECTED course (cardPlayerSelectedCourse -> the last diff-select column this skin
-- already caches in _mark_diff, from player:selected_difficulty()), falling back to the
-- ascending any-crown walk where the LAST hit wins (= highest difficulty with a crown).
-- The rank chip follows the same per-player choice (this engine has one local score DB,
-- so both players read the same records — the seat split is still the cabinet's layout).
-- ROUND 56 (r56): NextSongIcon rotation (song_select_song_disp_data.lua NextSongIcon
-- :1107 + SearchNextIcon :1306; R49 audit row).  The cabinet rotates the board's
-- instance_icon once per second (wait_icon_change_cnt = 1*Common.FPS) through every
-- icon that applies to the song, in the fixed cycle dani -> new -> papamama ->
-- official -> compe -> challenge.  This engine's data model exposes only `is_new`
-- today — there is no song->dan membership at wheel time (chart hashes are computed
-- lazily on play, so a dan.json hash match would silently miss unplayed songs), no
-- papamama/演奏skip concept, and no compe/challenge — so the applicable list holds at
-- most one entry and the cycle is latent.  The machinery IS the cabinet's: any future
-- engine flag joins the rotation by appending its name + art here.
local ICON_ARTS = nil   -- name -> texture id (resolved lazily; only icon_new has art)
function SongSelect:song_icon(box)
    if ICON_ARTS == nil then ICON_ARTS = { icon_new = self._id_new_icon } end
    local list = {}
    -- icon_dani: no engine counterpart (see block comment)
    if box.is_new then list[#list + 1] = "icon_new" end
    -- icon_papamama / icon_official / icon_compe / icon_challenge: no engine counterpart
    if #list == 0 then return nil end
    local pick = list[(math.floor(tex.get_current_ms() / 1000.0) % #list) + 1]
    return ICON_ARTS[pick]
end

function SongSelect:badge_diffs(box, pl)
    local sel = self._mark_diff and self._mark_diff[pl]
    local crown_d, rank_d
    if sel and sel >= DIFF_EASY and sel <= DIFF_URA then
        local info = box:course_info(sel)
        if info.has_course then
            if info.crown ~= CROWN_NONE then crown_d = sel end
            if (info.rank or 0) > 0 then rank_d = sel end
        end
    end
    if not crown_d or not rank_d then
        local cbest, rbest   -- SearchAnyCrown: ascending walk, latest set wins
        for d = DIFF_EASY, DIFF_URA do
            local info = box:course_info(d)
            if info.has_course then
                if info.crown ~= CROWN_NONE then cbest = d end
                if (info.rank or 0) > 0 then rbest = d end
            end
        end
        crown_d = crown_d or cbest
        rank_d  = rank_d or rbest
    end
    return crown_d, rank_d
end

function SongSelect:draw_badges(box, bx, by, p, fade)
    local scale = 0.75 + 0.25 * p
    local size = 72 * scale
    local players = math.min(2, tex.player_num())
    for pl = 1, players do
        -- solo play keeps the single left badge the cabinet shows for 1P
        local cx = bx + ((pl == 1) and -425 or 425)
        local crown_d, rank_d = self:badge_diffs(box, pl)
        if crown_d then
            local info = box:course_info(crown_d)
            local id = (info.crown == CROWN_DFC) and self._id_crown_dfc
                    or (info.crown == CROWN_FC) and self._id_crown_fc or self._id_crown_clear
            local cy = by + (-27 + (-114 + 27) * p)
            draw_centered(id, cx, cy, size, size, { frame = crown_d, x2 = size - 72, y2 = size - 72, fade = fade })
        end
        if rank_d then
            local info = box:course_info(rank_d)
            local cy = by + (27 + (-42 - 27) * p)
            draw_centered(self._id_score_rank, cx, cy, size, size,
                { frame = (info.rank - 1) * 5 + rank_d, x2 = size - 72, y2 = size - 72, fade = fade })
        end
    end
end

-- ESE / taiko.wiki community clear-tier letter (SSS/SS/S+/…), from SongBox:tier().
-- Mirrors box_song.cpp's gold OutlinedText badge; the C++ draw is hidden because this
-- skin replaces the wheel boards in Lua. Guarded with pcall so older engines without
-- the binding just skip the badge.
-- No new main-chunk locals: this file is already at Lua's 200-local limit.
function SongSelect:tier_badge(box)
    local ok, letter = pcall(function() return box:tier() end)
    if not ok or not letter or letter == "" then return nil end
    local t = self._tier_badges[letter]
    if t == nil then
        if self._tier_badges_n >= 32 then
            self._tier_badges = {}
            self._tier_badges_n = 0
        end
        -- gold fill {255,230,120}, black edge — same as box_song.cpp tier_badge
        t = text.create_raw_text(letter, 26, { 255, 230, 120, 255 }, { 0, 0, 0, 255 },
                                 false, ol(3.0), 1.0)
        self._tier_badges[letter] = t or false
        self._tier_badges_n = self._tier_badges_n + 1
    end
    return t or nil
end

-- Oni/Ura chip centre x — same column math as draw_plates (pitch 182, centred).
function SongSelect:oni_plate_cx(box, bx)
    local cols = {}
    for d = DIFF_EASY, DIFF_HARD do
        if box:course_info(d).has_course then cols[#cols + 1] = d end
    end
    local has_oni = box:course_info(DIFF_ONI).has_course
    if has_oni or box:has_ura() then cols[#cols + 1] = DIFF_ONI end
    local n = #cols
    for i, d in ipairs(cols) do
        if d == DIFF_ONI then
            return bx + (i - 1 - (n - 1) / 2) * 182
        end
    end
    return nil
end

function SongSelect:draw_tier_badge(box, bx, by, pb, fade)
    local badge = self:tier_badge(box)
    if not badge or fade <= 0 then return end
    if pb <= 0.0 then
        -- closed board: sit just above the centred title (C++ uses title-x / by+8)
        draw_title(badge, bx, by - 36, fade)
    else
        -- open board: above the oni/ura difficulty plate (C++: ONI column, y=-24)
        local cx = self:oni_plate_cx(box, bx)
        if cx then
            draw_title(badge, cx, by + 75 - 62, fade)
        else
            draw_title(badge, bx, by - 70 * pb - 28, fade)
        end
    end
end


--------------------------------------------------------------------------------
-- SongBox
--------------------------------------------------------------------------------
-- ROUND 51 (r51-genreanim-chn-port): song boards now play the cabinet's own select_on /
-- select_off timelines (anim/song_board, sprite 222) on the same wall-clock + 508 ms
-- hold machinery as the folders, instead of the raw engine open_anim ramp (which started
-- 342 ms early and spread the crisp 50 ms plateau + 83 ms growth over a mushy 233 ms).
-- All states funnel through this one body:
--   pb = board progress (0 closed .. 1 open), from instance_board_center sy
--   ia = contents alpha (diff plates / subtitle / BPM / favorite), from song_kanban_info
--   ic = NEW-icon alpha, from instance_icon (fades out f5..f10, back in f38..f43)
function SongSelect:draw_song_board(box, pb, ia, ic)
    local bx, by = box:box_x(), box:box_y()
    local fdx, fdy = fly_offset(box); bx, by = bx + fdx, by + fdy   -- r56 wheel fly-in
    local fade = math.min(1.0, box_fade(box))

    -- ROUND 61: the folder-ENTER unfurl.  On the frames the cabinet is playing
    -- genre_deceide f113..f145 the incoming song board is NOT growing vertically from
    -- the closed slab -- it is already at full height and unfurls HORIZONTALLY from a
    -- 2.5%-wide hairline (anim/genre_open song_board_sx).  Contents are alpha 0 for the
    -- whole sweep, so this returns before any of them are drawn; the title picks up on
    -- song_title_a (f132..f145) once the slab is at rest.
    -- Strictly additive: this branch OWNS the board only while the sweep is actually in
    -- progress (clip f113..f127, sx < 1).  From f128 the slab is at rest and every later
    -- frame falls through to the normal body untouched, so the leg cannot regress the
    -- glow / characters / icon / plates the rest of this function draws.  (First cut also
    -- took over the f128..f145 contents fade-in and dropped exactly those -- caught by the
    -- YATAIDON_R61_DISABLE A/B at t=1000 ms, scratchpad/r61/t1000_{on,off}.png.)
    -- ROUND 85: the same block now serves BOTH legs.  Open: f113 placed at full
    -- height sx 0.025, alpha 1 from f115, unfurls to sx 1 over f115..f128, contents
    -- fade in f133..f145.  Close (`return`, f150): contents fade out f188..f200, the
    -- slab FURLS back sx 1 -> 0.025 over f205..f217 and goes alpha 0 at f218.
    local gof = leg_f()
    local ltitle = 1.0
    if R61.dbg and gof then
        pcall(function()
            tex.log(string.format("[r85] f=%.1f kind=%s swapped=%s state=%s box=%s", gof,
                                  tostring(R61.kind), tostring(R61.swapped),
                                  tostring(box:draw_state()), tostring(box.text_name)))
        end)
    end
    if gof and R61.kind == "open" and gof < R61.open_swap - 1 and box:draw_state() == "open" then
        return          -- the new content's board does not exist until the swap frame
    end
    if gof and box:draw_state() == "open" then
        local sx, ba = go_get("song_board_sx", gof, 1), go_get("song_board_sx", gof, 2)
        if sx and ba then
            if ba <= 0.0 then return end          -- board not on screen on this frame
            if sx < 1.0 then                      -- unfurl / furl: the slab alone
                if R61.dbg then
                    pcall(tex.log, string.format(
                        "[r85s] ms=%.1f f=%.2f kind=%s sx=%.4f a=%.3f w=%.1f",
                        tex.get_current_ms(), gof, tostring(R61.kind), sx, ba,
                        BOARD_W * sx))
                end
                draw_board(self._id_bar_genre, box:genre_frame(), bx, by, CEN_OPEN_H,
                           fade * ba, nil, sx)
                return
            end
            -- slab at rest: the contents ride the clip's own alphas rather than the
            -- select_on curve, so the fade-in after the unfurl (f133..f145) and the
            -- fade-out before the furl (f188..f200) are the cabinet's.
            local ia2 = go_get("song_info_a", gof, 1)
            if ia2 then ia = math.min(ia or 1.0, ia2) end
            ltitle = go_get("song_title_a", gof, 1) or 1.0
            local ic2 = go_get("song_icon_a", gof, 1)
            if ic2 then ic = math.min(ic or 0.0, ic2) end
        end
    end

    local cen_h = CEN_CLOSED_H + (CEN_OPEN_H - CEN_CLOSED_H) * pb
    if pb > 0 then self:draw_glow(bx, by, cen_h, fade * pb * glow_pulse()) end
    draw_board(self._id_bar_genre, box:genre_frame(), bx, by, cen_h, fade)

    if pb <= 0.0 then
        draw_title(self:song_title(box), bx, by, fade, SONG_TEXT_BOX_W, 0)
        -- r56: per-player crown/rank (SetCrown / SearchAnyCrown, see draw_badges)
        self:draw_badges(box, bx, by, 0.0, fade)
        self:draw_tier_badge(box, bx, by, 0.0, fade)
        -- instance_icon: the arcade hangs it off the board's left edge (-532); which
        -- icon shows rotates once per second (r56 song_icon = NextSongIcon)
        local icon = (ic > 0) and self:song_icon(box) or nil
        if icon then
            draw_centered(icon, bx - 532, by + 4, 120, 96, { fade = fade * ic })
        end
        return
    end

    -- growing / open / closing board.  Contents follow the cabinet's song_kanban_info
    -- alpha (in f13..f18 AFTER the growth, out f30..f35 BEFORE the shrink), not the old
    -- p >= 0.66 gate on the engine ramp.
    local cfade = fade * ia
    if cfade > 0 then
        self:draw_plates(box, bx, by, cfade)
        -- r56: badges are per player + selected course now (SetCrown), so the old
        -- oni/ura crossfade pair is one call — the ura toggle switches the player's
        -- selected course, which is what the cabinet's GotoAndStop follows.
        self:draw_badges(box, bx, by, pb, cfade)
        self:draw_tier_badge(box, bx, by, pb, cfade)
    end

    -- title rises from the closed position (y=0) to the open one (y=-70) with the growth
    draw_title(self:song_title(box), bx, by - 70 * pb, fade * ltitle, SONG_TEXT_BOX_W, 0)
    local sub = self:song_subtitle(box)
    if sub and cfade > 0 then draw_title(sub, bx, by - 28, cfade, SONG_TEXT_BOX_W) end   -- user-tuned: -28 (arcade text_sub box y -53..+9 under the title box)

    if tex.display_bpm() and cfade > 0 then
        local bpm_cfg = tex.get_skin_config("song_box_bpm")
        box:bpm_text():draw({ x = bx + bpm_cfg.x, y = by + bpm_cfg.y, fade = cfade })
    end
    local icon = (ic > 0) and self:song_icon(box) or nil   -- board label 'on' hides instance_icon (f5..f10 fade)
    if icon then
        draw_centered(icon, bx - 532, by + 4, 120, 96, { fade = fade * ic })
    end
    -- ROUND 63 (r63-duplicate-song-box): the 新音 / 旧音 (and 期間限定) marker.
    -- TJAParser STRIPS the "-New Audio-" / "-Old Audio-" suffix out of TITLE and
    -- raises ex_data.new_audio / ex_data.old_audio instead (tja.cpp:460), and the
    -- engine's own C++ board draws the badge that carries that information
    -- (box_song.cpp:430).  This skin's Lua board replaced that draw and never
    -- restored it, so two charts of one song were left with identical titles AND
    -- no marker -- the 「還是只是歌名相同?」 half of the user's report.  Art is the
    -- parent skin's 224x64 pill, inherited at 1.5x like every other yellow_box
    -- badge this skin does not override (MAPPING, yellow_box table).  Drawn BEFORE
    -- the favorite pill so the precedence matches box_song.cpp's else-if chain.
    local ok_exd, exd = pcall(function() return box:ex_data_flag() end)
    if ok_exd and exd and exd > 0 and cfade > 0 then
        local art = (exd == 1) and get_any("yellow_box", "ex_data_new_audio")
                 or (exd == 2) and get_any("yellow_box", "ex_data_old_audio")
                 or get_any("yellow_box", "ex_data_limited_time")
        -- Hung off the board's RIGHT edge at (bx+532, by+4), the mirror of the
        -- instance_icon slot this file already uses at (bx-532, by+4).  The
        -- engine's own slot (the favorite pill's, texture.json 458,120) lands the
        -- pill straight on top of the Ura difficulty chip on this skin's 960 px
        -- board -- verified, scratchpad/r63/fix_d_back1.png.  These parent-skin
        -- textures carry a texture.json base that the engine ADDS and that the
        -- parent loader already scaled by screen_scale, so it is subtracted back
        -- out here (the draw_centered_b contract).
        if art then
            local s = tex.get_screen_scale()
            draw_centered_b(art, { 458 * s, 120 * s }, bx + 532, by + 4,
                            224 * s, 64 * s, { fade = cfade })
        end
    end
    -- Favorites: small star to the right of the diff-plate row (not the big 1P/2P pill,
    -- which covers Oni/Ura on this 960px board).
    if box.is_favorite and cfade > 0 and self._id_star then
        local sz = 22
        local gap = 8
        local cx = bx + BOARD_W / 2 + sz / 2 + gap
        local cy = by + 75
        draw_centered(self._id_star, cx, cy, sz, sz, { frame = 3, fade = cfade })
    end
end

function SongSelect:draw_song_closed(box)
    local key = "s\0" .. box_id(box)   -- r63: per-BOX, not per-title (see box_id)
    local e = wheel_close_step(key, SONG_CLOSE_MS, SONG_STALE_MS)
    if e then
        self:draw_song_board(box, song_close_timeline(e))
        return
    end
    self:draw_song_board(box, 0.0, 0.0, 1.0)
end

-- Diff plates row (song_kanban_info, board y +75): plate 184x96 = icon (left column, centre -42) with the
-- course name (text_course_*, font 18) under it, ★ at +27 and the level number at +61 on the dark block.
-- Arcade labels: 'default' = easy/normal/hard at -182/0/+182, 'oni'/'ura' = 4 chips at -273/-91/+91/+273
-- (pitch 182, centred on the board); songs with fewer courses keep the same pitch, centred.
function SongSelect:chip_label(diff)
    local lbl = self._chip_labels[diff]
    if lbl == nil then
        local key = "chip_course_name_" .. diff
        if tex.get_skin_config(key) then
            -- `main_song_board` `text_course_easy/normal/hard/oni/ura`: size 18, border 1.5
            lbl = text.create_text(key, { 255, 255, 255, 255 }, { 40, 20, 20, 255 }, false, ol(1.5), 1.0)
        end
        self._chip_labels[diff] = lbl or false
    end
    return lbl or nil
end

function SongSelect:draw_plates(box, bx, by, fade)
    local has_ura = box:has_ura()
    local ura_weight = 0.0
    -- ROUND 8: arcade song_kanban_info (`common_song_select_main_song_board.nulm` MC 78) label
    -- 'ura' = f10..f189, a 180-frame (3 s) loop cross-fading the oni chip (depth 0) and the ura
    -- chip (depth 1, on top).  Sampled from `anim/song_board_ura` -- tracks
    -- `oni/text_course_oni` and `ura/text_course_ura`.
    -- What the export changed: round 5 read the four 15-frame ramps as starting at 1.00 / 1.25 /
    -- 2.50 / 2.75 s.  The cabinet starts each of them ONE FRAME EARLIER (ura up f69..f84, oni
    -- down f84..f99, oni up f159..f174, ura down f174..f189 absolute), so every ramp ran 16.7 ms
    -- late and the ura chip was still a sliver visible on the frame the loop wraps.
    local oni_alpha = 1.0
    if has_ura then
        local f = URA_LOOP_F0 + (tex.get_current_ms() * 0.06) % URA_LOOP_LEN
        ura_weight = URA_LOOP:get("ura/text_course_ura", f, "a") or 0.0
        oni_alpha  = URA_LOOP:get("oni/text_course_oni", f, "a") or 1.0
    end
    local py = by + 75
    local function plate(cx, diff, f)
        local info = box:course_info(diff)
        if not info.has_course or f <= 0 then return end
        draw_centered(self._id_plate, cx, py, 184, 96, { frame = diff, fade = f })
        draw_centered(self._id_star, cx + 27, py, 40, 40, { frame = diff, fade = f })
        local lv = math.max(1, math.min(11, info.level))
        draw_centered(self._id_level_number, cx + 61, py, 48, 48, { frame = diff * 11 + (lv - 1), fade = f })
        local name = self:chip_label(diff)
        if name then draw_title(name, cx - 42, py + 30, f) end
        -- ROUND 8: the branch chip does NOT blink on the cabinet.  `branch` is MC 36 of
        -- common_song_select_main_song_board (and MC 10 of main_diff_board), a two-label
        -- selector (`off` f0 / `on` f5) with a stop() on frame 0 -- `--auto-stop` on the
        -- dumper shows it parked, and neither half animates.  The 1 Hz blink round 5 wrote
        -- here had no arcade source at all; drawn steady now.
        if info.is_branching then
            draw_centered(self._id_branch, cx - 69, py - 23, 40, 40, { fade = f })
        end
    end
    -- charted columns: easy / normal / hard / (oni|ura)
    local cols = {}
    for d = 0, 2 do if box:course_info(d).has_course then cols[#cols + 1] = d end end
    local has_oni = box:course_info(DIFF_ONI).has_course
    if has_oni or has_ura then cols[#cols + 1] = DIFF_ONI end
    local n = #cols
    for i, d in ipairs(cols) do
        local cx = bx + (i - 1 - (n - 1) / 2) * 182
        if d == DIFF_ONI and has_ura then
            if has_oni then
                plate(cx, DIFF_ONI, fade * oni_alpha)      -- oni below, ura on top (lumen depth order)
                plate(cx, DIFF_URA, fade * ura_weight)
            else
                plate(cx, DIFF_URA, fade)
            end
        else
            plate(cx, d, fade)
        end
    end
    return ura_weight
end

function SongSelect:draw_song_open(box)
    local key = "s\0" .. box_id(box)   -- r63: per-BOX, not per-title (see box_id)
    local t = wheel_open_t(key)
    if t == nil then     -- cabinet 508 ms pre-growth hold: fully closed
        self:draw_song_board(box, 0.0, 0.0, 1.0)
        return
    end
    self:draw_song_board(box, song_open_timeline(t))
end

--------------------------------------------------------------------------------
-- Difficulty select (common_song_select_main_diff layout, stage root 960,540, board at y-132)
--   backboard 1272x784 centred (960,440); return (440,462) option (572,462) [neiro (440,602) added];
--   diff boards 200x360 at x = 745,960,1175,1390  y = 583; cursor bubble y = 370, x = slot -/+ 40
--------------------------------------------------------------------------------
local DS_SLOT_X = { [DIFF_BACK] = 440, [DIFF_MODIFIER] = 572, [DIFF_NEIRO] = 440,
                    [DIFF_EASY] = 745, [DIFF_NORMAL] = 960, [DIFF_HARD] = 1175, [DIFF_ONI] = 1390 }
local DS_BOARD_Y = 583
local DS_COURSE_NAME_DY = 49   -- course-name centre below the card origin (ROUND 5c, user-measured arcade +48..50)
local URA_ANIM_MS   = 1500   -- oni_main change_ura/change_oni: 90 frames @ 60 fps
local URA_SWAP_MS   = 450    -- board_oni -> board_ura swap at arcade frame 27 of the change
local URA_CELLS     = 90     -- ROUND 4d: every arcade frame (60 cells/s)
local DS_BTN_Y   = 462
local DS_CURSOR_Y = 370   -- player_cursor_1/2 row (same for both): board 442-72 = 426-56 = 370
local DS_NEIRO_Y = 602

-- main_diff: instance_mark_1p / _2p at board (+-760, 0) -> stage (200,408) / (1720,408), 680x680, label per
-- the player's cursor column (course_1..5; 'none' on the buttons). Drawn under the buttons/cards.
-- ROUND 16 (r16-select, QA defect #5): the mark is drawn BEFORE the backboard, not after.
-- Re-measured from the cabinet with `lumen_tools/inst.py common_songselect/common_song_select_main_diff 0 2`:
-- `common_song_select_main_diff` sprite 46 (`board`) frame 0 places
--     d0 instance_mark_2p (1720,408)   d1 instance_mark_1p (200,408)
--     ...  d6 instance_backboard (960,440)  d7/d10 course_frame_N ... d14-17 instance_board_N
-- i.e. both marks sit at the BOTTOM of the board's display list, under `instance_backboard`.
-- The 680x680 mark spans x -140..540 / 1380..2060, so on the cabinet only the part OUTSIDE the
-- 1272x784 backboard (x 324..1596) is visible -- the plum/bamboo peeks out past the board's edge
-- and is hidden behind it. We drew it after the backboard, so its 154/256 alpha bled across the
-- board's left/right edge (QA `shots/select_c1/z_plum.png`, `select_c2/c2_03_p2_moved.png`).
local DS_MARK_X = { [1] = 200, [2] = 1720 }
local DS_MARK_Y = 408
-- main_diff_mark 'course_decide_start' (frames 35..64, 60 fps), one row per frame:
-- { mark alpha, mark y-scale, white alpha, white y-scale, white backing alpha }.  The
-- last row is the 'course_decide' hold: opaque mark on its white backing.
local DS_MARK_DECIDE = {
    { 0.5, 1, 0, 1, 0 },
    { 0.5938, 1.0447, 0.1914, 1.0447, 0.1016 },
    { 0.6797, 1.0847, 0.3594, 1.0847, 0.1992 },
    { 0.7539, 1.12, 0.5117, 1.12, 0.3008 },
    { 0.8203, 1.1506, 0.6406, 1.1506, 0.3984 },
    { 0.875, 1.1765, 0.75, 1.1765, 0.5 },
    { 0.9219, 1.1976, 0.8398, 1.1976, 0.6016 },
    { 0.9531, 1.2141, 0.9102, 1.2141, 0.6992 },
    { 0.9805, 1.2259, 0.9609, 1.2259, 0.8008 },
    { 0.9961, 1.2329, 0.9883, 1.2329, 0.8984 },
    { 1, 1.2353, 1, 1.2353, 1 },
    { 1, 1.2229, 0.9961, 1.2346, 1 },
    { 1, 1.2105, 0.9883, 1.2327, 1 },
    { 1, 1.1981, 0.9766, 1.2294, 1 },
    { 1, 1.1858, 0.957, 1.2248, 1 },
    { 1, 1.1733, 0.9297, 1.219, 1 },
    { 1, 1.161, 0.8984, 1.2118, 1 },
    { 1, 1.1486, 0.8633, 1.2033, 1 },
    { 1, 1.1362, 0.8242, 1.1936, 1 },
    { 1, 1.1238, 0.7773, 1.1825, 1 },
    { 1, 1.1114, 0.7227, 1.1701, 1 },
    { 1, 1.0991, 0.6641, 1.1564, 1 },
    { 1, 1.0867, 0.6016, 1.1414, 1 },
    { 1, 1.0743, 0.5312, 1.1251, 1 },
    { 1, 1.0619, 0.457, 1.1075, 1 },
    { 1, 1.0495, 0.375, 1.0886, 1 },
    { 1, 1.0371, 0.2891, 1.0684, 1 },
    { 1, 1.0247, 0.1992, 1.0469, 1 },
    { 1, 1.0124, 0.1016, 1.0241, 1 },
    { 1, 1, 0, 1, 1 },
}
local DS_MARK_DECIDE_MS = 1000 / 60

function SongSelect:draw_diff_mark_decided(p, dec, fade)
    local f = math.floor((tex.get_current_ms() - dec.t0) / DS_MARK_DECIDE_MS) + 1
    local k = DS_MARK_DECIDE[math.min(f, #DS_MARK_DECIDE)]
    local id, id_w = self._id_mark[dec.course], self._id_mark_w[dec.course]
    local cx, cy = DS_MARK_X[p], DS_MARK_Y
    local function stretched(tid, a, sy)
        if a <= 0 then return end
        tex.draw_texture(tid, { x = cx, y = cy, y2 = 680 * (sy - 1), origin = { 340, 340 * sy }, fade = a * fade })
    end
    stretched(id_w, k[5], 1)      -- mark_course_white_2: backing that makes the mark solid
    stretched(id,   k[1], k[2])   -- mark_course
    stretched(id_w, k[3], k[4])   -- mark_course_white: the flash on top
end

function SongSelect:draw_song_diff_select_bg(box)
    local fade = box.diff_fade_in.attribute
    for p = 1, 2 do
        local d = self._mark_diff[p]
        local dec = self._mark_decided[p]
        if dec and self._id_mark[dec.course] then
            self:draw_diff_mark_decided(p, dec, fade)
        elseif self._id_diff_mark and d and d >= DIFF_EASY and DS_MARK_X[p] then
            draw_centered(self._id_diff_mark, DS_MARK_X[p], DS_MARK_Y, 680, 680, { frame = d, fade = fade })
        end
    end
    tex.draw_texture(self._id_backboard, { frame = box:genre_frame(), x = 960 - 636, y = 440 - 392, fade = fade })
end

function SongSelect:draw_song_diff_select(box)
    -- ROUND 51: course-back detection.  The cabinet's ReLoading state (CourseBackFlag)
    -- replays select_on IMMEDIATELY (no 508 ms hold) on the focused boards when the
    -- player backs out of course select - a fresh open that starts within 300 ms of the
    -- last diff_select draw takes mode "nohold" (see open_start_mode).
    _diff_seen_ms = tex.get_current_ms()
    local fade = box.diff_fade_in.attribute
    local is_ura = box.is_ura
    local side = (tex.player_num() == 2) and 1 or -1
    local now = tex.get_current_ms()

    -- oni<->ura toggle detection (player.cpp toggle_ura_mode flips box.is_ura at once and plays
    -- song_select/ura_switch = arcade appear_ura; the arcade TurnOniPanel plays oni_main change_ura/
    -- change_oni for 90 frames, hides crown/rank and the toggling player's cursor meanwhile).
    if fade < 1.0 then
        self._ura_prev = is_ura; self._ura_anim = nil
    elseif self._ura_prev ~= nil and is_ura ~= self._ura_prev then
        self._ura_anim = { t0 = now, to_ura = is_ura }
    end
    self._ura_prev = is_ura
    local ura_elapsed = self._ura_anim and (now - self._ura_anim.t0) or nil
    if ura_elapsed and ura_elapsed >= URA_ANIM_MS then self._ura_anim = nil; ura_elapsed = nil end
    local ura_mid = (not ura_elapsed) or ura_elapsed >= URA_SWAP_MS
    -- what the oni slot shows: before the swap point the previous course, after it the new one
    local oni_shown = is_ura
    if ura_elapsed and not ura_mid then oni_shown = not is_ura end

    -- buttons: arcade shows only return (440,462); the engine's MODIFIER slot stays reachable
    -- (player.cpp move_left: first course -> [NEIRO ->] MODIFIER -> BACK), so the wrench is kept but
    -- small; the NEIRO slot is a faint frame (no label) only while it is still a separate panel.
    draw_centered(self._id_btn_back,   DS_SLOT_X[DIFF_BACK],     DS_BTN_Y,   128, 128, { fade = fade })
    -- ROUND 4d (user): option = same 128 px plain icon as return, full opacity (main_diff_other_board
    -- 'option' / 'return' are the same-size boards; the cursor frame/glow geometry is shared in draw_selector)
    draw_centered(self._id_btn_option, DS_SLOT_X[DIFF_MODIFIER], DS_BTN_Y,   128, 128, { fade = fade })
    -- (The faint 80 px NEIRO placeholder frame that used to sit under the return button
    -- is gone: the cabinet's course-select board has no such element.)

    -- diff boards
    for i = 0, 3 do
        local cx = DS_SLOT_X[i]
        local diff = (i == DIFF_ONI and oni_shown) and DIFF_URA or i
        local info = box:course_info(diff)
        local f = info.has_course and fade or math.min(fade, 0.4)
        draw_centered(self._id_diff_board, cx, DS_BOARD_Y, 200, 360, { frame = diff, fade = f })
        if info.has_course and not (i == DIFF_ONI and ura_elapsed) then
            -- crown / rank badges (arcade: crown at ±58, rank at ±20, y -138)
            local crown = info.crown or 0
            draw_centered(self._id_small_crown, cx + side * 58, DS_BOARD_Y - 138, 40, 40, { frame = crown, fade = f })
            if (info.rank or 0) > 0 then
                draw_centered(self._id_small_rank, cx + side * 20, DS_BOARD_Y - 138, 40, 40, { frame = info.rank - 1, fade = f })
            end
            -- level (ROUND 4d): difficulty_star.png = 12 cells 56x40: frame 0 = main_diff_board shape42 ★
            -- (40x40, pure white) at cell x 0..40; frames 1..11 = the board's own diff_1..diff_11 glyphs
            -- (pure white, no outline; local (0,-16)..(40|56,16)) at cell x 0, y 4. Lumen: ★ centred
            -- (-20,+86), glyph origin (-4,+88) -> cells at (cx-40, y+66) and (cx-4, y+68). Dotted bar y+116.
            local lv = math.max(1, math.min(11, info.level))
            tex.draw_texture(self._id_small_star, { frame = 0,  x = cx - 40, y = DS_BOARD_Y + 66, fade = f })
            tex.draw_texture(self._id_small_star, { frame = lv, x = cx - 4,  y = DS_BOARD_Y + 68, fade = f })
            draw_centered(self._id_dot_bar, cx, DS_BOARD_Y + 116, 172, 24, { fade = f })
            for k = 0, math.min(10, info.level) - 1 do
                -- ROUND 11: the centre was cx-67.5, i.e. the 24x24 cell landed on a HALF pixel
                -- (x = cx-79.5). Textures load with mipmaps + TRILINEAR (texture.h), so a .5
                -- offset costs a full 50/50 texel blend - this is why the dotted star bar read
                -- softer than the cabinet's. Snapped to whole px (0.5 px shift, arcade-invisible).
                draw_centered(self._id_dot_star, cx - 67 + k * 15, DS_BOARD_Y + 116, 24, 24, { fade = f })
            end
            if info.is_branching then   -- ROUND 8: steady, not blinking (see draw_plates)
                draw_centered(self._id_branch_diff, cx, DS_BOARD_Y + 144, 40, 40, { fade = f })
            end
        end
    end

    -- course names on the cards (main_diff_board text_course_title EditText: ＭＳ 明朝 34, box 160x80 at
    -- (-78,-3)). ROUND 5c (user crops, arcade vs ours): the arcade name ink is ~6-7 % narrower than our
    -- font-30 label and its centre sits at card y+48..50 (ours was +35) -> skin_config course_name_N
    -- font 28, centred at DS_BOARD_Y + DS_COURSE_NAME_DY (49). (★/level unchanged.)
    -- change animation cells over the oni slot (the cells carry the board art at 'none', so the card
    -- under it is covered exactly like the arcade board MC during the change)
    if ura_elapsed then
        local id = self._ura_anim.to_ura and self._id_ura_to_ura or self._id_ura_to_oni
        if id then
            local cell = math.min(URA_CELLS - 1, math.floor(ura_elapsed / URA_ANIM_MS * URA_CELLS))
            tex.draw_texture(id, { frame = cell, x = DS_SLOT_X[DIFF_ONI] - 170, y = DS_BOARD_Y - 210, fade = fade })
        end
    end
    for i = 0, 3 do
        local diff = (i == DIFF_ONI and oni_shown) and DIFF_URA or i
        local info = box:course_info(diff)
        if info.has_course then
            local lbl = self:course_label(diff)
            if lbl then draw_title(lbl, DS_SLOT_X[i], DS_BOARD_Y + DS_COURSE_NAME_DY, fade) end
        end
    end

    -- title / subtitle inside the backboard header (main_diff text_title_song: ＭＳ 明朝 54, box 1000x60
    -- at stage y 148..208 -> centre 178; text_song_title_sub: 32, box 1000x40 at y 222..262 -> centre 242),
    -- both centred on x=960. ROUND 4d (user): BLACK outline + arcade size for the title -> Lua-made
    -- raw text (box.text_name, font 48, outline 4) instead of the C++ name_large (song_box_name x1.5 = 63,
    -- outline = genre fore_color). ROUND 5c: the subtitle also gets a BLACK outline (arcade
    -- text_song_title_sub = plain black-edged white text, 32) -> `box.text_subtitle` (new SongBox binding
    -- in box_lua_bindings.cpp) rendered here at font 30 / outline 3; the C++ `subtitle_large`
    -- (fore_color outline) is only the fallback for an engine without the binding.
    draw_title(self:header_title(box), 960, 178, fade, DS_TEXT_BOX_W, 0)
    draw_title(self:header_subtitle(box), 960, 242, fade, DS_TEXT_BOX_W, 0)

end

function SongSelect:song_subtitle(box)
    local str = box.text_subtitle
    if not str or str == "" then return nil end
    -- ROUND 6 (board text colours): main_song_board sprite 92 `text_song_title_sub` carries the SAME
    -- per-genre colour add as the title, so the subtitle's edge is the genre colour too, not black.
    -- ROUND 9: `_inside_frame` (from Navigator:current_folder()) wins inside an event folder.
    local gf  = self._inside_frame or box:genre_frame()
    local key = gf .. "\0" .. str
    if self._sub_key ~= key then
        self._sub_key = key
        -- `main_song_board` id 85 `text_song_title_sub`: size 24, border 3.5
        self._sub_label = text.create_raw_text(str, 24, { 255, 255, 255, 255 }, board_outline(gf), false, ol(3.5), 2.0)
    end
    return self._sub_label
end

-- ROUND 6 (item 15): the song board title, arcade-exact (see SONG_TITLE_OUTLINE above).
-- Font size comes from skin_config `song_box_name` (42 = lumen id 82) so the size stays skinnable,
-- but WITHOUT the engine's >= 30-char shrink; outline is BLACK, not the genre fore_color.
-- Cached per title string (the engine caches one OutlinedText per box, so this is the same order
-- of cost); the cache is dropped wholesale past SONG_TITLE_CACHE_MAX so a long scroll cannot grow
-- it without bound.
function SongSelect:song_title(box)
    local name = box.text_name
    if not name or name == "" then return box:name() end
    local gf  = self._inside_frame or box:genre_frame()
    local key = gf .. "\0" .. name                 -- the outline follows the genre, so it keys the cache
    local t = self._song_titles[key]
    if t == nil then
        if SONG_TITLE_SIZE == nil then
            local cfg = tex.get_skin_config("song_box_name")
            SONG_TITLE_SIZE = (cfg and cfg.font_size and cfg.font_size > 0) and cfg.font_size or 42
        end
        if self._song_titles_n >= SONG_TITLE_CACHE_MAX then
            self._song_titles = {}
            self._song_titles_n = 0
        end
        t = text.create_raw_text(name, SONG_TITLE_SIZE, { 255, 255, 255, 255 }, board_outline(gf),
                                 false, ol(SONG_TITLE_OUTLINE), 2.0)
        self._song_titles[key] = t or false
        self._song_titles_n = self._song_titles_n + 1
    end
    return t or box:name()
end

function SongSelect:header_title(box)
    local name = box.text_name or ""
    if self._header_title_key ~= name then
        self._header_title_key = name
        -- `common_song_select_main_diff` id 29 `text_title_song`: border 7.0 (its size is 54; we draw 48)
        self._header_title = text.create_raw_text(name, 48, { 255, 255, 255, 255 }, { 0, 0, 0, 255 }, false, ol(7.0), 2.0)
    end
    return self._header_title
end

function SongSelect:header_subtitle(box)
    local sub = box.text_subtitle
    if sub == nil then return box:subtitle_large() end   -- engine without the binding
    if sub == "" then return nil end
    if self._header_sub_key ~= sub then
        self._header_sub_key = sub
        -- `common_song_select_main_diff` id 27 `text_song_title_sub`: border 4.0 (its size is 32; we draw 30)
        self._header_sub = text.create_raw_text(sub, 30, { 255, 255, 255, 255 }, { 0, 0, 0, 255 }, false, ol(4.0), 2.0)
    end
    return self._header_sub
end

function SongSelect:course_label(diff)
    local lbl = self._course_labels[diff]
    if lbl == nil then
        local key = "course_name_" .. diff
        if tex.get_skin_config(key) then
            -- `main_diff_board` id 13 `text_course_title`: size 34, border 4.5
            lbl = text.create_text(key, { 255, 255, 255, 255 }, { 20, 20, 20, 255 }, false, ol(4.5), 1.0)
        end
        self._course_labels[diff] = lbl or false
    end
    return lbl or nil
end

function SongSelect:guide_labels()
    if not self._guide_labels then
        local function mk(key)
            if tex.get_skin_config(key) then
                return text.create_text(key, { 40, 40, 40, 255 }, { 0, 0, 0, 0 }, false, 0, 2.0)
            end
        end
        self._guide_labels = { select = mk("guide_select_label"), decide = mk("guide_decide_label") }
    end
    return self._guide_labels
end

-- Cursor bubble (1P red / 2P teal with the P icon) + yellow course frame / button glow.
-- ROUND 15 — TWO PASSES, because the cabinet splits this element across the boards.
-- `common_song_select_main_diff` sprite 46 depth order, measured:
--     d6  instance_backboard
--     d7  course_frame_1        <- the yellow selection frame ...
--     d10 course_frame_2
--     d12 instance_return_board / d13 instance_option_board
--     d14..d17 instance_board_1..4   ... the difficulty cards sit ON TOP of the frame
--     d21 player_cursor_1 / d22 player_cursor_2   <- the 1P/2P bubble is above everything
-- So the frame/button glow belongs UNDER the wheel and the bubble ON TOP of it. The engine
-- calls this twice per frame: `pass` 0 before Navigator::draw(), 1 after it. An engine that
-- calls it once (pass = nil) gets both, which is the pre-round-15 behaviour.
function SongSelect:draw_selector(player, is_half, fade_in, pass)
    local under = (pass == nil) or (pass == 0)
    local over  = (pass == nil) or (pass == 1)
    local diff = player:selected_difficulty()
    local p = player:player_num()
    if under then
        self._mark_diff[p] = diff   -- main_diff_mark course for draw_song_diff_select_bg (next frame)
        -- course locked in (engine binding, PR #162): start the decide pop once; cleared again
        -- when the engine resets the selection (course back / song change)
        local decided = diff >= DIFF_EASY and player.difficulty_decided and player:difficulty_decided()
        if decided then
            if not self._mark_decided[p] then
                self._mark_decided[p] = { t0 = tex.get_current_ms(), course = diff }
            end
        else
            self._mark_decided[p] = nil
        end
    end
    if fade_in < 1.0 then return end
    local mod_on = player:modifier_active()
    -- ROUND 5: engine binding SongSelectPlayer:modifier_offset() (box_lua_bindings.cpp) = the exact y offset
    -- ModifierSelector::draw adds to the modifier/* textures (slide-in, hold AND slide-out); nil = no panel.
    -- Fallback for an exe without it: re-time the slide-in from the frame modifier_active() turned true.
    if player.modifier_offset then
        self._mod_off[p] = player:modifier_offset()
    elseif mod_on then
        if not self._mod_seen[p] then
            -- the C++ ctor starts the slide clock, then stalls that frame ~80 ms building its texts, so use
            -- the start of this frame (previous frame + 1/60 s) as the slide origin
            local now = tex.get_current_ms()
            local prev = self._last_sel_ms or now
            self._mod_t0[p] = (now - prev < 250) and math.min(now, prev + 16.7) or now
        end
        local u = math.max(0.0, math.min(1.0, (tex.get_current_ms() - self._mod_t0[p]) / MOD_SLIDE_MS))
        self._mod_off[p] = -MOD_SLIDE * u * (2 - u)
    else
        self._mod_off[p] = nil
    end
    self._last_sel_ms = tex.get_current_ms()
    self._mod_seen[p] = mod_on
    local fade = (player:neiro_active() or mod_on) and 0.5 or fade_in
    -- song_select_course_main.lua PlayerMoveSelect: player_cursor_N takes tbl_DuoCursorMoveLabel
    -- (pos_N, x = slot -+ 40) only while `player_num >= 2 and selectIndex[1] == selectIndex[2]`
    -- (exactly the engine's `is_half`); otherwise tbl_SoloCursorMoveLabel (pos_center_N, x = slot).
    local cursor_dx = is_half and ((p == 2) and 40 or -40) or 0
    local sx = DS_SLOT_X[(diff >= DIFF_EASY) and math.min(DIFF_ONI, diff) or diff]
    if not sx then return end

    if diff >= DIFF_EASY then
        local col = math.min(DIFF_ONI, diff)
        if col == DIFF_ONI and self._ura_anim then return end   -- TurnOniPanel: cursor/frame hidden during the change
        sx = DS_SLOT_X[col]
        local frame_id = (p == 1) and self._id_course_frame or self._id_course_frame2
        if under then draw_centered(frame_id, sx, DS_BOARD_Y, 248, 408, { fade = fade }) end
        if over then
            draw_centered_b((p == 1) and self._id_bubble_1p or self._id_bubble_2p, SEL_BASE_BALLOON,
                            sx + cursor_dx, DS_CURSOR_Y, 124, 124, { fade = fade })
        end
    else
        local by = (diff == DIFF_NEIRO) and DS_NEIRO_Y or DS_BTN_Y
        if under then draw_centered(self._id_btn_glow, sx, by, 168, 168, { fade = fade }) end
        if over then
            draw_centered_b((p == 1) and self._id_bubble_1p or self._id_bubble_2p, SEL_BASE_BALLOON,
                            sx + cursor_dx, DS_CURSOR_Y, 124, 124, { fade = fade })
        end
    end
end

--------------------------------------------------------------------------------
-- FolderBox (genre kanban: same 3-slice board, characters slide out from ±340 to ±450)
--------------------------------------------------------------------------------
-- folder board frame: arcade label per folder kind (event / sort_select / genre colour)
--
-- ROUND 9: engine bindings `BaseBox.collection` (the raw `#COLLECTION:` from
-- box.def) and `FolderBox:kind()` ("dan" / "sort" / "genre") replace the
-- display-name matching for everything the engine actually knows about
-- (LUA_CAPABILITIES item 26). The name tables stay as the fallback for the one
-- case the engine has no concept of - an *event* folder is not an engine idea,
-- there is no marker for it in box.def - and for skins/song trees that use
-- plain genre folders with those names.
--   SEARCH / DIFFICULTY -> the red sort_select board
--   NEW / RECOMMENDED   -> the event board (arcade: 「ドドンと！新曲祭り！」/「おすすめ曲」)
--   RECENT / FAVORITE   -> deliberately NOT remapped: the arcade gives them
--                          their own adds, and frame 0 is the closest we ship.
local SORT_COLLECTIONS  = { SEARCH = true, DIFFICULTY = true }
local EVENT_COLLECTIONS = { NEW = true, RECOMMENDED = true }

local function folder_frame(box)
    local n = box.text_name
    if n and EVENT_FOLDER_NAMES[n] then return FOLDER_FRAME_EVENT, true end
    local col = box.collection
    if col and col ~= "" then
        if EVENT_COLLECTIONS[col] then return FOLDER_FRAME_EVENT, true end
        if SORT_COLLECTIONS[col]  then return FOLDER_FRAME_SORT, false end
    end
    if box.kind and box:kind() == "dan" then return FOLDER_FRAME_DANI, false end
    if n and SORT_FOLDER_NAMES[n]  then return FOLDER_FRAME_SORT, false end
    return box:genre_frame(), false
end

-- ROUND 9 (MAPPING "ROUND 6 - board text colours", section 3): inside an EVENT
-- folder the arcade paints every song board with the `event` add (100,6,22),
-- not the song's own genre. That needed "which folder am I inside", which the
-- engine now exposes as `Navigator:current_folder()`. Refreshed once per frame
-- from draw_background (which is the only method that is handed the navigator)
-- and consumed by song_title / song_subtitle.
function SongSelect:refresh_folder_context(nav)
    self._inside_frame = nil
    if R61.dbg and not self._r61_ctx_logged then
        self._r61_ctx_logged = true
        pcall(function()
            tex.log("r61 ctx: nav=" .. tostring(nav ~= nil)
                    .. " has_current_folder=" .. tostring(nav and nav.current_folder ~= nil))
        end)
    end
    -- ROUND 85 -----------------------------------------------------------------
    -- The real state-machine transitions, straight from the engine.
    --
    -- ROUND 51 built the five-entry open behaviour on `current_folder()` change
    -- detection plus TTL'd one-shot flags and wrote, in its own trade-offs section,
    -- that these were "behavioural approximations of state-machine transitions the
    -- engine does not expose to Lua ... if the engine ever exposes real
    -- OpenFolder/CloseFolder events, route the modes off those instead".  ROUND 56
    -- and ROUND 61 both kept that detection point.  `Navigator` now emits the
    -- transitions themselves (Navigator::WheelEvent, navigator.h) and both the open
    -- modes AND the board/wheel animation clock hang off them.
    --
    -- A bumped counter rather than a queue: only the newest transition is read, which
    -- is correct here because the legs it starts are hundreds of ms long and two of
    -- them can never be one frame apart.  pcall + `have_events` keep a binary that
    -- predates this round on the ROUND 51 path instead of erroring (and the shipped
    -- exe HAS been out of date before -- R61 found it missing R56's tex.get_env,
    -- which had silently made a whole A/B meaningless).
    if nav then
        local ok, seq = pcall(function() return nav.wheel_event_seq end)
        if ok and seq ~= nil then
            R61.have_events = true
            if R61.seq == nil then
                R61.seq = seq                    -- first frame: adopt, do not replay
            elseif seq ~= R61.seq then
                R61.seq = seq
                local ok2, id = pcall(function() return nav.wheel_event end)
                if ok2 and id then leg_event(id) end
            end
        end
    end

    if not (nav and nav.current_folder) then return end
    local f = nav:current_folder()
    if not f then return end
    local ff, is_event = folder_frame(f)
    if is_event then self._inside_frame = ff end
end

-- instance_board_pattern: 960x120 strip centred on the board, y = 0 closed -> -94 open (follows the title)
function SongSelect:draw_pattern(bx, by, p, fade)
    if not self._id_pattern then return end
    tex.draw_texture(self._id_pattern, { x = bx - 480, y = by - 94 * p - 60, fade = fade })
end


-- ROUND 5b: the genre board's own title is bigger than a song board's. Arcade
-- CS/main_genre_board `text_kanban_title_genre` is an ＭＳ明朝 EditText at size 54
-- (unscaled in both the 'off' and 'on' board frames), while main_song_board's
-- `text_song_title` is 42 = the engine's song_box_name. The engine builds every
-- box title from song_box_name, so the folder title is re-made here at the size
-- skin_config `folder_box_name` asks for, keeping the engine's white fill. Falls back to
-- the engine text if the key is missing. ROUND 6: the outline is no longer the engine's
-- fore_color -- see `board_outline` / the ROUND 6 "board text colours" MAPPING section.
local FOLDER_TITLE_SIZE = nil       -- resolved once from skin_config
function SongSelect:folder_title(box)
    if FOLDER_TITLE_SIZE == nil then
        local cfg = tex.get_skin_config("folder_box_name")
        FOLDER_TITLE_SIZE = (cfg and cfg.font_size and cfg.font_size > 0) and cfg.font_size or false
    end
    if not FOLDER_TITLE_SIZE then return box:name() end
    local key = box.text_name
    if not key or key == "" then return box:name() end
    -- ROUND 6 (board text colours): the outline is the arcade genre colour add on
    -- `main_genre_board` sprite 70, not the engine's `fore_color` (which is a darkened
    -- back_color and does not match the cabinet).  `folder_frame` already resolves
    -- event (11) / sort (12) folders.
    local ff = folder_frame(box)
    key = ff .. "\0" .. key
    local t = self._folder_titles[key]
    if t == nil then
        t = text.create_raw_text(box.text_name, FOLDER_TITLE_SIZE, { 255, 255, 255, 255 },
                                 -- `main_genre_board` id 31 `text_kanban_title_genre`: border 7.5
                                 board_outline(ff), false, ol(7.5), 2.0)
        self._folder_titles[key] = t or false
    end
    return t or box:name()
end

function SongSelect:draw_folder_board(box, pb, ch_alpha, ch_off, ta, glow_a)
    local bx, by = box:box_x(), box:box_y()
    local fdx, fdy = fly_offset(box); bx, by = bx + fdx, by + fdy   -- r56 wheel fly-in
    local cen_h = CEN_CLOSED_H + (CEN_OPEN_H - CEN_CLOSED_H) * pb
    local fade = box_fade(box)
    local frame, has_pattern = folder_frame(box)

    -- ROUND 85 ---------------------------------------------------------------
    -- THE PIECE FIVE ROUNDS NEVER DREW.  The genre board does not merely go
    -- alpha 0 when a folder opens: `instance_board_center.sx` on sprite 73 sweeps
    -- 1.0 -> 0.025 over clip f100..f112 -- the board FURLS SHUT HORIZONTALLY to a
    -- vertical hairline, 13 f = 216.7 ms, the exact mirror of the song board's
    -- unfurl -- and only THEN vanishes at f113.  Coming back out it unfurls
    -- 0.025 -> 1.0 over f220..f232 and its contents return f238..f250.
    -- ROUND 61 sampled these channels into anim/genre_open and left them
    -- unconsumed (its own section 9: "the outgoing genre board still just
    -- disappears"), so the whole visible signature of the transition was absent
    -- on our side.  That is what six reports of 「分類開啟動畫還是不對」 were
    -- pointing at.
    local ltitle = 1.0
    if not R61.disabled then
        local lf = (box:draw_state() == "open" or is_decided(box)) and leg_f() or nil
        if lf then
            local sx, ba = go_get("genre_board_sx", lf, 1), go_get("genre_board_sx", lf, 2)
            if sx and ba then
                if ba <= 0.0 then return end   -- f113..f219: the board is not on screen
                if sx < 1.0 then               -- furl / unfurl: the open slab alone
                    if R61.dbg then
                        pcall(tex.log, string.format(
                            "[r85g] ms=%.1f f=%.2f kind=%s sx=%.4f a=%.3f w=%.1f",
                            tex.get_current_ms(), lf, tostring(R61.kind), sx, ba,
                            BOARD_W * sx))
                    end
                    draw_board(self._id_bar_folder, frame, bx, by, CEN_OPEN_H,
                               fade * ba, false, sx)
                    return
                end
                ch_alpha = math.min(ch_alpha, go_get("genre_chara_a", lf, 1) or 1.0)
                ta       = math.min(ta,       go_get("genre_intro_a", lf, 1) or 1.0)
                ltitle   = go_get("genre_title_a", lf, 1) or 1.0
                if R61.dbg then
                    pcall(tex.log, string.format(
                        "[r85g] ms=%.1f f=%.2f kind=%s sx=%.4f a=%.3f w=%.1f",
                        tex.get_current_ms(), lf, tostring(R61.kind), sx, ba,
                        BOARD_W * sx))
                end
            end
        end
    end

    if glow_a > 0 then self:draw_glow(bx, by, cen_h, fade * glow_a * glow_pulse()) end
    -- ROUND 5: closed folder = bar_genre, open folder = folder_graphic (coloured title band on top,
    -- pale panel only below it), frames 10..12 = dani/event/sort. While the board grows
    -- (pb 0 -> 1, 83 ms) the closed look is cross-faded out over the open one.
    -- ROUND 14: both textures are now the cabinet's own shapes, not round 5's flat rounded rect --
    -- `main_genre_board` shape 19 (the 960x24 white lip, 8 clear rows on top) + shape 23 (the 960x8
    -- white bar, x 82..878) at cmul alpha 141, plus shapes 1/3/5 (the black drop shadow).
    -- closed bake rows 32..56 lip / 56..296 fill / 296..320 flipped lip;
    -- open bake rows 126..150 lip / 150..296 fill / 296..320 flipped lip (the cabinet leaves the top
    -- lip at board -26 and grows the white DOWNWARDS, which is why the open board has a 70 px
    -- coloured band above the panel).  See Graphics/song_select/MAPPING.md ROUND 14.
    if pb <= 0.0 then
        draw_board(self._id_bar_genre, frame, bx, by, cen_h, fade)
    else
        draw_board(self._id_bar_folder, frame, bx, by, cen_h, fade, pb >= 1.0)
        if pb < 1.0 then draw_board(self._id_bar_genre, frame, bx, by, cen_h, fade * (1.0 - pb)) end
    end
    if has_pattern then self:draw_pattern(bx, by, pb, fade) end

    if box:has_box_texture() then
        -- ROUND 82 (r82-songselect-overflow-and-searchbox): a library-supplied
        -- `box.png` is arbitrary-sized character art with no cabinet counterpart of
        -- a known size, and it used to be pasted at 1:1 * screen_scale from a fixed
        -- top-left -- Songs/17 New's 344x424 banner came out 516x636 and hung ~359 px
        -- below the board, over 曲検索.  The cabinet does not scissor this board
        -- (0 ClipDepth masks across all 22 common_songselect movies, ~8,900
        -- PlaceObject records -- scratchpad/r82/); its art simply never exceeds its
        -- slot, every genre character frame being exactly 480x480.  So the banner is
        -- FITTED (aspect kept, shrink only) into the board's own 960x352 3-slice rect
        -- centred on (bx, by) -- MAPPING.md ROUND 14: "the 960x352 3-slice texture
        -- (cap 0..56 / centre 56..296 / cap 296..352)", canvas y = board y + 176.
        -- No size of input can leave the board's own footprint.
        if ch_alpha > 0 then
            box:draw_box_texture({ cx = bx, cy = by,
                                   max_w = BOARD_W, max_h = CAP_H * 2 + CEN_OPEN_H,
                                   fade = fade * ch_alpha })
        end
        -- The title is drawn AFTER the art, matching the cabinet's own depth order:
        -- `main_genre_board` puts `text_kanban_title_genre` at depth 18, ABOVE
        -- `instance_character_l/r` at depths 15/16 (MAPPING.md ROUND 14 table).  It
        -- used to be drawn first here, so a banner that reaches the title band
        -- covered the folder name.
        draw_title(self:folder_title(box), bx, by - 94 * pb, fade * ltitle)
        return
    end

    -- characters: texture has L at x=510 and R at x=1410 (+-450 from centre = the arcade overshoot
    -- position); ch_off is the arcade character offset (340 closed -> 450 -> 440 settled).
    if ch_alpha > 0 then
        local slide = 450.0 - ch_off
        local gframe = box:genre_frame()
        tex.draw_texture(self._id_box_chara, { frame = gframe, x = bx - 960 + slide, y = by - 240, x2 = 960 - 1920, fade = fade * ch_alpha,
                                               src = { x = 0, y = 0, width = 960, height = 480 } })
        tex.draw_texture(self._id_box_chara, { frame = gframe, x = bx - slide, y = by - 240, x2 = 960 - 1920, fade = fade * ch_alpha,
                                               src = { x = 960, y = 0, width = 960, height = 480 } })
    end
    -- arcade board label 'on': text_kanban_title_genre rises to y-94, text_kanban_intro_genre (font 32)
    -- sits at the centre; the song count stands in for the intro text.
    draw_title(self:folder_title(box), bx, by - 94 * pb, fade * ltitle)
    local n = box.tja_count
    if ta > 0 and n and n > 0 then
        -- ROUND 6 (board text colours): the intro text this stands in for
        -- (`main_genre_board` sprite 30 `text_kanban_intro_genre`, size 32 / outline 4.0, fill #ffffff)
        -- carries the same per-genre colour add as the title, so its edge is the genre colour.
        local ckey = frame .. "\0" .. n
        local label = self._count_cache[ckey]
        if not label then
            -- `main_genre_board` id 28 `text_kanban_intro_genre`: size 32, border 4.0
            label = text.create_raw_text(tostring(n) .. " songs", 32, { 255, 255, 255, 255 }, board_outline(frame), false, ol(4.0), 2.0)
            self._count_cache[ckey] = label
        end
        -- ROUND 14: the arcade's own ink centre for this field.  `main_genre_board`
        -- DefineEditText id 28 has rect index 7 = (-2,-2)-(758,134) (centre y +66) and its
        -- owner sprite 29 places it at ty -62+... = -17, inside `text_kanban_intro_genre`
        -- (sprite 30), which sprite 73 parks at board ty 0 in BOTH 'off' and 'on'
        -- => ink centre = board y +49 (Lumen dynamic text is always vertically centred in
        -- its rect, MAPPING "DefineEditText" ROUND 6).  That is also the centre of the
        -- re-baked pale panel (board -50..+144 -> +47); round 5's +40 was the centre of the
        -- flat panel it shipped with.
        if label then draw_title(label, bx, by + 49 * pb, fade * ta) end
    end
end

-- ROUND 51: the ROUND 17/39 per-name timer bodies moved into wheel_open_t /
-- wheel_close_step (shared with song / back / dani boards) - the behaviour for a
-- cursor-move refocus is unchanged (hold 508 ms, grow 233 ms, close 133 ms), but a
-- folder enter/exit now SNAPS open and a scene entry grows without the hold, matching
-- the cabinet state machine (see the mode table above open_start_mode).
function SongSelect:draw_folder_closed(box)
    local key = "f\0" .. box_id(box)   -- r63: per-BOX, not per-title (see box_id)
    local e = wheel_close_step(key, FOLDER_CLOSE_MS, STALE_TIMER_MS)
    if e then
        local pb, ch_alpha, ch_off, ta = folder_close_timeline(e)
        self:draw_folder_board(box, pb, ch_alpha, ch_off, ta, pb)
        return
    end
    self:draw_folder_board(box, 0.0, 0.0, 340.0, 0.0, 0.0)
end

function SongSelect:draw_folder_open(box)
    -- remember the decided board: the engine removes the folder box from the wheel
    -- while its songs load, and draw_overlays keeps drawing it from here until it
    -- has furled away (the C++ box outlives the wheel slot, so the handle stays valid)
    R61.last_open_id, R61.last_open_box = box_id(box), box
    if is_decided(box) then
        self._dec_frame = self._frame_no
        self:draw_folder_board(box, 1.0, 1.0, 440.0, 1.0, 1.0)
        return
    end
    local key = "f\0" .. box_id(box)   -- r63: per-BOX, not per-title (see box_id)
    local t = wheel_open_t(key)
    if t == nil then     -- cabinet 508 ms pre-growth hold: fully closed
        self:draw_folder_board(box, 0.0, 0.0, 340.0, 0.0, 0.0)
        return
    end
    local pb, ch_alpha, ch_off, ta = folder_open_timeline(t)
    self:draw_folder_board(box, pb, ch_alpha, ch_off, ta, pb)
end


--------------------------------------------------------------------------------
-- BackBox ("back" brown board with a label)
--------------------------------------------------------------------------------
function SongSelect:back_label()
    if not self._back_label then
        local cfg = tex.get_skin_config("song_back_label")
        -- ROUND 6 (board text colours): main_song_board sprite 86 `text_kanban_title_back` at label
        -- 'back' carries colour add (76,50,20) -> that is the exact もどる outline (was 80,50,10).
        if cfg then
            -- `main_song_board` id 79 `text_kanban_title_back`: size 42, border 5.0
            self._back_label = text.create_text("song_back_label", { 255, 255, 255, 255 }, BOARD_OUTLINE_BACK, false, ol(5.0), 2.0)
        else
            self._back_label = text.create_raw_text("BACK", 44, { 255, 255, 255, 255 }, BOARD_OUTLINE_BACK, false, ol(5.0), 2.0)
        end
    end
    return self._back_label
end

-- ROUND 51: the back board is a wheel panel like any other (main_song_board label 'back'),
-- so the cabinet's ProcKanbanWaitCountToSelectOn hold + select_on growth applies to it
-- too.  pb comes from the same folder-clip growth curve (identical f8..f13 window).
local function back_pb_open(box)
    local key = "b\0" .. box_id(box)   -- r63: per-BOX, not per-title (see box_id)
    local t = wheel_open_t(key)
    if t == nil then return 0.0 end
    return (folder_open_timeline(t))
end

local function back_pb_closed(box)
    local key = "b\0" .. box_id(box)   -- r63: per-BOX, not per-title (see box_id)
    local e = wheel_close_step(key, FOLDER_CLOSE_MS, STALE_TIMER_MS)
    if e == nil then return 0.0 end
    return (folder_close_timeline(e))
end

function SongSelect:draw_back_board(box, p)
    local bx, by = box:box_x(), box:box_y()
    local fdx, fdy = fly_offset(box); bx, by = bx + fdx, by + fdy   -- r56 wheel fly-in
    local cen_h = CEN_CLOSED_H + (CEN_OPEN_H - CEN_CLOSED_H) * p
    local fade = math.min(1.0, box_fade(box))

    -- ROUND 85: on the cabinet the panel that unfurls across the swap is the
    -- newly-focused one (`GetSongKanbanMc(kSelectNumber + openOffset)`).  In THIS
    -- engine a folder enter builds the wheel around a もどる box and leaves the
    -- cursor on it (`setup_back_box`, navigator.cpp), so the back board IS that
    -- panel -- the first live trace of this round drew 1146 genre-board frames and
    -- not one song-board frame, which is what pointed here.  It rides the same
    -- song_board_sx channel (the cabinet draws every wheel panel from
    -- main_song_board sprite 222; `back` is just one of its title labels).
    local lta = 1.0
    if not R61.disabled then
        local lf = (box:draw_state() == "open") and leg_f() or nil
        if lf and R61.kind == "open" and lf < R61.open_swap - 1 then return end   -- not yet
        if lf then
            local sx, ba = go_get("song_board_sx", lf, 1), go_get("song_board_sx", lf, 2)
            if sx and ba then
                if ba <= 0.0 then return end
                if sx < 1.0 then
                    if R61.dbg then
                        pcall(tex.log, string.format(
                            "[r85s] ms=%.1f f=%.2f kind=%s sx=%.4f a=%.3f w=%.1f",
                            tex.get_current_ms(), lf, tostring(R61.kind), sx, ba,
                            BOARD_W * sx))
                    end
                    draw_board(self._id_bar_back, 0, bx, by, CEN_OPEN_H,
                               fade * ba, false, sx)
                    return
                end
                lta = go_get("song_title_a", lf, 1) or 1.0
            end
        end
    end

    if p > 0 then self:draw_glow(bx, by, cen_h, fade * p * glow_pulse()) end
    draw_board(self._id_bar_back, 0, bx, by, cen_h, fade)
    draw_title(self:back_label(), bx, by, fade * lta)
end

function SongSelect:draw_back_closed(box)
    self:draw_back_board(box, back_pb_closed(box))
end

function SongSelect:draw_back_open(box)
    self:draw_back_board(box, back_pb_open(box))
end

--------------------------------------------------------------------------------
-- ROUND 15 — むずかしさからえらぶ / the arcade sort window
--------------------------------------------------------------------------------
-- Source: `common_songselect/common_song_select_sort_window.nulm` (geometry) and
-- `common_song_select/song_select_select_sort_window.lua` (state logic).  The cabinet
-- shows ONE 1320x760 window with three rows that are decided in turn - むずかしさ,
-- ★の数, 表示順 - not PyTaikoGreen's row of course boxes followed by a star page.
-- The engine (DiffSortSelect, arcade mode) owns the state machine and hands this
-- function a `SortWindow`; everything drawn below is measured off the .nulm:
--
--   main_instance          (960,540)        board shape1 1320x760 -> x 300..1620, y 160..920
--   select_diff_instance   row y 540-130 = 410      \ each row (sprite 30):
--   select_star_instance   row y 540+ 11 = 551      |   cursor_instance   (   0, 0) 1240x180
--   select_order_instance  row y 540+152 = 692      |   shape8 pill       (+164, 0)  792x104
--                                                   |   diff_mark         ( -89, 0)   96x96
--                                                   |   right_arrow       (-174, 0)   48x56
--                                                   |   left_arrow        (+502, 0)   48x56, matrix -1,0,0,-1
--                                                   |   tx_item_01/02     (+164, 0)  ink centre -13
--                                                   \   tx_header         (-400, 0)  ink centre -13
--   dummy_control_guide    (960, 827)       control_guide_s `s_menfuchi_kp`
--   board_songs_num        ink left  (1402, 277)    EditText id 32, align left, size 42
--   num_song               digits right-aligned at 1379, centre y 289, pitch 34, 48x56
--   board_genre_title      ink left  ( 405, 272)    EditText id 56, align left, size 54,
--                                                   colour-add (119,26,45) on its outline
--
-- Text ink centres: every one of the three EditText owners places its field so that the
-- rect centre lands at instance y -13 (id 24 rect (322.75,31)-(942.75,127) placed at
-- (-632.75,-92); id 26 (418.75,31)-(848.75,127) at (-633.75,-92); id 28
-- (-2,85.65)-(308,181.65) at (-153,-146.65)) - Lumen dynamic text is always vertically
-- centred in its rect (MAPPING ROUND 6 "DefineEditText").
local SW_ROW_Y     = { 410, 551, 692 }
local SW_BOARD_W, SW_BOARD_H = 1320, 760
local SW_CURSOR_W, SW_CURSOR_H = 1240, 180
local SW_PILL_CX   = 1124          -- 960 + 164
local SW_PILL_W, SW_PILL_H = 792, 104
local SW_MARK_CX   = 871           -- 960 - 89
local SW_ARROW_LX  = 786           -- 960 - 174
local SW_ARROW_RX  = 1462          -- 960 + 502
local SW_HEADER_CX = 560           -- 960 - 400
local SW_TEXT_DY   = -13
local SW_GUIDE_Y   = 827
local SW_GUIDE_W, SW_GUIDE_H = 372, 96
local SW_NUM_RIGHT = 1379
local SW_NUM_Y     = 289
local SW_NUM_PITCH = 34
local SW_NUM_W, SW_NUM_H = 48, 56
local SW_SONGS_X   = 1402          -- ink LEFT edge (EditText align = left)
local SW_SONGS_Y   = 277
local SW_TITLE_X   = 405
local SW_TITLE_Y   = 272
-- text_kanban colour add on board_genre_title_instance (colour table entry 43)
local SW_TITLE_OUTLINE = { 119, 26, 45, 255 }
local SW_HEADER_BOX_W  = 310       -- EditText id 28 rect width
local SW_ITEM_BOX_W    = { 620, 430, 430 }   -- id 24 (diff row) / id 26 (star, order)
-- cursor_instance (sprite 7): label on@9, shape3 (glow, below) alpha ramps 256 -> 0 over
-- 20 frames then 0 -> 256 over 30, and the clip loops at f59 => a 51-frame / 850 ms cycle.
local SW_CURSOR_CYCLE = 51.0

-- The strings are the cabinet's own word table (the reference build `datatable` wordlist), not invented:
-- sort_window_diff / _star / _order, course_easy..course_mania, sort_window_order_*,
-- folder_sort_select, sort_window_songs.  Keyed by `tex.language()` with en as the fallback.
-- skin_config keys (the strings themselves live there, per language)
local SW_KEYS = {
    header = { "sort_window_diff", "sort_window_star", "sort_window_order" },
    course = { "course_name_0", "course_name_1", "course_name_2", "course_name_3", "course_name_3" },
    order  = { "sort_window_order_0", "sort_window_order_1", "sort_window_order_2", "sort_window_order_3" },
    title  = "folder_sort_select",
    songs  = "sort_window_songs",
}

local function sw_words(group)
    local k = SW_KEYS[group]
    if type(k) == "table" then
        local out = {}
        for i, key in ipairs(k) do out[i] = tex.skin_text(key) end
        return out
    end
    return tex.skin_text(k)
end

-- OutlinedText cache: key = group .. "\0" .. index (the strings never change at runtime).
function SongSelect:_sw_text(key, str, size, thickness, outline)
    self._sw_texts = self._sw_texts or {}
    local t = self._sw_texts[key]
    if t == nil then
        t = text.create_raw_text(str, size, { 255, 255, 255, 255 },
                                 outline or { 0, 0, 0, 255 }, false, thickness, 2.0)
        self._sw_texts[key] = t or false
    end
    return t or nil
end

-- ROUND 19 (r19-sortwindow): the sort window's row/header boxes are the cabinet's own
-- Japanese-sized EditText rects (header 310px, item 430/620px - measured off the .nulm,
-- see the geometry table above). English localisation strings run much longer than any
-- Japanese or Chinese equivalent ('Number of "★"' at 368px vs a 310px header box;
-- "Not Donderful Combo" at 535px vs a 430px item box), so `draw_title`'s squeeze (which
-- stretches the dst quad's WIDTH only, leaving height untouched - see `draw_title` above
-- and `OutlinedText::draw`, src/libs/text.cpp) was squashing those two strings ~16-20%
-- horizontally while their outline strokes stayed full height: an anisotropic (x-only)
-- scale of an already-rasterised, diagonal-heavy hand-drawn font, which reads as the
-- glyphs being tilted/crooked rather than merely narrower. User report: "diff select的字
-- 稍微歪了" (sort_window_star / order row text).
--
-- Fix: when a string does not fit its box, re-rasterise it at a smaller (uniformly
-- scaled) point size instead of stretching the finished bitmap - same fix shape as
-- `draw_title`'s max_w/min_scale squeeze, but scaling BOTH axes together so the glyphs
-- keep their proportions. Falls back to the normal squeeze only if even the smallest
-- attempted size still overflows (should not happen at these box widths / string
-- lengths, kept only as a safety net for a future, longer translation).
local SW_FIT_MIN_SIZE = 22
function SongSelect:_sw_text_fit(key, str, size, thickness, outline, max_w)
    self._sw_texts = self._sw_texts or {}
    local t = self._sw_texts[key]
    if t == nil then
        t = text.create_raw_text(str, size, { 255, 255, 255, 255 },
                                 outline or { 0, 0, 0, 255 }, false, thickness, 2.0)
        if t and max_w and t.width > max_w then
            -- shrink proportionally (both axes) until it fits, or we hit the floor
            local fit_size = math.max(SW_FIT_MIN_SIZE, math.floor(size * max_w / t.width))
            if fit_size < size then
                local fit_thickness = thickness * fit_size / size   -- ROUND 16: border scales with size
                local t2 = text.create_raw_text(str, fit_size, { 255, 255, 255, 255 },
                                                outline or { 0, 0, 0, 255 }, false, fit_thickness, 2.0)
                if t2 then t = t2 end
            end
        end
        self._sw_texts[key] = t or false
    end
    return t or nil
end

-- Draw an OutlinedText with its ink LEFT edge on x (EditText align = left).
local function draw_text_left(t, x, cy, fade)
    if not t then return end
    t:draw({ x = math.floor(x + 0.5), y = math.floor(cy - t.height / 2 + 0.5), fade = fade })
end

-- shape3 (the glow half of the cursor) alpha over the 51-frame loop.
-- ROUND 53 (r53-anim-lumen-sweep): sampled from the cabinet's own timeline —
-- `Scripts/anim/sort_cursor.lua` is sprite 7 (`cursor_instance`) of
-- `common_song_select_sort_window.nulm` exported per-frame: track `#3@0` alpha 1 -> 0 over
-- f9..f29 and back over f29..f59, looping from the `on` label (f9) => the 51-frame cycle.
-- The R15 hand ramp had the same shape (the export confirms it is exactly piecewise
-- linear); the table is now the source of truth.  Fail-soft to the hand ramp (ROUND 8).
local _sc_ok, _sc_anim = pcall(require, "anim/sampler")
local SW_CURSOR_CLIP = (_sc_ok and _sc_anim.safe("anim/sort_cursor"))
                       or { get = function() return nil end, label = function() return nil end }
local SW_ON_F = SW_CURSOR_CLIP:label("on") or 9
local function sw_cursor_glow(ms)
    local r = (ms * 0.06) % SW_CURSOR_CYCLE
    local a = SW_CURSOR_CLIP:get("#3@0", SW_ON_F + r, "a")
    if a then return a end
    if r <= 20.0 then return 1.0 - r / 20.0 end
    return (r - 20.0) / 30.0
end

function SongSelect:_sw_ids()
    if self._sw_loaded then return end
    tex.load_folder("song_select", "diff_sort")
    self._id_sw_board  = tex.get_texture("song_select/diff_sort/background")     -- 1320x760 window (name reused)
    self._id_sw_pill   = tex.get_texture("song_select/diff_sort/level_box")      -- 792x104 white item pill
    self._id_sw_cur    = tex.get_texture("song_select/diff_sort/box_highlight")  -- 1240x180 flat yellow bar
    self._id_sw_glow   = tex.get_texture("song_select/diff_sort/box_outline")    -- 1240x180 pulsing glow
    self._id_sw_mark   = tex.get_texture("song_select/diff_sort/diff")           -- 96x96, frame = course
    self._id_sw_arrow  = tex.get_texture("song_select/diff_sort/arrow")          -- 48x56, frame 1 = rotated
    self._id_sw_digit  = tex.get_texture("song_select/diff_sort/stat_num")       -- 48x56, frame = digit
    self._id_sw_guide  = tex.get_texture("song_select/diff_sort/star_select_text") -- s_menfuchi_kp 210-cell loop
    self._sw_loaded = true
end

function SongSelect:draw_sort_window(w)
    self:_sw_ids()
    local a = w:alpha()
    if a <= 0.0 or not self._id_sw_board then return end
    local ms = tex.get_current_ms()
    local session = w:session()
    local vals = { w:diff(), w:star(), w:order() }

    -- main_instance: board shape1 centred on the stage root
    draw_centered(self._id_sw_board, 960, 540, SW_BOARD_W, SW_BOARD_H, { fade = a })

    -- the three rows, in the cabinet's own depth order (order, star, diff)
    for row = 3, 1, -1 do
        local ry = SW_ROW_Y[row]
        local idx = row - 1                       -- 0-based, matching SortWindow:session()
        if idx == session and self._id_sw_cur then
            -- cursor_instance @ label `on`: shape3 (glow) below, shape5 (flat) above
            if self._id_sw_glow then
                draw_centered(self._id_sw_glow, 960, ry, SW_CURSOR_W, SW_CURSOR_H,
                              { fade = a * sw_cursor_glow(ms) })
            end
            draw_centered(self._id_sw_cur, 960, ry, SW_CURSOR_W, SW_CURSOR_H, { fade = a })
        end
        if self._id_sw_pill then
            draw_centered(self._id_sw_pill, SW_PILL_CX, ry, SW_PILL_W, SW_PILL_H, { fade = a })
        end
        -- diff_mark_instance is visible on the むずかしさ row only (SetItemName)
        if row == 1 and self._id_sw_mark then
            draw_centered(self._id_sw_mark, SW_MARK_CX, ry, 96, 96,
                          { frame = math.max(0, math.min(4, vals[1] - 1)), fade = a })
        end
        if self._id_sw_arrow then
            local dx = w:arrow_offset(idx)
            draw_centered(self._id_sw_arrow, SW_ARROW_LX + dx, ry, 48, 56, { frame = 0, fade = a })
            draw_centered(self._id_sw_arrow, SW_ARROW_RX - dx, ry, 48, 56, { frame = 1, fade = a })
        end
        -- item value
        local str, key
        if row == 1 then
            local n = math.max(1, math.min(5, vals[1]))
            str, key = sw_words("course")[n], "course" .. n
        elseif row == 2 then
            str, key = tostring(vals[2]), "star" .. vals[2]
        else
            local n = math.max(1, math.min(4, vals[3]))
            str, key = sw_words("order")[n], "order" .. n
        end
        -- `common_song_select_sort_window` ids 24/26/28/32: size 42, border 6.5
        -- ROUND 19: fit-shrink (both axes) instead of draw_title's x-only squeeze, so a
        -- too-wide English string (e.g. "Not Donderful Combo") never comes out anisotropically
        -- squashed/crooked. draw_title's own max_w squeeze is kept as a last-resort safety net.
        draw_title(self:_sw_text_fit(key, str, 42, ol(6.5), nil, SW_ITEM_BOX_W[row]),
                   SW_PILL_CX, ry + SW_TEXT_DY, a, SW_ITEM_BOX_W[row], 0)
        -- header
        draw_title(self:_sw_text_fit("hdr" .. row, sw_words("header")[row], 42, ol(6.5), nil, SW_HEADER_BOX_W),
                   SW_HEADER_CX, ry + SW_TEXT_DY, a, SW_HEADER_BOX_W, 0)
    end

    -- control guide (s_menfuchi_kp) + its two pill labels
    if self._id_sw_guide then
        local cell = math.floor((ms * 0.06) % 210)
        draw_centered(self._id_sw_guide, 960, SW_GUIDE_Y, SW_GUIDE_W, SW_GUIDE_H,
                      { frame = cell, fade = a })
        local labels = self:guide_labels()
        if labels then
            draw_guide_label(labels.select, GUIDE_SELECT_CX, SW_GUIDE_Y + 2, a)
            draw_guide_label(labels.decide, GUIDE_DECIDE_CX, SW_GUIDE_Y + 2, a)
        end
    end

    -- song counter: 「N 曲」 (right-aligned digits, then the unit label)
    local n = math.max(0, math.min(9999, w:song_num()))
    local digits = tostring(n)
    for i = 1, #digits do
        local d = tonumber(digits:sub(i, i))
        local x = SW_NUM_RIGHT - (#digits - i) * SW_NUM_PITCH
        draw_centered(self._id_sw_digit, x, SW_NUM_Y, SW_NUM_W, SW_NUM_H, { frame = d, fade = a })
    end
    draw_text_left(self:_sw_text("songs", sw_words("songs"), 42, ol(6.5)), SW_SONGS_X, SW_SONGS_Y, a)

    -- board title, with the cabinet's own dark-red outline add
    -- `common_song_select_sort_window` id 56 `board_genre_title_instance`: size 54, border 7.5
    draw_text_left(self:_sw_text("title", sw_words("title"), 54, ol(7.5), SW_TITLE_OUTLINE),
                   SW_TITLE_X, SW_TITLE_Y, a)
end

--------------------------------------------------------------------------------
-- Dispatch
--------------------------------------------------------------------------------
-- scenes/song_select_2p.cpp calls neither Navigator::draw_diff_select_bg() nor
-- SongSelectPlayer::try_lua_selector(), so in 2P the arcade backboard has to be drawn from
-- draw_box (which IS called) and the cursors come from the engine's own draw_selector.
local function is_2p_screen()
    return (tex.current_screen and tex.current_screen() or "") == "SONG_SELECT_2P"
end

function SongSelect:draw_box(box)
    -- ROUND 51: scene-entry detection.  The cabinet's SecondLoading plays select_on
    -- IMMEDIATELY on the initially-focused board (no 508 ms hold), so the first box draw
    -- ever - and the first after any >1 s gap in box draws (came back from enso etc.,
    -- if this Lua state survives the scene) - arms the one-shot "nohold" mode.
    local nowms = tex.get_current_ms()
    -- (r85: the engine also emits WHEEL_EVENT_SCENE_ENTRY, but Navigator::init()
    -- runs before this Lua state has polled once, so this stays as the detector for
    -- "first box draw ever" and for a Lua state that outlives the scene.)
    if _last_box_draw_ms == nil or (nowms - _last_box_draw_ms) > 1000.0 then
        R61.nohold_once = true
        R61.nohold_ms = nowms
    end
    _last_box_draw_ms = nowms

    local kind, state = box:lua_kind(), box:draw_state()

    -- ROUND 63 (r63-duplicate-song-box) -- env-gated wheel trace.  Settles
    -- "two identical song boxes" the only way it can be settled: A (two entries
    -- in the wheel) logs two DIFFERENT paths; B (one box drawn twice) logs the
    -- same path twice inside one frame.  Off unless YATAIDON_R63_DEBUG is set.
    if R63_DBG then
        local p = box_id(box)                         -- r63: per-BOX identity
        if nowms ~= _r63_ms then
            _r63_ms, _r63_seen, _r63_n = nowms, {}, 0
            pcall(tex.log, string.format("[r63] --- frame ms=%.1f ---", nowms))
        end
        _r63_n = _r63_n + 1
        local dup = _r63_seen[p] and " DUPLICATE-IN-FRAME" or ""
        _r63_seen[p] = true
        pcall(tex.log, string.format("[r63] #%d kind=%s state=%s y=%.1f name=%q id=%q%s",
              _r63_n, tostring(kind), tostring(state), box:box_y(),
              tostring(box.text_name), p, dup))
    end

    if kind == "song" then
        if state == "diff_select" then
            -- ROUND 15: the 2P scene now calls Navigator::draw_diff_select_bg() itself
            -- (scenes/song_select_2p.cpp, gated on has_box_bg) BEFORE the wheel, and the
            -- skin's course frame is drawn right after it. Drawing the 1272x784 backboard
            -- a second time from here painted over that frame, which is why the 1P/2P
            -- yellow course frame was missing in 2P. `_bg_drawn` latches the first time
            -- the engine calls draw_box_bg, so an engine that never calls it still gets
            -- the ROUND 6 fallback and a current one never double-draws.
            if is_2p_screen() and not self._bg_drawn then
                self:draw_song_diff_select_bg(box)
            end
            self:draw_song_diff_select(box)
        elseif state == "open" then self:draw_song_open(box)
        else self:draw_song_closed(box) end
    elseif kind == "folder" then
        if state == "open" or is_decided(box) then self:draw_folder_open(box)
        else self:draw_folder_closed(box) end
    elseif kind == "back" then
        if state == "open" then self:draw_back_open(box)
        else self:draw_back_closed(box) end
    else
        -- dan / other boxes: plain board in the arcade "dani" colour (folder_graphic frame 10)
        -- ROUND 51: timed like every other wheel panel (508 ms hold + clip growth /
        -- select_off close) instead of the raw engine open_anim ramp.
        local bx, by = box:box_x(), box:box_y()
        local fdx, fdy = fly_offset(box); bx, by = bx + fdx, by + fdy   -- r56 wheel fly-in
        local key = "o\0" .. box_id(box)   -- r63: per-BOX, not per-title (see box_id)
        local p
        if state == "open" then
            local t = wheel_open_t(key)
            p = t and (folder_open_timeline(t)) or 0.0
        else
            local e = wheel_close_step(key, FOLDER_CLOSE_MS, STALE_TIMER_MS)
            p = e and (folder_close_timeline(e)) or 0.0
        end
        local cen_h = CEN_CLOSED_H + (CEN_OPEN_H - CEN_CLOSED_H) * p
        local fade = math.min(1.0, box_fade(box))
        if p > 0 then self:draw_glow(bx, by, cen_h, fade * p * glow_pulse()) end
        draw_board((p > 0) and self._id_bar_folder or self._id_bar_genre, 10, bx, by, cen_h, fade, p <= 0 or p >= 1)   -- ROUND 5: closed = full pale panel
        if p > 0 and p < 1 then draw_board(self._id_bar_genre, 10, bx, by, cen_h, fade * (1.0 - p)) end
        draw_title(self:folder_title(box), bx, by - 94 * p, fade)
    end
end

function SongSelect:draw_box_bg(box)
    if box:lua_kind() ~= "song" then return end
    -- ROUND 15: latch that the ENGINE drives the backdrop, so draw_box's 2P
    -- fallback never draws it a second time over the course frame.
    self._bg_drawn = true
    self:draw_song_diff_select_bg(box)
end

-- ============================================================================
-- Pro select v2 (see the vault handoff `Handoff - YataiDON Pro Select v2.md`).
-- The engine calls SongSelect:draw_pro(model) while the pro screen is live.
-- All pro code lives in its own file (this chunk is at Lua's 200-local limit);
-- the loader below uses a function so it adds no main-chunk locals.
-- ============================================================================
YD_PRO = nil
function YD_PRO_LOAD()
    local ok, mod = pcall(require, "song_select/pro_select")
    if ok then YD_PRO = mod end
end
YD_PRO_LOAD()

function SongSelect:draw_pro(model)
    if YD_PRO then return YD_PRO.draw(self, model) end
    return false
end
