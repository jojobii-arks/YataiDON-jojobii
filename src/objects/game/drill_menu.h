#pragma once

#include "../../libs/drill.h"
#include "../../libs/screen.h"
#include <vector>

// Workout setup. Keyboard, drum, and mouse. Call update(false) while playing
// so menu keys stay out of the drum until the run is stopped.
class DrillMenu {
public:
    enum class Mode { SELECT, FIELD };

    enum class Action {
        NONE,
        SPEC_CHANGED,
        START,
        PLAY,
        RESTART,
        BACK
    };

    void open(Mode mode);
    void close_editing();
    bool is_editing() const { return editing; }

    Action update(bool open_input);
    void draw() const;

private:
    enum class Row {
        MODE, BPM, SCROLL, LEAD,
        SEQUENCE, ADD, REMOVE, CUSTOM, CLEAR,
        REPS, INTERVAL, GAP_P, GAP_S, TOTAL, SPACING,
        ENDLESS, INVERT, RANDOM, ALTERNATE, METRONOME,
        NO_MISS, MISS_ACTION, MAX_MISSES,
        TIMER, TIMER_SEC,
        START, PLAY, RESTART, BACK
    };

    Mode mode = Mode::SELECT;
    std::vector<Row> rows;
    int index = 0;
    int catalog_index = 0;
    bool editing = false;
    bool adjusting = false;
    std::string edit_buffer;
    DrillConfig spec;

    bool dirty = false;
    double dirty_ms = 0.0;

    struct Hit {
        float x = 0, y = 0, w = 0, h = 0;
        int row = -1;
        int part = 0; // 0 body, 1 minus, 2 plus
    };
    mutable std::vector<Hit> hits;

    void rebuild_rows();
    void persist_now();
    void flush_idle(double now);
    Action commit_spec();
    Action nudge(int dir, bool fast);
    Action activate();
    void move_row(int dir);
    bool is_numeric(Row row) const;
    bool is_adjustable(Row row) const;
    std::string row_label(Row row) const;
    std::string row_value(Row row) const;
    void add_phrase(const std::string& notes);
    void add_visible_patterns();
    void draw_pattern_browser(float x, float y, float w, float h) const;

    int hit_filter = 2;
    mutable float pattern_scroll = 0;
    mutable float pattern_scroll_max = 0;
    mutable bool browser_open = false;
    mutable float browser_x = 0, browser_y = 0, browser_w = 0, browser_h = 0;

    struct Chip {
        float x = 0, y = 0, w = 0, h = 0;
        int catalog = -1;
        int filter = -1;
        bool add_all = false;
    };
    mutable std::vector<Chip> chips;
};
