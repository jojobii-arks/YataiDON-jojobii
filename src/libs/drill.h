#pragma once

#include "config.h"
#include "parsers/tja.h"
#include <deque>
#include <random>
#include <string>
#include <vector>

// Workout charts. Pattern mode is a timed sequence. Drill mode freezes the
// lane on the current note and advances one step per correct hit.
//
// Alphabet: d/1 don, k/2 ka, D/3 big don, K/4 big ka, 0 . - _ space rest.
// A sequence is phrases separated by | or comma.

inline constexpr int DRILL_DIFFICULTY = 3;
inline constexpr int DRILL_BPM_MIN = 40;
inline constexpr int DRILL_BPM_MAX = 500;
inline constexpr int DRILL_PATTERN_MAX = 64;
inline constexpr int DRILL_SIZE_MAX = 256;
inline constexpr int DRILL_ENDLESS_CHUNK = 16;
inline constexpr int WORKOUT_PHRASE_MAX = 256;
inline constexpr int WORKOUT_REPS_MAX = 999;
inline constexpr int WORKOUT_PATTERN_EVERY_MIN = 1;
inline constexpr int WORKOUT_PATTERN_EVERY_MAX = 64;
inline constexpr int WORKOUT_NOTES_MIN = 10;
inline constexpr int WORKOUT_NOTES_MAX = 2000;
inline constexpr int WORKOUT_SPACE_MIN = 2;
inline constexpr int WORKOUT_SPACE_MAX = 25;
inline constexpr double WORKOUT_SCROLL_MIN = 0.5;
inline constexpr double WORKOUT_SCROLL_MAX = 5.0;
// Note length as a fraction of a 4-beat measure: 1/8 is an eighth note (0.5 beats).
inline constexpr int WORKOUT_NOTE_DIVS[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64};
inline constexpr int WORKOUT_NOTE_DIV_COUNT = 12;

inline int clamp_note_div(int div) {
    for (int value : WORKOUT_NOTE_DIVS)
        if (value == div) return value;
    return 8;
}

inline double note_div_beats(int div) {
    return 4.0 / (double)clamp_note_div(div);
}

struct DrillSlot {
    bool rest = true;
    NoteType type = NoteType::DON;
};

// SongParser stand-in: metadata plus a hand-built NoteList, no chart file.
class SyntheticParser {
public:
    TJAMetadata metadata;
    TJAEXData ex_data;
    fs::path file_path;
    NoteList notes;

    std::string get_difficulty_name() { return "oni"; }

    std::tuple<NoteList, std::deque<NoteList>, std::deque<NoteList>, std::deque<NoteList>>
    notes_to_position(int) {
        return {notes, std::deque<NoteList>{}, std::deque<NoteList>{}, std::deque<NoteList>{}};
    }

    std::string get_song_hash() { return "drill"; }
    std::string get_diff_hash(int) { return "drill"; }
};

struct DrillChart {
    DrillConfig spec;
    std::vector<DrillSlot> slots;
    std::vector<std::string> phrases;
    NoteList notes;
    std::vector<double> steps;
    double bpm = 120.0;
    double beat_ms = 500.0;
    double count_in_ms = 2000.0;
    double cycle_ms = 500.0;
    double generated_until_ms = 0.0;
    double place_t = 0.0;
    double next_bar_ms = 0.0;
    int next_index = 0;
    int next_hand = 2;
    int reps_generated = 0;
    bool endless = false;
    bool hold = false;
    std::mt19937 rng{1};
};

DrillConfig normalize_drill_config(DrillConfig in);
bool same_drill_config(const DrillConfig& a, const DrillConfig& b);
DrillChart build_drill_chart(const DrillConfig& raw);
// Pattern: extra is more sequence repetitions. Drill: extra is more notes.
NoteList extend_drill_chart(DrillChart& chart, int extra);
SyntheticParser make_synthetic_parser(const DrillChart& chart);

const std::vector<std::string>& workout_catalog();
std::string workout_pattern_name(const std::string& notes);

// Writes a short flat tick into the temp dir and loads it as "drill_click".
bool load_drill_click();
