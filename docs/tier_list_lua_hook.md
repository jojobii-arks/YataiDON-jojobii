-- Snippet for Skins/YataiDONNijiiro/Scripts/song_select/song_select.lua
-- The skin submodule is not checked out in this worktree; wire this into
-- draw_box (or wherever course stars are drawn) once the skin is available.
--
-- SongBox fields (C++ Lua bindings):
--   box:tier()          -> primary letter (ura if ura-mode + ura tier, else oni)
--   box:tier_oni()      -> oni letter or ""
--   box:tier_ura()      -> ura letter or ""
--   box:tier_for(diff)  -> letter for course 3/4
--   box:course_info(d).tier -> same as tier_for(d)
--
-- Minimal draw example (guard missing / empty):

--[[
local function draw_tier_label(box, x, y, fade)
    if not box or box:lua_kind() ~= "song" then return end
    local letter = box:tier()
    if not letter or letter == "" then return end
    -- Use whatever text helper the skin already uses for star/rank labels:
    -- draw_text(letter, x, y, fade)  -- placeholder
end
]]
