#include "pro_list.h"

#include "../file_navigator/navigator.h"
#include "../../../libs/filesystem.h"
#include "../../../libs/global_data.h"
#include "../../../libs/scores.h"
#include "../../../libs/tier_list.h"

#include <algorithm>
#include <cctype>

// Course order is 0 Easy, 1 Normal, 2 Hard, 3 Oni, 4 Ura (SPEC 4).
static constexpr int kOniCourse = 3;
static constexpr int kUraCourse = 4;
static constexpr int kRowsPerPair = 8;   // 8 charts + BACK + FILTER = the settled 10-row cycle

namespace {
std::string lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}
}  // namespace

// ---- tier helpers ---------------------------------------------------------

int ProListModel::tier_letter_rank(const std::string& letter) {
    // Settled ladder, hardest to blank: SSS SS S+ S A B C D E F X, then None.
    // X deliberately sorts below F; 0 = tier-less.
    static const std::unordered_map<std::string, int> ranks = {
        {"SSS", 11}, {"SS", 10}, {"S+", 9}, {"S", 8}, {"A", 7}, {"B", 6},
        {"C", 5},    {"D", 4},   {"E", 3},  {"F", 2}, {"X", 1},
    };
    auto it = ranks.find(letter);
    return it == ranks.end() ? 0 : it->second;
}

int ProListModel::ladder_index(const std::string& letter) {
    return tier_letter_rank(letter);
}

const std::vector<std::string>& ProListModel::ladder_hard_to_blank() {
    static const std::vector<std::string> ladder = {
        "SSS", "SS", "S+", "S", "A", "B", "C", "D", "E", "F", "X",
    };
    return ladder;
}

std::vector<std::string> ProListModel::tier_set_for_level(int level) {
    // The per-level sets are NOT monotonic (★6 has neither B nor D) — listed
    // explicitly, in ladder order within the set (SPEC 3, data/tier_data.json).
    switch (level) {
        case 6:  return {"S", "A", "C", "E", "F"};
        case 7:  return {"S", "A", "B", "C", "D", "E", "F"};
        case 8:  return {"S", "A", "B", "C", "D", "E", "F"};
        case 9:  return {"S", "A", "B", "C", "D", "E", "F", "X"};
        case 10: return {"SSS", "SS", "S+", "S", "A", "B", "C", "D", "E", "F", "X"};
        default: return {};
    }
}

bool ProListModel::tier_in_level_set(int level, const std::string& letter) {
    if (level <= 0) return letter.empty();   // ALL star: anything the ladder has
    for (const auto& t : tier_set_for_level(level)) {
        if (t == letter) return true;
    }
    return false;
}

// ---- tier / filter logic --------------------------------------------------

std::string ProListModel::tier_for_chart(const fs::path& song_path, int course, int level) const {
    // Tiers exist only at ★6-10, Oni / Ura. Everything else is tier-less.
    if (course != kOniCourse && course != kUraCourse) return {};
    if (level < 6 || level > 10) return {};
    const auto& keys = nav_->library_song_tier_keys();
    auto it = keys.find(song_path.string());
    if (it == keys.end()) return {};
    return tier_list.tier_for_first(it->second, course, level);
}

bool ProListModel::chart_passes_filter(const fs::path& song_path, int course, int level,
                                       const std::string& tier) const {
    if (course < 0 || course > 4) return false;
    if (level < 1 || level > 10) return false;       // -1 = course absent
    if (!filter_.diff[course]) return false;
    if (filter_.star != 0 && level != filter_.star) return false;
    if (!filter_.tier.empty()) {
        const std::string& t = tier;
        if (filter_.tier == "NONE") {
            if (!t.empty()) return false;            // None wants the tier-less charts
        } else if (t != filter_.tier) {
            return false;
        }
    }
    return true;
}

// ---- contexts -------------------------------------------------------------

void ProListModel::reset_root() {
    selected_ = false;
    swap_course_.reset();
    context_ = Context::ROOT;
    folder_path_.clear();
    context_genre_ = 0;
    context_label_ = "root";
    begin_build();
}

void ProListModel::open_folder_row(int row_index) {
    if (row_index < 0 || row_index >= (int)rows_.size()) return;
    const Row& r = rows_[row_index];
    selected_ = false;
    swap_course_.reset();
    switch (r.kind) {
        case RowKind::FOLDER:   context_ = Context::FOLDER;   folder_path_ = r.folder_path; break;
        case RowKind::RECENT:   context_ = Context::RECENT;   folder_path_.clear(); break;
        case RowKind::FAVORITE: context_ = Context::FAVORITE; folder_path_.clear(); break;
        case RowKind::ALL:      context_ = Context::ALL;      folder_path_.clear(); break;
        case RowKind::SEARCH:   context_ = Context::SEARCH;   folder_path_.clear(); break;
        default: return;   // chart / back / filter rows do not open contexts
    }
    context_label_ = r.name.empty() ? r.title : r.name;   // the header names what is open
    context_genre_ = r.genre_index;   // FOLDER rows carry theirs; collections fall through
    if (context_ == Context::RECENT) context_genre_ = 9;        // recent ref frame
    else if (context_ != Context::FOLDER) context_genre_ = 0;   // default ref frame
    begin_build();
}

void ProListModel::back_to_root() {
    selected_ = false;
    swap_course_.reset();
    context_ = Context::ROOT;
    folder_path_.clear();
    context_genre_ = 0;
    context_label_ = "root";
    begin_build();
}

// ---- building -------------------------------------------------------------

void ProListModel::begin_build() {
    building_ = true;
    phase_ = Phase::SONGS;
    cand_.clear();
    build_pos_ = 0;
    cursor_ = 0;                 // a new context starts at the top row (not the last cursor)
    collect_context_songs();

    if (context_ == Context::ROOT) {
        build_root_rows();
        building_ = false;
        phase_ = Phase::IDLE;
        cursor_ = 0;
    }
}

float ProListModel::build_progress() const {
    if (context_ == Context::ROOT || build_songs_.empty()) return 1.0f;
    return (float)build_pos_ / (float)build_songs_.size();
}

void ProListModel::collect_context_songs() {
    build_songs_.clear();
    if (context_ == Context::ROOT) return;

    const auto& files = nav_->library_song_files();
    if (context_ == Context::FOLDER) {
        for (const auto& [key, path] : files) {
            std::error_code ec;
            fs::path rel = path.lexically_relative(folder_path_);
            if (rel.empty()) continue;
            auto first = rel.begin();
            if (first == rel.end() || *first == "..") continue;
            build_songs_.emplace_back(key, path);
        }
        return;
    }
    if (context_ == Context::ALL) {
        for (const auto& [key, path] : files) build_songs_.emplace_back(key, path);
        return;
    }
    if (context_ == Context::RECENT || context_ == Context::FAVORITE) {
        // The collection's own order — the song_list.txt the engine maintains.
        const std::optional<fs::path>* dir =
            context_ == Context::RECENT ? &nav_->library_recent_folder_path()
                                        : &nav_->library_favorite_folder_path();
        if (!dir->has_value()) return;
        fs::path list_path = dir->value() / "song_list.txt";
        for (const SongListEntry& e : read_song_list(list_path)) {
            auto it = files.find({e.title, e.subtitle});
            if (it != files.end()) build_songs_.emplace_back(it->first, it->second);
        }
        return;
    }
    // SEARCH: fold the query exactly like the wheel, then substring-match the
    // already-folded per-song search text.
    if (context_ == Context::SEARCH) {
        if (search_query_.empty()) return;
        const std::string query = search_fold(search_query_);
        if (query.empty()) return;
        const auto& texts = nav_->library_song_search_text();
        for (const auto& [key, path] : files) {
            auto it = texts.find(path.string());
            const std::string& text = (it != texts.end()) ? it->second : search_fold(key.first);
            if (text.find(query) != std::string::npos) build_songs_.emplace_back(key, path);
        }
        return;
    }
}

void ProListModel::update() {
    if (!building_ || context_ == Context::ROOT) return;
    if (phase_ == Phase::SONGS) {
        build_songs_step(2500);
        if (build_pos_ >= build_songs_.size()) phase_ = Phase::SORT;
    } else if (phase_ == Phase::SORT) {
        build_sort_and_assemble();
        building_ = false;
        phase_ = Phase::IDLE;
    }
}

void ProListModel::build_songs_step(size_t budget) {
    const auto& levels_map = nav_->library_song_levels();
    const bool need_tier = !filter_.tier.empty() || sort_ == Sort::TIER;
    size_t done = 0;
    while (build_pos_ < build_songs_.size() && done < budget) {
        const auto& [key, path] = build_songs_[build_pos_];
        ++build_pos_;
        ++done;

        auto lv_it = levels_map.find(path.string());
        if (lv_it == levels_map.end()) continue;
        const std::array<int, 5>& levels = lv_it->second;

        // Scores are resolved per chart once here: the card shows them and
        // score / crown sorts need them — the cache behind get_score is loaded.
        std::array<std::string, 5> hashes;
        bool hashes_loaded = false;

        for (int course = 0; course < 5; ++course) {
            int level = levels[course];
            if (level < 1 || level > 10) continue;
            if (!filter_.diff[course]) continue;
            if (filter_.star != 0 && level != filter_.star) continue;

            std::string tier;
            if (need_tier || !filter_.tier.empty())
                tier = tier_for_chart(path, course, level);
            if (!chart_passes_filter(path, course, level, tier)) continue;

            RawRow rr;
            rr.song_path = path;
            rr.title = key.first;
            rr.subtitle = key.second;
            rr.course = course;
            rr.level = level;
            rr.tier_rank = tier_letter_rank(tier);

            if (!hashes_loaded) {
                hashes = scores_manager.get_hashes(path);
                hashes_loaded = true;
            }
            const std::string& hash = hashes[course];
            if (!hash.empty()) {
                if (auto s = scores_manager.get_score(hash, course, scores_manager.player_1)) {
                    rr.played = true;
                    rr.score  = s->score;
                    rr.crown  = (int)s->crown;
                }
            }
            cand_.push_back(std::move(rr));
        }
    }
}

void ProListModel::build_sort_and_assemble() {
    const Sort mode = sort_;
    std::stable_sort(cand_.begin(), cand_.end(), [mode](const RawRow& a, const RawRow& b) {
        // Charts never played sort LAST in every mode — never dropped.
        if (a.played != b.played) return a.played;
        switch (mode) {
            case Sort::SCORE: if (a.score != b.score) return a.score > b.score; break;
            case Sort::CROWN: if (a.crown != b.crown) return a.crown > b.crown; break;
            case Sort::TIER:  if (a.tier_rank != b.tier_rank) return a.tier_rank > b.tier_rank; break;
            case Sort::ALPHA: break;
        }
        // Tie-break and the alphabetical mode: title A-Z, then subtitle, then course.
        std::string at = lower_ascii(a.title), bt = lower_ascii(b.title);
        if (at != bt) return at < bt;
        if (a.subtitle != b.subtitle) return a.subtitle < b.subtitle;
        return a.course < b.course;
    });
    assemble_rows();
}

void ProListModel::assemble_rows() {
    rows_.clear();
    rows_.reserve(cand_.size() + (cand_.size() / kRowsPerPair + 1) * 2);
    auto push_pair = [&]() {
        Row back;  back.kind = RowKind::BACK;
        Row filter; filter.kind = RowKind::FILTER;
        rows_.push_back(std::move(back));
        rows_.push_back(std::move(filter));
    };

    if (cand_.empty()) {
        // A zero-result filter is a real state: the pair stays so there is
        // always a way out of the list.
        push_pair();
    } else {
        int since_pair = 0;
        for (RawRow& rr : cand_) {
            Row row;
            row.kind = RowKind::CHART;
            row.song_path = std::move(rr.song_path);
            row.title = std::move(rr.title);
            row.subtitle = std::move(rr.subtitle);
            row.course = rr.course;
            row.level = rr.level;
            row.played = rr.played;
            row.score = rr.score;
            row.crown = rr.crown;
            rows_.push_back(std::move(row));
            if (++since_pair == kRowsPerPair) {
                push_pair();
                since_pair = 0;
            }
        }
        if (since_pair != 0) {
            // Trailing partial group: keep a pair at the end so a short list
            // still has BACK / FILTER reachable without wrapping far.
            push_pair();
        }
    }
    if (cursor_ < 0 || cursor_ >= (int)rows_.size()) cursor_ = 0;
}

void ProListModel::build_root_rows() {
    rows_.clear();
    cursor_ = 0;

    // Counts: one pass over the library. The root rows carry the folder's
    // unfiltered chart-line count (filter and sort never touch the root).
    std::unordered_map<std::string, int> folder_counts;   // folder path string -> charts
    int all_count = 0;
    const auto& files = nav_->library_song_files();
    const auto& levels_map = nav_->library_song_levels();
    for (const auto& [key, path] : files) {
        auto lv_it = levels_map.find(path.string());
        if (lv_it == levels_map.end()) continue;
        int charts = 0;
        for (int level : lv_it->second) {
            if (level >= 1 && level <= 10) ++charts;
        }
        if (charts == 0) continue;
        all_count += charts;
        for (const fs::path& root : global_data.config->paths.tja_path) {
            std::error_code ec;
            fs::path rel = path.lexically_relative(root);
            if (rel.empty()) continue;
            auto first = rel.begin();
            if (first == rel.end() || *first == "..") continue;
            folder_counts[(root / *first).string()] += charts;
            break;
        }
    }

    auto count_for = [&](const std::optional<fs::path>& folder) -> int {
        if (!folder.has_value()) return 0;
        int total = 0;
        for (const SongListEntry& e : read_song_list(folder.value() / "song_list.txt")) {
            std::string key = e.title + "|" + e.subtitle;
            auto it = files.find({e.title, e.subtitle});
            if (it == files.end()) continue;
            auto lv_it = levels_map.find(it->second.string());
            if (lv_it == levels_map.end()) continue;
            for (int level : lv_it->second)
                if (level >= 1 && level <= 10) ++total;
        }
        return total;
    };

    Row r;
    r.kind = RowKind::SEARCH;   r.name = "SEARCH";
    rows_.push_back(r);
    rows_.back().chart_count = all_count;   // the pill reads the library total on SEARCH
    r = Row(); r.kind = RowKind::RECENT;   r.name = "RECENT";
    r.chart_count = count_for(nav_->library_recent_folder_path());   rows_.push_back(r);
    r = Row(); r.kind = RowKind::FAVORITE; r.name = "FAVORITE";
    r.chart_count = count_for(nav_->library_favorite_folder_path()); rows_.push_back(r);
    r = Row(); r.kind = RowKind::ALL;      r.name = "ALL";
    r.chart_count = all_count; rows_.push_back(r);

    // Genres: the same walk the navigator's root loader does — top-level
    // folders with a box.def, in disk order; collection folders are already
    // represented by the fixed rows above, so they are skipped here.
    for (const fs::path& root : global_data.config->paths.tja_path) {
        std::error_code ec;
        for (const fs::directory_entry& entry : fs::directory_iterator(root, ec)) {
            if (ec) break;
            if (!fs::is_directory(entry.path(), ec)) continue;
            if (!nav_->library_has_def_file(entry.path())) continue;
            BoxDef def = nav_->parse_box_def(entry.path());
            if (!def.collection.empty()) continue;   // NEW / RECENT / FAVORITE / ...
            if (def.genre_index == GenreIndex::DAN && hide_dan_) continue;

            Row g;
            g.kind = RowKind::FOLDER;
            g.name = def.name;
            g.folder_path = entry.path();
            g.genre_index = (int)def.genre_index;
            auto c_it = folder_counts.find(entry.path().string());
            g.chart_count = c_it == folder_counts.end() ? 0 : c_it->second;
            rows_.push_back(std::move(g));
        }
    }
}

// ---- cursor ---------------------------------------------------------------

void ProListModel::move_cursor(int delta) {
    if (rows_.empty()) return;
    int n = (int)rows_.size();
    cursor_ = ((cursor_ + delta) % n + n) % n;
}

void ProListModel::skip_cursor(int dir) {
    move_cursor(10 * (dir >= 0 ? 1 : -1));
}

void ProListModel::set_cursor(int index) {
    if (rows_.empty()) { cursor_ = 0; return; }
    int n = (int)rows_.size();
    cursor_ = ((index % n) + n) % n;
}

// ---- filter / sort --------------------------------------------------------

void ProListModel::set_sort(Sort s) {
    if (sort_ == s) return;
    sort_ = s;
    if (context_ != Context::ROOT) begin_build();
}

void ProListModel::filter_changed() {
    if (context_ != Context::ROOT) begin_build();
}

const char* ProListModel::sort_name() const {
    switch (sort_) {
        case Sort::SCORE: return "SCORE";
        case Sort::CROWN: return "CROWN";
        case Sort::ALPHA: return "A-Z";
        case Sort::TIER:  return "TIER";
    }
    return "A-Z";
}

int ProListModel::chart_row_count() const {
    int n = 0;
    for (const Row& r : rows_) {
        if (r.kind == RowKind::CHART) ++n;
    }
    return n;
}

// ---- resolved info --------------------------------------------------------

ProListModel::ChartInfo ProListModel::resolve(int row_index) const {
    ChartInfo info;
    if (row_index < 0 || row_index >= (int)rows_.size()) return info;
    const Row& r = rows_[row_index];
    if (r.kind != RowKind::CHART) return info;

    info.found = true;
    info.title = r.title;
    info.subtitle = r.subtitle;
    info.course = r.course;
    info.level = r.level;
    {
        int gi = 0;
        std::string gl;
        if (song_genre(r.song_path, gi, gl)) info.genre_index = gi;
    }
    info.tier = tier_for_chart(r.song_path, r.course, r.level);
    info.played = r.played;
    info.score = r.score;
    info.crown = r.crown;
    info.rank = 0;

    std::array<std::string, 5> hashes = scores_manager.get_hashes(r.song_path);
    const std::string& hash = hashes[r.course];
    if (!hash.empty()) {
        if (auto s = scores_manager.get_score(hash, r.course, scores_manager.player_1)) {
            info.played = true;
            info.score = s->score;
            info.crown = (int)s->crown;
            info.rank = (int)s->rank;
            info.good = s->good;
            info.ok = s->ok;
            info.miss = s->bad;
        }
    }
    const auto& favs = nav_->library_favorite_songs();
    info.favorite = favs.count(r.title + "|" + r.subtitle) > 0;
    info.branching = branch_flag(r.song_path, r.course);
    return info;
}

ProListModel::ChartInfo ProListModel::resolve_display(int row_index) const {
    ChartInfo info = resolve(row_index);
    if (!info.found || !selected_ || !swap_course_.has_value()) return info;
    const int course = *swap_course_;
    if (course == info.course) return info;
    const Row& r = rows_[row_index];
    if (r.kind != RowKind::CHART) return info;

    info.course = course;
    info.level = 0;
    const auto& levels_map = nav_->library_song_levels();
    auto lv_it = levels_map.find(r.song_path.string());
    if (lv_it != levels_map.end()) info.level = lv_it->second[course];
    info.tier = tier_for_chart(r.song_path, course, info.level);
    info.played = false;
    info.score = 0; info.crown = 0; info.rank = 0;
    info.good = 0; info.ok = 0; info.miss = 0;
    std::array<std::string, 5> hashes = scores_manager.get_hashes(r.song_path);
    const std::string& hash = hashes[course];
    if (!hash.empty()) {
        if (auto s = scores_manager.get_score(hash, course, scores_manager.player_1)) {
            info.played = true;
            info.score = s->score;
            info.crown = (int)s->crown;
            info.rank = (int)s->rank;
            info.good = s->good;
            info.ok = s->ok;
            info.miss = s->bad;
        }
    }
    info.branching = branch_flag(r.song_path, course);
    return info;
}

bool ProListModel::branch_flag(const fs::path& song_path, int course) const {
    const std::string key = song_path.string();
    auto it = branch_cache_.find(key);
    if (it == branch_cache_.end()) {
        std::array<uint8_t, 5> flags{};   // 1 = yes, 2 = no, 0 = unknown-but-checked
        try {
            SongParser parser(song_path);
            parser.get_metadata();
            for (const auto& [c, data] : parser.metadata.course_data) {
                if (c >= 0 && c < 5) flags[c] = data.is_branching ? 1 : 2;
            }
        } catch (const std::exception&) {
            // leave all unknown; treated as "no branch info"
        }
        it = branch_cache_.emplace(key, flags).first;
    }
    if (course < 0 || course > 4) return false;
    return it->second[course] == 1;
}

// ---- selected-chart state -------------------------------------------------

void ProListModel::enter_selected() {
    if (rows_.empty() || cursor_row().kind != RowKind::CHART) return;
    selected_ = true;
    control_ = 2;   // PLAY centred
    swap_course_.reset();
}

void ProListModel::leave_selected() {
    selected_ = false;
    swap_course_.reset();
    control_ = 2;
    picker_open_ = false;
}

void ProListModel::open_search(const std::string& query) {
    if (query.empty()) return;
    search_query_ = query;
    selected_ = false;
    swap_course_.reset();
    context_ = Context::SEARCH;
    folder_path_.clear();
    context_genre_ = 0;
    context_label_ = "SEARCH: " + query;
    begin_build();
}

void ProListModel::move_filter_cursor(int delta) {
    constexpr int kFilterRows = 8;   // 5 difficulty toggles + star + tier + sort
    filter_cursor_ = ((filter_cursor_ + delta) % kFilterRows + kFilterRows) % kFilterRows;
}

void ProListModel::advance_filter_value() {
    switch (filter_cursor_) {
        case 0: case 1: case 2: case 3: case 4:
            filter_.diff[filter_cursor_] = !filter_.diff[filter_cursor_];
            break;
        case 5:
            filter_.star = (filter_.star + 1) % 11;    // ALL -> 1..10 -> ALL
            filter_.tier.clear();                      // switching the star clamps tier to ALL
            break;
        case 6: {
            std::vector<std::string> vals;
            vals.emplace_back("");                     // ALL
            if (filter_.star == 0) {
                for (const auto& t : ladder_hard_to_blank()) vals.push_back(t);
            } else {
                for (const auto& t : tier_set_for_level(filter_.star)) vals.push_back(t);
            }
            vals.emplace_back("NONE");
            size_t idx = 0;
            for (size_t i = 0; i < vals.size(); i++) {
                if (vals[i] == filter_.tier) { idx = i; break; }
            }
            filter_.tier = vals[(idx + 1) % vals.size()];
            break;
        }
        case 7:
            switch (sort_) {
                case Sort::SCORE: sort_ = Sort::CROWN; break;
                case Sort::CROWN: sort_ = Sort::ALPHA; break;
                case Sort::ALPHA: sort_ = Sort::TIER;  break;
                case Sort::TIER:  sort_ = Sort::SCORE; break;
            }
            break;
        default:
            break;
    }
    if (context_ != Context::ROOT) begin_build();
}

void ProListModel::move_control(int delta) {
    control_ = ((control_ + delta) % kControlCount + kControlCount) % kControlCount;
}

int ProListModel::active_course() const {
    if (swap_course_.has_value()) return *swap_course_;
    if (!rows_.empty() && cursor_row().kind == RowKind::CHART) return cursor_row().course;
    return -1;
}

void ProListModel::set_active_course(int course) {
    if (course < 0 || course > 4) return;
    swap_course_ = course;
}

void ProListModel::open_picker() {
    if (!selected_ || rows_.empty() || cursor_row().kind != RowKind::CHART) return;
    picker_open_ = true;
    picker_cursor_ = active_course();
    if (picker_cursor_ < 0) picker_cursor_ = 0;
}

void ProListModel::move_picker(int delta) {
    picker_cursor_ = ((picker_cursor_ + delta) % 5 + 5) % 5;
}

void ProListModel::commit_picker() {
    if (!picker_open_) return;
    set_active_course(picker_cursor_);
    picker_open_ = false;
}

void ProListModel::cancel_picker() {
    picker_open_ = false;
}

std::array<ProListModel::PickerCell, 5> ProListModel::picker_cells() const {
    std::array<PickerCell, 5> cells{};
    if (rows_.empty() || cursor_row().kind != RowKind::CHART) return cells;
    const Row& row = rows_[cursor_];
    const auto& levels_map = nav_->library_song_levels();
    auto lv_it = levels_map.find(row.song_path.string());
    for (int c = 0; c < 5; ++c) {
        int level = lv_it == levels_map.end() ? -1 : lv_it->second[c];
        cells[c].has_course = (level >= 1 && level <= 10);
        cells[c].level = cells[c].has_course ? level : 0;
        if (cells[c].has_course) cells[c].tier = tier_for_chart(row.song_path, c, level);
    }
    return cells;
}

bool ProListModel::song_genre(const fs::path& song_path, int& genre_index, std::string& genre_label) const {
    for (const fs::path& root : global_data.config->paths.tja_path) {
        fs::path rel = song_path.lexically_relative(root);
        if (rel.empty()) continue;
        auto first = rel.begin();
        if (first == rel.end() || *first == "..") continue;
        BoxDef def = nav_->parse_box_def(root / *first);
        genre_index = (int)def.genre_index;
        genre_label = def.genre_label;
        return true;
    }
    return false;
}