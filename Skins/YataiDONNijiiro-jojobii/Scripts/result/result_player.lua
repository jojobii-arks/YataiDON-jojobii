-- Nijiiro arcade result board (1P/2P). Draws the score board, gauge, crown and
-- message; the engine owns the result state machine.
ResultPlayer = {}
ResultPlayer.__index = ResultPlayer

local GAUGE_SCALE   = 0.7           -- arcade places tamashii_gage at 0.7 on the result board
local GAUGE_CELLS   = 50                 -- the cabinet's gauge is 50 cells (kMaxGage_), like the game HUD
local GAUGE_UNIT    = 21 * GAUGE_SCALE   -- one 21 px cell of the groove, at the board's 0.7
local OPTION_PITCH  = 43

local FRAME_MS        = 1000 / 120
-- Local override: snappier results than arcade parent (YataiDONNijiiro keeps stock timings).
-- Post-reveal C++ dwell is shortened separately via config general.fast_transitions.
local WAIT_GAUGE_MS   = 0                           -- was 100 frames / 833.3 ms
local GAUGE_MS_UNIT   = 7 * FRAME_MS                -- 58.33  ms per cell (keep fill rate)
local WAIT_SCORE_MS   = 0                           -- was 100 frames / 833.3 ms
local SCORE_TO_CROWN  = 0                           -- was 500 frames / 4166.7 ms
local CROWN_TO_ACTION = 0                           -- was 150 frames / 1250.0 ms
local RANK_STATE_MS   = 0                           -- was 120 frames @ 60fps / 2000.0 ms

local ClipPlayer = require("game/clip_player")
-- score_rank_mc / score_rank_kiwami_mc placements inside the 1920x1080 result stage
local RANK_POS   = { L = { 181.1, 500.7 }, R = { 1126.9, 501.0 } }
local KIWAMI_POS = { L = { 198.1, 505.7 }, R = { 1144.0, 506.0 } }
local RANK_SHAPES = { 74, 76, 78, 80, 82, 84, 86 }  -- rank 1..7 plates inside score_rank
local RANK_THRESHOLDS = { 500000, 600000, 700000, 800000, 900000, 950000, 1000000 }
local function rank_of(score)
    local r = 0
    for i, t in ipairs(RANK_THRESHOLDS) do if score >= t then r = i end end
    return r
end

local digit_ok, dg_anim = pcall(require, "anim/sampler")
if not digit_ok then dg_anim = { safe = function()
    local n = {}
    n.at = function() return nil end; n.get = function() return nil end
    n.label = function() return nil end; n.has = function() return false end
    n.first = function() return 0 end;  n.last = function() return 0 end
    return n
end } end
local digit_judge, DGJ_TRACK = dg_anim.safe("anim/result_digit_judge"), "#147@0"
local digit_score,  DGS_TRACK = dg_anim.safe("anim/result_digit_score"), "#225@0"
local DGJ_F0, DGJ_F1 = digit_judge:first(), digit_judge:last()
local DGS_F0, DGS_F1 = digit_score:first(), digit_score:last()

local success_bg = dg_anim.safe("anim/result_bg_success")
local SBG_F0 = success_bg:label("success") or 5
local function success_fade_alpha(t0, now_ms)
    if not t0 then return nil end
    return success_bg:get("crear_1P_l", SBG_F0 + (now_ms - t0) * 0.06, "a")
end

local FIRE = dg_anim.safe("anim/fever_fire")
local FIRE_LEAVES = { { "#40@0", 0 }, { "#44@0", 1 }, { "#46@0", 2 }, { "#48@0", 3 },
                      { "#50@0", 4 }, { "#52@0", 5 }, { "#54@0", 6 }, { "#56@0", 7 } }
local FIRE_START = FIRE:label("fever_start") or 1
local FIRE_LOOP  = FIRE:label("fever") or 10
local FIRE_END   = FIRE:label("fever_end") or 34
local FIRE_SHEEN_A = FIRE:get("#39@2", (FIRE_LOOP + FIRE_END) / 2, "a") or 0.5

-- flame cel, alpha and sheen alpha at (now - t0) ms since the gauge maxed; nil = no table
local function fire_state(t0, now_ms)
    if not t0 or t0 < 0 or not FIRE.tracks then return nil end
    local f = FIRE_START + (now_ms - t0) * 0.06
    if f >= FIRE_LOOP then
        f = FIRE_LOOP + math.fmod(f - FIRE_LOOP, FIRE_END - FIRE_LOOP)
    end
    local fi = math.floor(f)
    for i = 1, #FIRE_LEAVES do
        local rows = FIRE.tracks[FIRE_LEAVES[i][1]]
        if rows and #rows > 0 and fi >= rows[1][1] and fi <= rows[#rows][1] then
            local a = FIRE:get(FIRE_LEAVES[i][1], f, "a")
            if a then
                local sheen
                if fi < FIRE_LOOP then
                    sheen = FIRE:get("#39@2", f, "a") or 0.0      -- start ramp 0 -> 0.6016
                else
                    sheen = ((fi - FIRE_LOOP) % 4 < 2) and FIRE_SHEEN_A or 0.0
                end
                return FIRE_LEAVES[i][2], a, sheen
            end
        end
    end
    return nil
end

local RBW = dg_anim.safe("anim/gauge_rainbow")
local RBW_LEAVES = { { "#15@0", 0 }, { "#17@1", 1 }, { "#19@2", 2 }, { "#21@3", 3 },
                     { "#23@4", 4 }, { "#25@5", 5 }, { "#27@6", 6 }, { "#29@7", 7 },
                     { "#15@8", 0 } }
local RBW_LEN = RBW:last() - RBW:first()      -- 40 frames = 666.7 ms cycle

local function pop_scale(clip, track, f0, f1, land_ms, now_ms)
    if not land_ms then return 1.0 end
    local f = f0 + (now_ms - land_ms) * 0.06
    if f > f1 then return 1.0 end
    return clip:get(track, f, "sx") or 1.0
end

function ResultPlayer.new(player_num, is_2p, has_2p,
                          gauge_length, difficulty, selected_difficulty,
                          crown_type, score_diff, score_method,
                          mod_display, mod_inverse, mod_random, mod_speed)
    local self = setmetatable({}, ResultPlayer)

    self.player_num          = player_num
    local timing = tex.timing_offset(player_num)
    if timing then
        self.timing_text = text.create_raw_text(timing, 20 * tex.get_screen_scale(), {255, 255, 255, 255}, {0, 0, 0, 255}, false, 3.0)
    end
    self.is_2p               = is_2p
    self.has_2p              = has_2p
    self.gauge_length        = gauge_length
    self.gauge_cells         = math.floor(gauge_length * GAUGE_CELLS / 87 + 1e-4)  -- 87-unit display -> 50-cell gauge
    self.difficulty          = difficulty
    self.selected_difficulty = selected_difficulty
    self.crown_type          = crown_type
    self.score_diff          = score_diff
    self.score_method        = score_method
    self.mod_display         = mod_display
    self.mod_inverse         = mod_inverse
    self.mod_random          = mod_random
    self.mod_speed           = mod_speed

    local ART   = { [0] = 0, [1] = 1, [2] = 1, [3] = 2 }
    local NORMA = { [0] = 6000, [1] = 7000, [2] = 7000, [3] = 8000 }
    local d     = math.min(math.max(difficulty, 0), 3)
    self.art_index = ART[d]
    self.norma     = NORMA[d]
    if gauge_length == 87 then
        self.gauge_state = 2
    elseif gauge_length / 87.0 * 10000.0 >= self.norma - 0.5 then
        self.gauge_state = 1
    else
        self.gauge_state = 0
    end

    if self.art_index >= 2 then
        self.diff_str = "_hard"
    elseif self.art_index >= 1 then
        self.diff_str = "_normal"
    else
        self.diff_str = "_easy"
    end

    local p                    = tostring(player_num) .. "p"
    self.player_str            = p
    self.score, self.good, self.ok, self.bad, self.max_combo, self.total_drumroll = "", "", "", "", "", ""
    self.fade_in_finished_flag = false

    self.gauge_started   = false
    self.gauge_start_ms  = -1
    self.gauge_shown     = 0          -- cells currently filled (animates up to gauge_cells)
    self.gauge_done      = false
    self.score_delay     = nil
    self.fade_in_started = false      -- read by C++: starts the score roll
    self.rainbow_start_ms = -1.0
    self.rainbow_frac    = 0.0
    self.crown_at        = nil
    self.msg_at          = nil       -- MoveResultAction: message bubble + success backdrop + Don pose
    self.skipped         = false
    self.reveal_end_ms   = 0
    self.count_up_instant  = true
    self.count_up_row_ms   = 50 * FRAME_MS
    self.count_up_score_ms = 100 * FRAME_MS
    self.count_up_row_sound    = "count_stop"
    self.count_up_score_sound  = "don_big"
    self.count_up_sound_1p_only = true
    self.seat_c   = has_2p and (is_2p and "_r" or "_l") or "_c"
    self.seat_lr  = is_2p and "_r" or "_l"
    self.count_loop_on = false
    self.achieve_played = false
    self.skip_don_big_played = false
    -- per-row landing times, for the arcade digit pop (see draw_score_info / draw_total_score)
    self.row_land_ms   = {}
    self.score_land_ms = nil

    self.crown_obj      = nil
    self.crown_msg_obj  = nil
    self.high_score_obj = nil

    self.fade_in_bottom = tex.get_animation(1, "result")    -- success bg fade (fallback)
    self.tamashii_fire  = tex.get_animation(20, "result")

    -- background success overlay (1P full / 2P halves)
    self.t_success      = tex.get_texture(has_2p and "result/background/footer_2p" or "result/background/footer_1p")
    -- board
    self.t_board        = tex.get_texture("result/score/overlay")         -- frames: 0 = left board, 1 = right board
    self.t_difficulty   = tex.get_texture("result/score/difficulty")      -- 5 frames + course name
    self.t_opt_bg       = tex.get_texture("result/score/mod_sanbai")      -- option pill, frame = icon count-1
    self.t_mod_doron    = tex.get_texture("result/score/mod_doron")
    self.t_mod_abekobe  = tex.get_texture("result/score/mod_abekobe")
    self.t_mod_kimagure = tex.get_texture("result/score/mod_kimagure")
    self.t_mod_detarame = tex.get_texture("result/score/mod_detarame")
    self.t_mod_speed    = tex.get_texture("result/score/mod_baisaku")     -- 14 frames x1.1 .. x4
    self.t_judge_labels = tex.get_texture("result/score/max_combo")       -- whole 良/可/不可/連打数/最大コンボ数 block (ja/en)
    self.t_judge_num    = tex.get_texture("result/score/judge_num")
    self.t_score_lbl    = tex.get_texture("result/score/score")
    self.t_score_shinuchi_lbl = tex.get_texture("result/score/score_shinuchi")
    self.t_score_num    = tex.get_texture("result/score/score_num")
    self.t_donback      = tex.get_texture("result/bottom/chara_0")        -- frames: 0 = 1P red, 1 = 2P blue

    -- gauge (HUD bakes, drawn at 0.7)
    self.t_gauge_unfilled = tex.get_texture("result/gauge/" .. p .. "_unfilled" .. self.diff_str)
    self.t_gauge_bar      = tex.get_texture("result/gauge/" .. p .. "_bar")
    self.t_gauge_rainbow  = tex.get_texture("result/gauge/rainbow" .. self.diff_str)
    self.t_gauge_overlay  = tex.get_texture("result/gauge/overlay" .. self.diff_str)
    self.t_gauge_clear    = tex.get_texture("result/gauge/clear")
    self.t_gauge_clear_dark = tex.get_texture("result/gauge/clear_dark")
    self.t_gauge_bar_clear_transition = tex.get_texture("result/gauge/bar_clear_transition")
    self.t_gauge_bar_clear_top    = tex.get_texture("result/gauge/bar_clear_top")
    self.t_gauge_bar_clear_bottom = tex.get_texture("result/gauge/bar_clear_bottom")
    self.t_gauge_tamashii         = tex.get_texture("result/gauge/tamashii")
    self.t_gauge_tamashii_dark    = tex.get_texture("result/gauge/tamashii_dark")
    self.t_gauge_tamashii_fire    = tex.get_texture("result/gauge/tamashii_fire")
    self.t_gauge_tamashii_overlay = tex.get_texture("result/gauge/tamashii_overlay")

    -- digit pitches are fixed by the arcade digit MCs (digit4 pitch 31, digit7 pitch 46), not skin_config
    self.sc_score_info_margin   = 31
    self.sc_result_score_margin = 46

    -- option icon list (arcade order: speed, doron, abekobe, random)
    self.options = {}
    if mod_speed > 10 then
        local speeds = { 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 25, 30, 35, 40 }
        local best, bi = 1e9, 1
        for i, s in ipairs(speeds) do
            if math.abs(s - mod_speed) < best then best, bi = math.abs(s - mod_speed), i end
        end
        table.insert(self.options, { self.t_mod_speed, bi - 1 })
    end
    if mod_display then table.insert(self.options, { self.t_mod_doron, 0 }) end
    if mod_inverse then table.insert(self.options, { self.t_mod_abekobe, 0 }) end
    if mod_random == 1 then table.insert(self.options, { self.t_mod_kimagure, 0 })
    elseif mod_random == 2 then table.insert(self.options, { self.t_mod_detarame, 0 }) end

    return self
end

function ResultPlayer:update(current_ms, fade_in_finished,
                             score, good, ok, bad, max_combo, total_drumroll, is_skipped)
    self.fade_in_finished_flag = fade_in_finished
    self.now_ms = current_ms
    local prev = { self.good, self.ok, self.bad, self.total_drumroll, self.max_combo }
    self.score, self.good, self.ok, self.bad = score, good, ok, bad
    self.max_combo, self.total_drumroll = max_combo, total_drumroll
    local now_rows = { good, ok, bad, total_drumroll, max_combo }
    for i = 1, 5 do
        if prev[i] == "" and now_rows[i] ~= "" and not self.row_land_ms[i] then
            self.row_land_ms[i] = current_ms
        end
    end
    if self.score ~= "" and not self.score_land_ms then self.score_land_ms = current_ms end

    -- isEffectSkip_ (ResultMain): every remaining state collapses at once.
    if is_skipped and not self.skipped then
        self.skipped = true
        self.gauge_shown = self.gauge_cells
        if self.count_loop_on then
            self.count_loop_on = false
            if audio.stop_sound then audio.stop_sound("count_up_loop" .. self.seat_c) end
        end
        if self.player_num == 1 and not self.skip_don_big_played then
            self.skip_don_big_played = true
            audio.play_sound("don_big", "sound")
        end
    end

    if fade_in_finished and not self.gauge_started then
        self.gauge_started  = true
        self.gauge_start_ms = current_ms
        self.tamashii_fire:start()
    end

    if self.gauge_started then
        self.tamashii_fire:update(current_ms)
        local target = self.gauge_cells
        -- WaitUpdateTamashiiGage: 100 frames of nothing, then 7 frames per arcade unit
        local elapsed = current_ms - self.gauge_start_ms - WAIT_GAUGE_MS
        if self.skipped then
            self.gauge_shown = target
        elseif elapsed > 0 then
            if not self.count_loop_on and not self.gauge_done then
                self.count_loop_on = true
                audio.play_sound("count_up_loop" .. self.seat_c, "sound", true)
            end
            self.gauge_shown = math.min(target, math.floor(elapsed / GAUGE_MS_UNIT))
            if not self.achieve_played and self.gauge_state == 2 and self.gauge_shown >= GAUGE_CELLS then
                self.achieve_played = true
                audio.play_sound("achieve_tamashii" .. self.seat_lr, "sound")
            end
        end
        if self.gauge_shown >= target and not self.gauge_done
           and (self.skipped or elapsed > 0) then
            self.gauge_done = true
            self.score_delay = current_ms + (self.skipped and 0 or WAIT_SCORE_MS)
            if self.count_loop_on then
                self.count_loop_on = false
                if audio.stop_sound then audio.stop_sound("count_up_loop" .. self.seat_c) end
            end
        end
        if self.gauge_state == 2 and self.gauge_shown >= GAUGE_CELLS then
            if self.rainbow_start_ms < 0 then self.rainbow_start_ms = current_ms end
            self.rainbow_frac = math.fmod((current_ms - self.rainbow_start_ms) / 75.0, 8.0)
        end
    end
    self.fade_in_bottom:update(current_ms)
    -- publish the success cross-fade for the live backdrop (1P only)
    if not self.has_2p then
        local sfa = success_fade_alpha(self.success_fade_t0, current_ms)
                    or self.fade_in_bottom.attribute
        __hss_result_bg_clear = (self.gauge_state >= 1) and sfa or 0.0
    end

    if self.score_delay ~= nil and current_ms > self.score_delay and not self.fade_in_started then
        self.fade_in_started = true
        self.crown_at = current_ms + SCORE_TO_CROWN
        self.msg_at   = self.crown_at + ((self.gauge_state >= 1) and CROWN_TO_ACTION or 0)
    end
    if self.skipped and self.fade_in_started then
        self.rank_obj = nil
        self.crown_at = math.min(self.crown_at or current_ms, current_ms)
        self.msg_at   = math.min(self.msg_at   or current_ms, current_ms)
    end

    if self.crown_at and current_ms >= self.crown_at and not self.rank_done then
        self.rank_done = true
        local rank = (not self.skipped) and rank_of(math.floor(tonumber(self.score) or 0)) or 0
        if rank > 0 then
            local side = self.is_2p and "R" or "L"
            local pos  = (rank == 7) and KIWAMI_POS[side] or RANK_POS[side]
            local clip = (rank == 7) and "rank_result_kiwami" or "rank_result"
            self.rank_obj = ClipPlayer.new("game/ending_anim/" .. clip, "game", clip .. "_anim", pos[1], pos[2])
            if self.rank_obj then
                self.rank_obj.hold_last = true
                if rank < 7 then self.rank_obj.shape_map = { [74] = RANK_SHAPES[rank], [76] = RANK_SHAPES[rank] } end  -- clip uses plate 74 then 76
                self.rank_obj:start(current_ms)
                -- se_result `scorerank_NN_<seat>`: one sample for every rank, seat like the crown
                audio.play_sound("scorerank" .. (self.has_2p and (self.is_2p and "_r" or "_l") or "_c"), "sound")
                self.crown_at = current_ms + RANK_STATE_MS
                if self.msg_at then self.msg_at = self.msg_at + RANK_STATE_MS end
            end
        end
    end

    -- Crown state entry: StartCrownDisplay (crownMc_ GotoAndPlay "start" + crown_<colour>_c)
    if self.crown_at and current_ms >= self.crown_at
       and self.gauge_state >= 1 and not self.crown_obj then
        self.crown_obj = ResultCrown.new(self.crown_type, self.is_2p, self.has_2p)
        if self.crown_type == 1 or self.crown_type == 2 then
            audio.play_sound("full_combo_voice_" .. tostring(self.player_num) .. "p", "voice")
        end
    end
    if self.crown_obj then self.crown_obj:update(current_ms) end

    if self.msg_at and current_ms >= self.msg_at and not self.crown_msg_obj then
        local frame
        if self.gauge_state == 2 then       frame = 3        -- perfect
        elseif self.gauge_state >= 1 then   frame = 2        -- success
        elseif self.gauge_length > 87 * 0.3 then frame = 1   -- near
        else                                frame = 0 end    -- miss
        self.crown_msg_obj = ResultCrownMessage.new(frame, self.is_2p)
        local voices = { [0] = "max_fail_voice_", [1] = "fail_voice_",
                         [2] = "clear_voice_",    [3] = "max_clear_voice_" }
        audio.play_sound(voices[frame] .. tostring(self.player_num) .. "p", "voice")
        if self.gauge_state >= 1 then
            self.fade_in_bottom:start()   -- success background
            self.success_fade_t0 = current_ms
            audio.play_sound("atmos_clear" .. self.seat_c, "sound")
        end
    end
    if self.crown_msg_obj then self.crown_msg_obj:update(current_ms) end

    if self.reveal_end_ms == 0 and self.msg_at and current_ms >= self.msg_at then
        self.reveal_end_ms = self.msg_at
    end

    -- DisplayHiscoreEffect runs inside the same branch that lands the total score.
    if not self.high_score_obj and self.score_diff > 0 and self.score ~= "" then
        self.high_score_obj = HighScoreIndicator.new(self.score_diff, self.is_2p)
    end
    if self.high_score_obj then self.high_score_obj:update(current_ms) end
end

function ResultPlayer:draw()
    local p_idx = self.is_2p and 1 or 0

    -- success background cross-fade
    local success_a = success_fade_alpha(self.success_fade_t0, tex.get_current_ms())
                      or self.fade_in_bottom.attribute
    if self.gauge_state >= 1 and success_a > 0 and self.t_success then
        tex.draw_texture(self.t_success, { frame = self.has_2p and p_idx or 0, index = p_idx, fade = success_a })
    end

    -- board frame (opaque white body), then the red/blue donback panel on top of it, then the difficulty tab
    if self.t_board then tex.draw_texture(self.t_board, { frame = p_idx, index = p_idx }) end
    if self.t_donback then tex.draw_texture(self.t_donback, { frame = p_idx, index = p_idx }) end
    if self.timing_text then
        local info = tex.get_texture_info("bottom", "chara_0")
        if info and info.x[p_idx + 1] then
            local pad = 6 * tex.get_screen_scale()
            self.timing_text:draw({ x = info.x[p_idx + 1] + pad, y = info.y[p_idx + 1] + pad })
        end
    end
    if self.t_difficulty then tex.draw_texture(self.t_difficulty, { frame = self.selected_difficulty, index = p_idx }) end

    -- option icons
    if #self.options > 0 then
        if self.t_opt_bg then tex.draw_texture(self.t_opt_bg, { frame = #self.options - 1, index = p_idx }) end
        for i, o in ipairs(self.options) do
            if o[1] then tex.draw_texture(o[1], { frame = o[2], x = (i - 1) * OPTION_PITCH, index = p_idx }) end
        end
    end

    -- judge table labels
    if self.t_judge_labels then tex.draw_texture(self.t_judge_labels, { index = p_idx }) end

    self:draw_score_info()
    self:draw_total_score()

    if self.high_score_obj then self.high_score_obj:draw() end
    if self.rank_obj then self.rank_obj:draw(tex.get_current_ms()) end
    if self.crown_obj then self.crown_obj:draw() end
    if self.crown_msg_obj then self.crown_msg_obj:draw() end

end

local CHARA_SCALE = 1.12
local CHARA_X     = 204
local CHARA_Y     = 941
local CHARA_X_2P  = 1649

function ResultPlayer:chara_pos()
    return { self.is_2p and CHARA_X_2P or CHARA_X, CHARA_Y, CHARA_SCALE }
end

local NAMEPLATE_X    = 2
local NAMEPLATE_Y    = 922
local NAMEPLATE_X_2P = 1509

function ResultPlayer:nameplate_pos()
    return { self.is_2p and NAMEPLATE_X_2P or NAMEPLATE_X, NAMEPLATE_Y }
end

function ResultPlayer:draw_gauge()
    if not self.gauge_started then return end

    local p_idx     = self.is_2p and 1 or 0
    local scale     = GAUGE_SCALE
    local base_fade = 1.0
    local gl_int    = self.gauge_shown
    local clear_pts = { 30, 35, 40 }   -- easy / normal+hard / oni norma cells
    local clear_pt  = clear_pts[self.art_index + 1]
    local u         = GAUGE_UNIT

    if self.t_gauge_unfilled then
        tex.draw_texture(self.t_gauge_unfilled, { scale = scale, fade = base_fade, index = p_idx })
    end

    if self.gauge_state == 2 and gl_int >= GAUGE_CELLS then
        local drew_rainbow = false
        if self.t_gauge_rainbow and RBW.tracks and RBW_LEN > 0 and self.rainbow_start_ms >= 0 then
            local f  = math.fmod((tex.get_current_ms() - self.rainbow_start_ms) * 0.06, RBW_LEN)
            local fi = math.floor(f)
            for i = 1, #RBW_LEAVES do
                local rows = RBW.tracks[RBW_LEAVES[i][1]]
                if rows and #rows > 0 and fi >= rows[1][1] and fi <= rows[#rows][1] then
                    local a = RBW:get(RBW_LEAVES[i][1], f, "a") or 0.0
                    if a > 0.0 then
                        tex.draw_texture(self.t_gauge_rainbow,
                            { frame = RBW_LEAVES[i][2], scale = scale, fade = base_fade * a, index = p_idx })
                        drew_rainbow = true
                    end
                end
            end
        end
        if not drew_rainbow and self.t_gauge_rainbow then
            local fa = math.floor(self.rainbow_frac) % 8
            local fb = (fa + 1) % 8
            local t  = self.rainbow_frac - math.floor(self.rainbow_frac)
            tex.draw_texture(self.t_gauge_rainbow, { frame = fa, scale = scale, fade = base_fade, index = p_idx })
            tex.draw_texture(self.t_gauge_rainbow, { frame = fb, scale = scale, fade = base_fade * t, index = p_idx })
        end
    else
        if gl_int > 0 and self.t_gauge_bar then
            local n = math.min(gl_int, clear_pt - 1)
            tex.draw_texture(self.t_gauge_bar, { scale = scale, x2 = (n - 1) * u, fade = base_fade, index = p_idx })
        end
        if gl_int >= clear_pt - 1 and self.gauge_state >= 1 and self.t_gauge_bar_clear_transition then
            tex.draw_texture(self.t_gauge_bar_clear_transition,
                { scale = scale, x = (clear_pt - 1) * u, fade = base_fade, index = p_idx })
        end
        if gl_int > clear_pt and self.gauge_state >= 1 then
            local extra = (gl_int - clear_pt - 1) * u
            if self.t_gauge_bar_clear_top then
                tex.draw_texture(self.t_gauge_bar_clear_top,
                    { scale = scale, x = clear_pt * u, x2 = extra, fade = base_fade, index = p_idx })
            end
            if self.t_gauge_bar_clear_bottom then
                tex.draw_texture(self.t_gauge_bar_clear_bottom,
                    { scale = scale, x = clear_pt * u, x2 = extra, fade = base_fade, index = p_idx })
            end
        end
    end

    if self.t_gauge_overlay then
        tex.draw_texture(self.t_gauge_overlay, { scale = scale, fade = math.min(0.15, base_fade), index = p_idx })
    end

    local clear_idx = self.art_index + p_idx * 3
    if gl_int >= clear_pt - 1 and self.gauge_state >= 1 then
        if self.t_gauge_clear then
            tex.draw_texture(self.t_gauge_clear, { scale = scale, fade = base_fade, index = clear_idx })
        end
        local fire_sheen = nil
        if self.gauge_state == 2 and gl_int >= GAUGE_CELLS and self.t_gauge_tamashii_fire then
            local ff, fa2, sheen_a = fire_state(self.rainbow_start_ms, tex.get_current_ms())
            if ff then
                fire_sheen = sheen_a
                tex.draw_texture(self.t_gauge_tamashii_fire,
                    { frame = ff, scale = 0.8 * scale, fade = fa2 * base_fade, index = p_idx })
            else
                -- fail-soft: engine animation ladder
                local fire_f = math.floor(self.tamashii_fire.attribute)
                tex.draw_texture(self.t_gauge_tamashii_fire,
                    { frame = fire_f, scale = 0.8 * scale, fade = base_fade, index = p_idx })
            end
        end
        if self.t_gauge_tamashii then
            tex.draw_texture(self.t_gauge_tamashii, { scale = scale, fade = base_fade, index = p_idx })
        end
        if self.gauge_state == 2 and gl_int >= GAUGE_CELLS and self.t_gauge_tamashii_overlay then
            if fire_sheen ~= nil then
                if fire_sheen > 0.0 then
                    tex.draw_texture(self.t_gauge_tamashii_overlay,
                        { scale = scale, fade = math.min(fire_sheen, base_fade), index = p_idx })
                end
            else
                local f = math.floor(self.tamashii_fire.attribute)
                if f == 0 or f == 1 or f == 4 or f == 5 then
                    tex.draw_texture(self.t_gauge_tamashii_overlay,
                        { scale = scale, fade = math.min(0.5, base_fade), index = p_idx })
                end
            end
        end
    else
        if self.t_gauge_clear_dark then
            tex.draw_texture(self.t_gauge_clear_dark, { scale = scale, fade = base_fade, index = clear_idx })
        end
        if self.t_gauge_tamashii_dark then
            tex.draw_texture(self.t_gauge_tamashii_dark, { scale = scale, fade = base_fade, index = p_idx })
        end
    end
end

function ResultPlayer:draw_score_info()
    -- judge rows top-down: good, ok, bad, drumroll, max combo
    local scores = { self.good, self.ok, self.bad, self.total_drumroll, self.max_combo }
    local p_idx  = self.is_2p and 1 or 0
    for j, s in ipairs(scores) do
        if s ~= "" and self.t_judge_num then
            local pop = pop_scale(digit_judge, DGJ_TRACK, DGJ_F0, DGJ_F1,
                                  self.row_land_ms[j], self.now_ms or 0)
            local n   = math.floor(tonumber(s) or 0)
            local str = tostring(n)
            for i = 1, #str do
                local d = tonumber(string.sub(str, #str - i + 1, #str - i + 1)) or 0
                tex.draw_texture(self.t_judge_num, {
                    frame  = d,
                    x      = -((i - 1) * self.sc_score_info_margin),
                    index  = (j - 1) + p_idx * 5,
                    scale  = pop,
                    center = true,
                })
            end
        end
    end
end

function ResultPlayer:draw_total_score()
    local p_idx = self.is_2p and 1 or 0
    if self.score_method then
        if self.t_score_shinuchi_lbl then tex.draw_texture(self.t_score_shinuchi_lbl, { index = p_idx }) end
    else
        if self.t_score_lbl then tex.draw_texture(self.t_score_lbl, { index = p_idx }) end
    end
    if not self.fade_in_finished_flag then return end
    if self.score ~= "" and self.t_score_num then
        local pop = pop_scale(digit_score, DGS_TRACK, DGS_F0, DGS_F1,
                              self.score_land_ms, self.now_ms or 0)
        local n   = math.floor(tonumber(self.score) or 0)
        local str = tostring(n)
        -- score_num frames 0..9 outline, 10..19 fill
        for layer = 0, 10, 10 do
            for i = 1, #str do
                local d = tonumber(string.sub(str, #str - i + 1, #str - i + 1)) or 0
                tex.draw_texture(self.t_score_num, {
                    frame  = d + layer,
                    x      = -((i - 1) * self.sc_result_score_margin),
                    index  = p_idx,
                    scale  = pop,
                    center = true,
                })
            end
        end
    end
end
