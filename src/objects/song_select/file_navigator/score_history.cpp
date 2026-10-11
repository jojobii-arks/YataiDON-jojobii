#include "score_history.h"

ScoreHistory::ScoreHistory(const std::array<std::optional<Score>, 5>& scores, double current_ms)
{
    if (!script_manager.lua) return;

    sol::state& lua = *script_manager.lua;
    sol::table available = lua.create_table();
    int n = 1;
    for (int i = 0; i < 5; i++) {
        if (!scores[i].has_value()) continue;
        const Score& s = *scores[i];
        sol::table entry = lua.create_table();
        entry["diff"]     = i;
        entry["score"]    = s.score;
        entry["good"]     = s.good;
        entry["ok"]       = s.ok;
        entry["bad"]      = s.bad;
        entry["drumroll"] = s.drumroll;
        available[n++] = entry;
    }

    bool is_shinuchi = global_data.config->general.score_method == ScoreMethod::SHINUCHI;

    if (!load("ScoreHistory", "score_history", available, is_shinuchi, current_ms)) return;
    fn_update = lua_object["update"];
    fn_draw   = lua_object["draw"];
}

void ScoreHistory::update(double current_ms) { call(fn_update, "ScoreHistory:update", current_ms); }
void ScoreHistory::draw()                    { call(fn_draw,   "ScoreHistory:draw"); }
