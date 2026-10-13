#pragma once

// Pro select v2 — the list model.
//
// Owns the row sequence for the pro screen: the root browse rows, a folder's
// chart rows under the active filter + sort, the periodic BACK / FILTER row
// pair, the pinned-centre cursor with wrap and the ±10 skip, and the lazy
// per-row resolution the skin and the details panel read.
//
// It reads the library through Navigator's read accessors (the seam) and
// never touches the wheel: no navigator.cpp copy, no box plan, no geometry.
// Big lists (ALL ≈ 12,167 chart rows) are built in chunks from update() so
// no frame pays the full bill.

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Navigator;

class ProListModel {
public:
    // What the row sequence is browsing.
    enum class Context { ROOT, FOLDER, RECENT, FAVORITE, ALL, SEARCH };

    enum class RowKind {
        FOLDER,    // a genre / dan folder row in the root (opens a FOLDER context)
        SEARCH,    // the root SEARCH row
        RECENT,    // the root RECENT row
        FAVORITE,  // the root FAVORITE row
        ALL,       // the root ALL row
        CHART,     // one chart = one row; song_path + course + level
        BACK,      // the periodic BACK row inside a folder
        FILTER,    // the periodic FILTER row (opens the Filter/Sort dialog)
    };

    enum class Sort { SCORE, CROWN, ALPHA, TIER, LEVEL };

    // The Filter/Sort dialog has TWO focus levels (control rework, 2026-10):
    //   ENTRY — kat walks the entries (Difficulty, Star, Tier, Sort, Confirm);
    //           don enters the entry's group (Confirm applies and closes).
    //   GROUP — kat walks the entered group's values; don commits per the
    //           entry's own contract (see advance_filter_value).
    enum class FilterFocus { ENTRY, GROUP };

    // View Settings (persisted to config.toml in a later step).
    struct Filter {
        std::array<bool, 5> diff{{true, true, true, true, true}};  // Easy..Ura
        int star = 0;          // 0 = ALL, else 1..10
        std::string tier;      // "" = ALL, "NONE", else a tier letter
    };

    struct Row {
        RowKind kind = RowKind::CHART;
        // Folder / collection rows.
        std::string name;                 // display name (box.def name for genres)
        std::filesystem::path folder_path;
        int genre_index = -1;
        int chart_count = 0;              // unfiltered chart rows under the folder
        // Chart rows.
        std::filesystem::path song_path;
        std::string title, subtitle;      // EN pair, the map's own key
        int course = -1;                  // 0 Easy .. 4 Ura
        int level = 0;
        // Sort keys, resolved while building.
        bool played = false;
        int score = 0, crown = 0;
        int tier_rank = 0;                // ladder rank, 0 = tier-less
    };

    // Everything the details panel / a drawn card needs for one chart row,
    // resolved on demand (never stored for a whole list).
    struct ChartInfo {
        bool found = false;
        std::string title, subtitle;
        int course = -1, level = 0;
        int genre_index = -1;
        std::string tier;                 // "" when tier-less
        bool played = false;
        int score = 0, crown = 0, rank = 0;   // scores.db best (P1)
        int good = 0, ok = 0, miss = 0;
        bool favorite = false;
        bool branching = false;
    };

    explicit ProListModel(Navigator* nav) : nav_(nav) {}

    void set_hide_dan(bool hide) { hide_dan_ = hide; }

    // ---- contexts -----------------------------------------------------
    void reset_root();                    // context ROOT, cursor 0
    Context context() const { return context_; }
    bool in_folder() const { return context_ != Context::ROOT; }
    void open_folder_row(int row_index);  // FOLDER / RECENT / FAVORITE / ALL / SEARCH row
    void back_to_root();
    // What the header names: "root", the folder's own name, or the collection.
    const std::string& context_label() const { return context_label_; }

    // ---- building -----------------------------------------------------
    bool ready() const { return !building_; }
    bool building() const { return building_; }
    float build_progress() const;
    void update();                        // advances the chunked build

    // ---- rows / cursor ------------------------------------------------
    int row_count() const { return (int)rows_.size(); }
    int cursor() const { return cursor_; }
    const Row& row_at(int index) const { return rows_[index]; }
    const Row& cursor_row() const { return rows_[cursor_]; }
    void move_cursor(int delta);          // ±1, wraps
    void skip_cursor(int dir);            // ±10 (dir = +1 / -1), wraps
    void set_cursor(int index);

    // ---- filter / sort ------------------------------------------------
    Filter& filter() { return filter_; }
    const Filter& filter() const { return filter_; }
    Sort sort() const { return sort_; }
    void set_sort(Sort s);
    void filter_changed();                // star changed -> tier clamp lives in the dialog UI
    const char* sort_name() const;

    // Chart rows currently in the list (the filtered count the pill shows).
    int chart_row_count() const;

    // ---- resolved per-row info ---------------------------------------
    ChartInfo resolve(int row_index) const;         // row as stored
    ChartInfo resolve_display(int row_index) const; // honours the selected-state swap

    // The chart's own audio, read from its tja on demand and cached (the parse
    // is per song, never for the whole list — ALL is ~12k rows). Mirrors the
    // branch_cache_ pattern below.
    struct SongAudio { std::filesystem::path wave; float demostart = 0.0f; bool ok = false; };
    SongAudio song_audio(const std::filesystem::path& song_path) const;

    // ---- the selected-chart state (DON flow) --------------------------
    bool selected() const { return selected_; }
    void enter_selected();
    void leave_selected();
    int control_index() const { return control_; }        // 0..4
    void move_control(int delta);                         // wraps 0..4
    static constexpr int kControlCount = 5;               // BACK, SETTINGS, PLAY, PRACTICE, CHANGE DIFF
    int active_course() const;                            // swapped course, or the row's own
    void set_active_course(int course);                   // CHANGE DIFF picker commits

    // CHANGE DIFF picker (view state lives here so the skin can read it).
    struct PickerCell { bool has_course = false; int level = 0; std::string tier; };
    bool picker_open() const { return picker_open_; }
    int picker_cursor() const { return picker_cursor_; }
    void open_picker();
    void move_picker(int delta);
    void commit_picker();
    void cancel_picker();
    std::array<PickerCell, 5> picker_cells() const;

    // The genre folder a chart sits under (session bookkeeping on PLAY).
    bool song_genre(const std::filesystem::path& song_path, int& genre_index,
                    std::string& genre_label) const;

    // The genre of the currently open context (folder row's genre; fixed for
    // the collections) — the skin's backdrop + details band key off it.
    int context_genre_index() const { return context_genre_; }

    // ---- Filter/Sort dialog (TAB / the filter card row) ----------------
    bool filter_open() const { return filter_open_; }
    void open_filter() {
        filter_open_ = true;
        filter_cursor_ = kEntryDiff;
        filter_focus_ = FilterFocus::ENTRY;
        filter_value_cursor_ = 0;
    }
    void close_filter() { filter_open_ = false; }
    // ENTRY focus: filter_cursor() is the entry index 0..4 —
    // 0 DIFFICULTY, 1 STAR, 2 TIER, 3 SORT, 4 CONFIRM. GROUP focus:
    // filter_cursor() stays the entry the focus is inside (0..3) and
    // filter_value_cursor() is the value index within it —
    //   Difficulty 0..4 = the Easy..Ura toggles, 5 = the confirm checkmark
    //   Star       0 = ALL, 1..10 = the level
    //   Tier       0 = ALL, then the level's ladder, last index = NONE
    //   Sort       0..4 = SCORE, CROWN, A-Z, TIER, LEVEL
    int filter_cursor() const { return filter_cursor_; }
    int filter_value_cursor() const { return filter_value_cursor_; }
    FilterFocus filter_focus() const { return filter_focus_; }
    const char* filter_focus_name() const;
    void move_filter_cursor(int delta);
    // don on the open dialog. ENTRY: enters the entry's group, except CONFIRM,
    // which returns true (the caller applies + closes). GROUP: Difficulty
    // toggles the value under the cursor and STAYS (its exit is the checkmark,
    // which returns to ENTRY with the cursor on Difficulty); Star / Tier / Sort
    // commit the value and return to ENTRY with the cursor on that entry. A
    // star change clears the tier selection (SPEC 3: SSS at ★10 does not exist
    // at ★9). Returns true when the dialog should close.
    bool advance_filter_value();

    // ---- SEARCH --------------------------------------------------------
    void open_search(const std::string& query);
    const std::string& search_query() const { return search_query_; }

private:
    struct RawRow {                       // a chart row before pair insertion
        std::filesystem::path song_path;
        std::string title, subtitle;
        int course = -1, level = 0;
        bool played = false;
        int score = 0, crown = 0;
        int tier_rank = 0;
    };

    Navigator* nav_ = nullptr;

    Context context_ = Context::ROOT;
    std::filesystem::path folder_path_;   // for FOLDER
    int context_genre_ = 0;               // ref-frame genre of the open context
    std::string context_label_ = "root";  // header name of the open context
    Filter filter_{};
    Sort sort_ = Sort::ALPHA;             // OPEN #2 proposal: alphabetical on first run

    std::vector<Row> rows_;
    int cursor_ = 0;

    // Selected-chart state.
    bool selected_ = false;
    int control_ = 2;                     // PLAY centred
    // CHANGE DIFF is per-row view state: the swap repaints ONE chart row — the
    // one selected when it committed — never the whole list. swap_row_ holds that
    // row's index (-1 = no swap in force); swap_course_ holds its course.
    std::optional<int> swap_course_;
    int swap_row_ = -1;
    bool picker_open_ = false;
    int picker_cursor_ = 0;

    void clear_swap() { swap_course_.reset(); swap_row_ = -1; }

    // Filter/Sort dialog + search state.
    bool filter_open_ = false;
    int filter_cursor_ = 0;            // ENTRY: 0..4; GROUP: the entered entry (0..3)
    FilterFocus filter_focus_ = FilterFocus::ENTRY;
    int filter_value_cursor_ = 0;      // GROUP: index within the entered entry's values
    std::string search_query_;

    // Entry indices of the dialog's top level.
    static constexpr int kEntryDiff = 0, kEntryStar = 1, kEntryTier = 2,
                         kEntrySort = 3, kEntryConfirm = 4, kEntryCount = 5;
    bool filter_group_open() const { return filter_focus_ == FilterFocus::GROUP; }
    void enter_filter_group();                                    // ENTRY -> GROUP
    void exit_filter_group();                                     // GROUP -> ENTRY
    int filter_value_count() const;                               // values in the entered entry
    std::vector<std::string> filter_tier_values() const;          // ALL, the ladder, NONE

    // ---- build state (chunked) -----------------------------------------
    void begin_build();
    void build_songs_step(size_t budget);
    void build_sort_and_assemble();
    void collect_context_songs();

    // tier ladder rank for a letter (0 = tier-less) — the settled ladder:
    // X below F, i.e. F E D C B A S S+ SS SSS = 2..11, X = 1, none = 0.
    static int tier_letter_rank(const std::string& letter);
    static int ladder_index(const std::string& letter);   // for "hardest first" ordering

    std::string tier_for_chart(const std::filesystem::path& song_path, int course, int level) const;
    bool chart_passes_filter(const std::filesystem::path& song_path, int course, int level,
                             const std::string& tier) const;
    bool branch_flag(const std::filesystem::path& song_path, int course) const;
    void build_root_rows();
    void assemble_rows();

    bool hide_dan_ = false;

    // ---- chunked build state ------------------------------------------
    bool building_ = false;
    enum class Phase { IDLE, SONGS, SORT, ASSEMBLE };
    Phase phase_ = Phase::IDLE;
    using SongKey = std::pair<std::string, std::string>;
    std::vector<std::pair<SongKey, std::filesystem::path>> build_songs_;   // context song list
    std::vector<RawRow> cand_;                              // filtered chart rows
    size_t build_pos_ = 0;

    // song path -> is_branching per course (1 = yes, 2 = no; parsed once on demand)
    mutable std::unordered_map<std::string, std::array<uint8_t, 5>> branch_cache_;

    // song path -> wave / demostart (parsed once on demand, for the preview)
    mutable std::unordered_map<std::string, SongAudio> audio_cache_;

public:
    // Tier helpers the Filter/Sort dialog needs (per-level sets are NOT
    // monotonic, so they are listed explicitly — SPEC 3).
    static const std::vector<std::string>& ladder_hard_to_blank();   // SSS..F, X, None handled by UI
    static std::vector<std::string> tier_set_for_level(int level);   // empty below ★6
    static bool tier_in_level_set(int level, const std::string& letter);
};