#include "pro_lua_bindings.h"

#include "pro_list.h"
#include "../../../libs/script.h"

namespace {

const char* context_name(ProListModel::Context c) {
    switch (c) {
        case ProListModel::Context::ROOT:     return "root";
        case ProListModel::Context::FOLDER:   return "folder";
        case ProListModel::Context::RECENT:   return "recent";
        case ProListModel::Context::FAVORITE: return "favorite";
        case ProListModel::Context::ALL:      return "all";
        case ProListModel::Context::SEARCH:   return "search";
    }
    return "root";
}

const char* row_kind_name(ProListModel::RowKind k) {
    switch (k) {
        case ProListModel::RowKind::FOLDER:   return "folder";
        case ProListModel::RowKind::SEARCH:   return "search";
        case ProListModel::RowKind::RECENT:   return "recent";
        case ProListModel::RowKind::FAVORITE: return "favorite";
        case ProListModel::RowKind::ALL:      return "all";
        case ProListModel::RowKind::CHART:    return "chart";
        case ProListModel::RowKind::BACK:     return "back";
        case ProListModel::RowKind::FILTER:   return "filter";
        case ProListModel::RowKind::DAN_FOLDER: return "danfolder";
        case ProListModel::RowKind::DAN:      return "dan";
    }
    return "chart";
}

int wrap_index(int i, int n) { return ((i % n) + n) % n; }

sol::table chart_info_table(sol::state& lua, const ProListModel::ChartInfo& info) {
    sol::table t = lua.create_table();
    t["title"]     = info.title;
    t["subtitle"]  = info.subtitle;
    t["course"]    = info.course;
    t["level"]     = info.level;
    t["genre_index"] = info.genre_index;
    t["tier"]      = info.tier;
    t["played"]    = info.played;
    t["score"]     = info.score;
    t["crown"]     = info.crown;
    t["rank"]      = info.rank;
    t["good"]      = info.good;
    t["ok"]        = info.ok;
    t["miss"]      = info.miss;
    t["favorite"]  = info.favorite;
    t["branching"] = info.branching;
    return t;
}

}  // namespace

void register_pro_lua_bindings(sol::state& lua) {
    lua.new_usertype<ProListModel>("ProList",
        // Whole-view state, once per frame.
        "state", [](ProListModel& m) {
            sol::state& l = *script_manager.lua;
            sol::table t = l.create_table();
            t["context"]      = context_name(m.context());
            t["label"]        = m.context_label();
            t["genre_index"]  = m.context_genre_index();
            t["ready"]        = m.ready();
            t["building"]     = m.building();
            t["progress"]     = m.build_progress();
            t["cursor"]       = m.cursor();
            t["rows"]         = m.row_count();
            t["charts"]       = m.chart_row_count();
            t["sort"]         = m.sort_name();
            const ProListModel::Filter& f = m.filter();
            sol::table diff = l.create_table(5, 0);
            for (int i = 0; i < 5; i++) diff[i + 1] = f.diff[i];
            t["diff"]         = diff;
            t["star"]         = f.star;
            t["tier"]         = f.tier;
            t["selected"]     = m.selected();
            t["control"]      = m.control_index();
            t["filter_open"]  = m.filter_open();
            t["filter_cursor"] = m.filter_cursor();   // entry 0..4 (ENTRY) / entered entry (GROUP)
            t["filter_focus"] = std::string(m.filter_focus_name());   // "entry" | "group"
            t["filter_value"] = m.filter_value_cursor();              // GROUP: value index
            t["active_course"] = m.active_course();
            t["dan_context"]  = m.dan_context();
            t["picker_open"]  = m.picker_open();
            t["picker"]       = m.picker_cursor();
            return t;
        },
        // One row by offset from the cursor (wraps like the list).
        "row", [](ProListModel& m, double offset) -> sol::object {
            sol::state& l = *script_manager.lua;
            const int n = m.row_count();
            if (n == 0) return sol::lua_nil;
            const int idx = wrap_index(m.cursor() + (int)offset, n);
            const ProListModel::Row& r = m.row_at(idx);
            sol::table t = l.create_table();
            t["kind"]       = row_kind_name(r.kind);
            t["is_cursor"]  = idx == m.cursor();
            t["name"]       = r.name;
            t["title"]      = r.title;
            t["subtitle"]   = r.subtitle;
            t["course"]     = r.course;
            t["level"]      = r.level;
            t["genre_index"] = r.genre_index;
            t["count"]      = r.chart_count;
            t["played"]     = r.played;
            if (r.kind == ProListModel::RowKind::DAN_FOLDER || r.kind == ProListModel::RowKind::DAN) {
                t["dan_count"] = r.dan_count;
                if (r.dan) {
                    t["dan_color"]     = r.dan->color;
                    t["dan_rank_art"]  = r.dan->rank_art;
                    t["dan_era"]       = r.dan->era;
                    t["dan_songs"]     = (int)r.dan->songs.size();
                    t["dan_earned"]    = r.dan->earned;
                    t["dan_gaiden"]    = r.dan->gaiden;
                }
            }
            return t;
        },
        // Resolved chart info for a row (honours the CHANGE DIFF swap) — the
        // card's art / score / tier fields and the details panel.
        "info", [](ProListModel& m, double offset) -> sol::object {
            sol::state& l = *script_manager.lua;
            const int n = m.row_count();
            if (n == 0) return sol::lua_nil;
            const int idx = wrap_index(m.cursor() + (int)offset, n);
            ProListModel::ChartInfo info = m.resolve_display(idx);
            if (!info.found) return sol::lua_nil;
            return chart_info_table(l, info);
        },
        // Filter/Sort dialog data.
        "tier_set", [](ProListModel&, double level) {
            sol::state& l = *script_manager.lua;
            std::vector<std::string> set = ProListModel::tier_set_for_level((int)level);
            sol::table t = l.create_table((int)set.size(), 0);
            for (size_t i = 0; i < set.size(); i++) t[(int)i + 1] = set[i];
            return t;
        },
        "ladder", [](ProListModel&) {
            sol::state& l = *script_manager.lua;
            const std::vector<std::string>& set = ProListModel::ladder_hard_to_blank();
            sol::table t = l.create_table((int)set.size(), 0);
            for (size_t i = 0; i < set.size(); i++) t[(int)i + 1] = set[i];
            return t;
        },
        // One dan row's full display data — the details panel for a course:
        // title / era / color / rank art / exams / the song list (with each
        // entry's hidden flag, so the panel can mirror the dojo's ???) /
        // the player's earned rank.
        "dan", [](ProListModel& m, double offset) -> sol::object {
            sol::state& l = *script_manager.lua;
            const int n = m.row_count();
            if (n == 0) return sol::lua_nil;
            const int idx = wrap_index(m.cursor() + (int)offset, n);
            const ProListModel::DanLite* d = m.row_dan(idx);
            if (!d) return sol::lua_nil;
            sol::table t = l.create_table();
            t["title"]      = d->title;
            t["era"]        = d->era;
            t["color"]      = d->color;
            t["rank_art"]   = d->rank_art;
            t["gaiden"]     = d->gaiden;
            t["earned"]     = d->earned;
            t["focus_path"] = d->json_path.string();
            sol::table songs = l.create_table((int)d->songs.size(), 0);
            for (size_t i = 0; i < d->songs.size(); ++i) {
                sol::table s = l.create_table();
                s["title"]      = d->songs[i].title;
                s["subtitle"]   = d->songs[i].subtitle;
                s["difficulty"] = d->songs[i].difficulty;
                s["hidden"]     = d->songs[i].hidden;
                songs[(int)i + 1] = s;
            }
            t["songs"] = songs;
            sol::table exams = l.create_table((int)d->exams.size(), 0);
            for (size_t i = 0; i < d->exams.size(); ++i) {
                sol::table x = l.create_table();
                x["type"]  = d->exams[i].type;
                x["range"] = d->exams[i].range;
                x["v1"]    = d->exams[i].v1;
                x["v2"]    = d->exams[i].v2;
                exams[(int)i + 1] = x;
            }
            t["exams"] = exams;
            return t;
        },
        // The CHANGE DIFF picker: one cell per course, mark / level / tier.
        "picker_cells", [](ProListModel& m) {
            sol::state& l = *script_manager.lua;
            std::array<ProListModel::PickerCell, 5> cells = m.picker_cells();
            sol::table t = l.create_table(5, 0);
            for (int i = 0; i < 5; i++) {
                sol::table c = l.create_table();
                c["has_course"] = cells[i].has_course;
                c["level"]      = cells[i].level;
                c["tier"]       = cells[i].tier;
                t[i + 1] = c;
            }
            return t;
        }
    );
}