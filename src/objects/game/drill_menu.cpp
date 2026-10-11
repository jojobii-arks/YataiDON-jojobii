#include "drill_menu.h"
#include "../../libs/audio.h"
#include "../../libs/config.h"
#include "../../libs/global_data.h"
#include "../../libs/input.h"
#include "../../libs/time.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

std::string key_name() {
    if (!global_data.config) return "f2";
    try {
        return getKeyString(global_data.config->keys.drill_key);
    } catch (const std::exception&) {
        return "f2";
    }
}

std::string tenth(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", value);
    return buf;
}

std::string clip(std::string text, int max_chars) {
    if ((int)text.size() <= max_chars) return text;
    if (max_chars <= 3) return text.substr(0, max_chars);
    return text.substr(0, max_chars - 3) + "...";
}

void play_kat() { audio.play_sound("kat", VolumePreset::SOUND); }
void play_don() { audio.play_sound("don", VolumePreset::SOUND); }

const char* note_icon_name(char c) {
    switch (c) {
        case 'd': case '1': return "notes/1";
        case 'k': case '2': return "notes/2";
        case 'D': case '3': return "notes/3";
        case 'K': case '4': return "notes/4";
        default: return nullptr;
    }
}

void ensure_note_icons() {
    if (!tex.has_texture("notes/1"))
        tex.load_folder("game", "notes");
}

float draw_note_icon(char c, float x, float y, float size) {
    const char* name = note_icon_name(c);
    if (!name) return size * 0.35f;
    ensure_note_icons();
    if (!tex.has_texture(name)) return size;
    TextureObject* icon = tex.get_texture(name);
    const ray::Texture2D* gpu = icon ? icon->frame_texture(0) : nullptr;
    if (!gpu || gpu->id == 0) return size;
    ray::Rectangle src{0, 0, (float)gpu->width, (float)gpu->height};
    if (icon->crop_data.has_value() && !icon->crop_data->empty())
        src = (*icon->crop_data)[0];
    ray::DrawTexturePro(*gpu, src, ray::Rectangle{x, y, size, size}, {0, 0}, 0.0f, ray::WHITE);
    return size;
}

float draw_phrase_icons(const std::string& notes, float x, float y, float size, float max_x) {
    float gap = 2.0f;
    for (char c : notes) {
        if (x + size > max_x) return x;
        float used = draw_note_icon(c, x, y, size);
        x += used + gap;
    }
    return x;
}

}  // namespace

void DrillMenu::open(Mode which) {
    mode = which;
    editing = false;
    adjusting = false;
    index = 0;
    dirty = false;
    spec = global_data.config ? normalize_drill_config(global_data.config->drill) : DrillConfig{};
    if (global_data.config) global_data.config->drill = spec;
    rebuild_rows();
}

void DrillMenu::close_editing() {
    editing = false;
    adjusting = false;
}

void DrillMenu::rebuild_rows() {
    int keep = index;
    rows.clear();
    const bool drill = spec.kind == 1;
    rows.push_back(Row::MODE);
    rows.push_back(Row::BPM);
    rows.push_back(Row::SCROLL);
    if (drill) {
        rows.push_back(Row::TOTAL);
        rows.push_back(Row::SPACING);
    } else {
        rows.push_back(Row::SEQUENCE);
        rows.push_back(Row::ADD);
        rows.push_back(Row::REMOVE);
        rows.push_back(Row::CUSTOM);
        rows.push_back(Row::CLEAR);
        rows.push_back(Row::REPS);
        rows.push_back(Row::INTERVAL);
        rows.push_back(Row::GAP_P);
        rows.push_back(Row::GAP_S);
        rows.push_back(Row::INVERT);
        rows.push_back(Row::RANDOM);
        rows.push_back(Row::METRONOME);
    }
    rows.push_back(Row::ENDLESS);
    rows.push_back(Row::ALTERNATE);
    if (spec.alternate) rows.push_back(Row::LEAD);
    rows.push_back(Row::NO_MISS);
    if (spec.no_miss) {
        rows.push_back(Row::MISS_ACTION);
        rows.push_back(Row::MAX_MISSES);
    }
    rows.push_back(Row::TIMER);
    if (spec.timer_on) rows.push_back(Row::TIMER_SEC);
    if (mode == Mode::SELECT) rows.push_back(Row::START);
    else {
        rows.push_back(Row::PLAY);
        rows.push_back(Row::RESTART);
    }
    rows.push_back(Row::BACK);
    index = keep;
    if (index >= (int)rows.size()) index = (int)rows.size() - 1;
    if (index < 0) index = 0;
}

void DrillMenu::persist_now() {
    if (!global_data.config) return;
    global_data.config->drill = spec;
    save_config(*global_data.config);
    dirty = false;
}

void DrillMenu::flush_idle(double now) {
    if (dirty && now - dirty_ms > 250.0) persist_now();
}

DrillMenu::Action DrillMenu::commit_spec() {
    DrillConfig next = normalize_drill_config(spec);
    bool changed = !same_drill_config(next, spec);
    if (global_data.config) changed = !same_drill_config(next, global_data.config->drill);
    spec = next;
    if (!changed) return Action::NONE;
    if (global_data.config) global_data.config->drill = spec;
    dirty = true;
    dirty_ms = get_current_ms();
    rebuild_rows();
    return Action::SPEC_CHANGED;
}

void DrillMenu::move_row(int dir) {
    if (rows.empty()) return;
    adjusting = false;
    int n = (int)rows.size();
    index = ((index + dir) % n + n) % n;
    play_kat();
}

bool DrillMenu::is_numeric(Row row) const {
    switch (row) {
        case Row::BPM:
        case Row::SCROLL:
        case Row::REPS:
        case Row::GAP_P:
        case Row::GAP_S:
        case Row::TOTAL:
        case Row::SPACING:
        case Row::TIMER_SEC:
        case Row::MAX_MISSES:
            return true;
        default:
            return false;
    }
}

bool DrillMenu::is_adjustable(Row row) const {
    return is_numeric(row) || row == Row::MODE || row == Row::ADD || row == Row::LEAD
        || row == Row::ENDLESS || row == Row::INVERT || row == Row::RANDOM
        || row == Row::ALTERNATE || row == Row::METRONOME || row == Row::NO_MISS || row == Row::MISS_ACTION
        || row == Row::TIMER || row == Row::INTERVAL;
}

DrillMenu::Action DrillMenu::nudge(int dir, bool fast) {
    if (rows.empty() || dir == 0) return Action::NONE;
    Row row = rows[index];
    int step = fast ? 10 : 1;
    switch (row) {
        case Row::MODE:
            spec.kind = spec.kind == 1 ? 0 : 1;
            break;
        case Row::BPM:
            spec.bpm += dir * step;
            break;
        case Row::SCROLL:
            spec.scroll += dir * (fast ? 0.5 : 0.1);
            break;
        case Row::ADD: {
            const auto& catalog = workout_catalog();
            int n = (int)catalog.size();
            if (n < 1) return Action::NONE;
            int move = (dir < 0 ? -1 : 1) * (fast ? 4 : 1);
            catalog_index = ((catalog_index + move) % n + n) % n;
            play_kat();
            return Action::NONE;
        }
        case Row::REPS:
            spec.repetitions += dir * step;
            break;
        case Row::INTERVAL: {
            int at = 0;
            for (int i = 0; i < WORKOUT_NOTE_DIV_COUNT; ++i)
                if (WORKOUT_NOTE_DIVS[i] == spec.note_div) at = i;
            int move = (dir < 0 ? -1 : 1) * (fast ? 3 : 1);
            at = ((at + move) % WORKOUT_NOTE_DIV_COUNT + WORKOUT_NOTE_DIV_COUNT) % WORKOUT_NOTE_DIV_COUNT;
            spec.note_div = WORKOUT_NOTE_DIVS[at];
            break;
        }
        case Row::GAP_P:
            spec.pattern_every += dir * (fast ? 4 : 1);
            break;
        case Row::GAP_S:
            spec.sequence_interval += dir * (fast ? 0.5 : 0.1);
            break;
        case Row::TOTAL:
            spec.total_notes += dir * (fast ? 50 : 1);
            break;
        case Row::SPACING:
            spec.note_spacing += dir * (fast ? 2 : 1);
            break;
        case Row::TIMER_SEC:
            spec.timer_sec += dir * (fast ? 30 : 10);
            break;
        case Row::MAX_MISSES:
            spec.max_misses += dir * (fast ? 5 : 1);
            break;
        case Row::LEAD:
            spec.lead_left = !spec.lead_left;
            break;
        case Row::ENDLESS:
            spec.endless = !spec.endless;
            break;
        case Row::INVERT:
            spec.invert = !spec.invert;
            break;
        case Row::RANDOM:
            spec.random_pool = !spec.random_pool;
            break;
        case Row::ALTERNATE:
            spec.alternate = !spec.alternate;
            break;
        case Row::METRONOME:
            spec.metronome = !spec.metronome;
            break;
        case Row::NO_MISS:
            spec.no_miss = !spec.no_miss;
            break;
        case Row::MISS_ACTION:
            spec.miss_restart = !spec.miss_restart;
            break;
        case Row::TIMER:
            spec.timer_on = !spec.timer_on;
            break;
        default:
            return Action::NONE;
    }
    play_kat();
    return commit_spec();
}

DrillMenu::Action DrillMenu::activate() {
    if (rows.empty()) return Action::NONE;
    switch (rows[index]) {
        case Row::BPM:
        case Row::SCROLL:
        case Row::REPS:
        case Row::GAP_P:
        case Row::GAP_S:
        case Row::TOTAL:
        case Row::SPACING:
        case Row::TIMER_SEC:
        case Row::MAX_MISSES:
            adjusting = !adjusting;
            play_don();
            return Action::NONE;
        case Row::MODE:
        case Row::LEAD:
        case Row::ENDLESS:
        case Row::INVERT:
        case Row::RANDOM:
        case Row::ALTERNATE:
        case Row::METRONOME:
        case Row::NO_MISS:
        case Row::MISS_ACTION:
        case Row::TIMER:
        case Row::INTERVAL:
            play_don();
            return nudge(1, false);
        case Row::ADD: {
            const auto& catalog = workout_catalog();
            if (catalog.empty()) return Action::NONE;
            int at = catalog_index % (int)catalog.size();
            if (!spec.sequence.empty()) spec.sequence.push_back('|');
            spec.sequence += catalog[at];
            play_don();
            return commit_spec();
        }
        case Row::REMOVE: {
            if (spec.sequence.empty()) return Action::NONE;
            auto pos = spec.sequence.find_last_of('|');
            if (pos == std::string::npos) spec.sequence.clear();
            else spec.sequence.erase(pos);
            play_don();
            return commit_spec();
        }
        case Row::CUSTOM:
            editing = true;
            adjusting = false;
            edit_buffer.clear();
            play_don();
            return Action::NONE;
        case Row::CLEAR:
            spec.sequence.clear();
            play_don();
            return commit_spec();
        case Row::START:
            play_don();
            persist_now();
            return Action::START;
        case Row::PLAY:
            play_don();
            persist_now();
            return Action::PLAY;
        case Row::RESTART:
            play_don();
            persist_now();
            return Action::RESTART;
        case Row::BACK:
            play_don();
            persist_now();
            return Action::BACK;
        case Row::SEQUENCE:
            return Action::NONE;
    }
    return Action::NONE;
}

std::string DrillMenu::row_label(Row row) const {
    const bool drill = spec.kind == 1;
    switch (row) {
        case Row::MODE: return "Mode";
        case Row::BPM: return "BPM";
        case Row::SCROLL: return "Scroll";
        case Row::LEAD: return "Lead hand";
        case Row::SEQUENCE: return drill ? "Pool" : "Sequence";
        case Row::ADD: return "Add pattern";
        case Row::REMOVE: return "Remove last";
        case Row::CUSTOM: return "Add custom";
        case Row::CLEAR: return "Clear";
        case Row::REPS: return "Repetitions";
        case Row::INTERVAL: return "Interval";
        case Row::GAP_P: return "Pattern every";
        case Row::GAP_S: return "Sequence gap";
        case Row::TOTAL: return "Total notes";
        case Row::SPACING: return "Note spacing";
        case Row::ENDLESS: return "Endless";
        case Row::INVERT: return "Invert";
        case Row::RANDOM: return "Random pool";
        case Row::ALTERNATE: return "Alternate hits";
        case Row::METRONOME: return "Metronome";
        case Row::NO_MISS: return "No miss";
        case Row::MISS_ACTION: return "On miss";
        case Row::MAX_MISSES: return "Miss limit";
        case Row::TIMER: return "Timer";
        case Row::TIMER_SEC: return "Seconds";
        case Row::START: return "Start";
        case Row::PLAY: return "Play";
        case Row::RESTART: return "Restart";
        case Row::BACK: return mode == Mode::SELECT ? "Back" : "Back to setup";
    }
    return "";
}

std::string DrillMenu::row_value(Row row) const {
    switch (row) {
        case Row::MODE: return spec.kind == 1 ? "Drill" : "Pattern";
        case Row::BPM: return std::to_string(spec.bpm);
        case Row::SCROLL: return tenth(spec.scroll);
        case Row::LEAD: return spec.lead_left ? "Left" : "Right";
        case Row::SEQUENCE: {
            if (spec.sequence.empty())
                return spec.kind == 1 ? "all 1-6 hit combos" : "(add a pattern)";
            std::string shown = spec.sequence;
            for (char& c : shown) if (c == '|') c = ' ';
            return clip(shown, 36);
        }
        case Row::ADD: {
            const auto& catalog = workout_catalog();
            if (catalog.empty()) return "";
            return clip(workout_pattern_name(catalog[catalog_index % (int)catalog.size()]), 36);
        }
        case Row::CUSTOM:
            return editing ? clip(edit_buffer + "_", 24) : "type d/k";
        case Row::REPS: return std::to_string(spec.repetitions);
        case Row::INTERVAL: return "1/" + std::to_string(spec.note_div);
        case Row::GAP_P:
            return std::to_string(spec.pattern_every) + (spec.pattern_every == 1 ? " beat" : " beats");
        case Row::GAP_S: return tenth(spec.sequence_interval) + " beats";
        case Row::TOTAL: return std::to_string(spec.total_notes);
        case Row::SPACING: return std::to_string(spec.note_spacing);
        case Row::ENDLESS: return spec.endless ? "on" : "off";
        case Row::INVERT: return spec.invert ? "on" : "off";
        case Row::RANDOM: return spec.random_pool ? "on" : "off";
        case Row::ALTERNATE: return spec.alternate ? "on" : "off";
        case Row::METRONOME: return spec.metronome ? "on" : "off";
        case Row::NO_MISS: return spec.no_miss ? "on" : "off";
        case Row::MISS_ACTION: return spec.miss_restart ? "Restart" : "Stop";
        case Row::MAX_MISSES: return std::to_string(spec.max_misses);
        case Row::TIMER: return spec.timer_on ? "on" : "off";
        case Row::TIMER_SEC: return std::to_string(spec.timer_sec);
        default: return "";
    }
}

void DrillMenu::add_phrase(const std::string& notes) {
    if (notes.empty()) return;
    if (!spec.sequence.empty()) spec.sequence.push_back('|');
    spec.sequence += notes;
}

void DrillMenu::add_visible_patterns() {
    const auto& catalog = workout_catalog();
    for (const std::string& notes : catalog) {
        if (hit_filter > 0 && (int)notes.size() != hit_filter) continue;
        add_phrase(notes);
    }
}

DrillMenu::Action DrillMenu::update(bool open_input) {
    double now = get_current_ms();
    flush_idle(now);
    if (!open_input || rows.empty()) return Action::NONE;

    PlayerNum pn = global_data.player_num;
    if (pn != PlayerNum::P1 && pn != PlayerNum::P2) pn = PlayerNum::P1;

    if (editing) {
        (void)is_l_kat_pressed(pn);
        (void)is_r_kat_pressed(pn);
        bool confirm = is_l_don_pressed(pn) || is_r_don_pressed(pn)
            || check_key_pressed(ray::KEY_ENTER);
        int back_key = global_data.config ? global_data.config->keys.back_key : ray::KEY_ESCAPE;
        if (check_key_pressed(back_key)) {
            editing = false;
            play_kat();
            return Action::NONE;
        }
        if (ray::IsKeyPressed(ray::KEY_BACKSPACE) && !edit_buffer.empty())
            edit_buffer.pop_back();
        int ch = 0;
        while ((ch = ray::GetCharPressed()) > 0) {
            if (ch >= 32 && ch < 127 && (int)edit_buffer.size() < DRILL_PATTERN_MAX)
                edit_buffer.push_back((char)ch);
        }
        if (confirm) {
            if (!edit_buffer.empty()) {
                if (!spec.sequence.empty()) spec.sequence.push_back('|');
                spec.sequence += edit_buffer;
            }
            editing = false;
            play_don();
            Action changed = commit_spec();
            if (changed != Action::NONE) persist_now();
            return changed;
        }
        return Action::NONE;
    }

    while (ray::GetCharPressed() > 0) {}

    if (spec.kind != 1 && !adjusting) {
        for (int n = 1; n <= 6; ++n) {
            if (ray::IsKeyPressed(ray::KEY_ONE + (n - 1))) {
                hit_filter = n;
                pattern_scroll = 0;
                play_kat();
            }
        }
        if (ray::IsKeyPressed(ray::KEY_ZERO)) {
            hit_filter = 0;
            pattern_scroll = 0;
            play_kat();
        }
    }

    int back_key = global_data.config ? global_data.config->keys.back_key : ray::KEY_ESCAPE;
    bool back = check_key_pressed(back_key);
    bool confirm = check_key_pressed(ray::KEY_ENTER) || is_l_don_pressed(pn) || is_r_don_pressed(pn);
    bool fast = ray::IsKeyDown(ray::KEY_LEFT_SHIFT) || ray::IsKeyDown(ray::KEY_RIGHT_SHIFT);

    if (back) {
        if (adjusting) {
            adjusting = false;
            play_kat();
            return Action::NONE;
        }
        persist_now();
        return Action::BACK;
    }

    Action action = Action::NONE;
    if (adjusting) {
        bool kat_left = is_l_kat_pressed(pn);
        bool kat_right = is_r_kat_pressed(pn);
        bool left = check_key_pressed(ray::KEY_LEFT) || kat_left;
        bool right = check_key_pressed(ray::KEY_RIGHT) || kat_right;
        if (check_key_pressed(ray::KEY_UP)) move_row(-1);
        else if (check_key_pressed(ray::KEY_DOWN)) move_row(1);
        else if (left) action = nudge(-1, fast);
        else if (right) action = nudge(1, fast);
        else if (confirm) action = activate();
    } else {
        bool up = check_key_pressed(ray::KEY_UP) || is_l_kat_pressed(pn);
        bool down = check_key_pressed(ray::KEY_DOWN) || is_r_kat_pressed(pn);
        bool left = check_key_pressed(ray::KEY_LEFT);
        bool right = check_key_pressed(ray::KEY_RIGHT);
        if (up) move_row(-1);
        else if (down) move_row(1);
        else if (left) action = nudge(-1, fast);
        else if (right) action = nudge(1, fast);
        else if (confirm) action = activate();
    }

    Row row = rows[index];
    bool repeatable = is_numeric(row) || row == Row::ADD;
    static double repeat_at = 0;
    static int repeat_dir = 0;
    int held = 0;
    if (ray::IsKeyDown(ray::KEY_LEFT)) held = -1;
    else if (ray::IsKeyDown(ray::KEY_RIGHT)) held = 1;
    if (!repeatable || held == 0) {
        repeat_dir = 0;
    } else if (held != repeat_dir) {
        repeat_dir = held;
        repeat_at = now + 350.0;
    } else if (now >= repeat_at && action == Action::NONE) {
        action = nudge(held, fast);
        repeat_at = now + 50.0;
    }

    ray::Vector2 mouse = ray::GetMousePosition();
    bool clicked = ray::IsMouseButtonPressed(ray::MOUSE_BUTTON_LEFT);
    auto inside = [](float x, float y, float w, float h, ray::Vector2 mouse_pos) {
        float sx = (float)virtual_to_screen_x(x);
        float sy = (float)virtual_to_screen_y(y);
        float sx2 = (float)virtual_to_screen_x(x + w);
        float sy2 = (float)virtual_to_screen_y(y + h);
        return mouse_pos.x >= sx && mouse_pos.x <= sx2 && mouse_pos.y >= sy && mouse_pos.y <= sy2;
    };
    if (browser_open && ray::GetMouseWheelMove() != 0.0f && inside(browser_x, browser_y, browser_w, browser_h, mouse)) {
        pattern_scroll -= ray::GetMouseWheelMove() * 48.0f;
        if (pattern_scroll < 0) pattern_scroll = 0;
        if (pattern_scroll > pattern_scroll_max) pattern_scroll = pattern_scroll_max;
    }
    bool on_chip = false;
    for (const Chip& chip : chips) {
        if (!inside(chip.x, chip.y, chip.w, chip.h, mouse)) continue;
        on_chip = true;
        if (!clicked) break;
        if (chip.add_all) {
            add_visible_patterns();
            play_don();
            action = commit_spec();
        } else if (chip.filter >= 0) {
            hit_filter = chip.filter;
            pattern_scroll = 0;
            play_kat();
        } else if (chip.catalog >= 0) {
            const auto& catalog = workout_catalog();
            if (chip.catalog < (int)catalog.size()) {
                add_phrase(catalog[chip.catalog]);
                play_don();
                action = commit_spec();
            }
        }
        break;
    }
    if (on_chip) {
        if (action != Action::NONE) persist_now();
        return action;
    }
    auto contains = [](const Hit& hit, ray::Vector2 mouse_pos) {
        float x = (float)virtual_to_screen_x(hit.x);
        float y = (float)virtual_to_screen_y(hit.y);
        float x2 = (float)virtual_to_screen_x(hit.x + hit.w);
        float y2 = (float)virtual_to_screen_y(hit.y + hit.h);
        return mouse_pos.x >= x && mouse_pos.x <= x2 && mouse_pos.y >= y && mouse_pos.y <= y2;
    };
    for (const Hit& hit : hits) {
        if (!contains(hit, mouse)) continue;
        if (hit.row < 0 || hit.row >= (int)rows.size()) break;
        if (!clicked) {
            if (hit.part == 0 && hit.row != index) {
                index = hit.row;
                adjusting = false;
            }
            break;
        }
        index = hit.row;
        if (hit.part == 1) action = nudge(-1, fast);
        else if (hit.part == 2) action = nudge(1, fast);
        else action = activate();
        break;
    }

    if (action != Action::NONE) persist_now();
    return action;
}

void DrillMenu::draw() const {
    hits.clear();
    chips.clear();
    browser_open = false;
    if (rows.empty() || tex.screen_width <= 0) return;

    const bool browser = spec.kind != 1;
    const float margin = 16.0f;
    const float gap = 12.0f;
    float browser_w_now = 0;
    if (browser) browser_w_now = std::min(620.0f, (float)tex.screen_width * 0.46f);
    float panel_w = browser
        ? (float)tex.screen_width - margin * 2.0f - gap - browser_w_now
        : std::min(920.0f, (float)tex.screen_width - 24.0f);
    if (panel_w < 420.0f) panel_w = 420.0f;
    const float pad = 16.0f;
    const float header = 52.0f;
    const float footer = 72.0f;
    float avail = (float)tex.screen_height - 24.0f - header - footer - pad;
    float row_h = 30.0f;
    if (row_h * (float)rows.size() > avail && rows.size() > 0)
        row_h = std::max(20.0f, avail / (float)rows.size());
    const float panel_h = header + footer + row_h * (float)rows.size() + pad;
    float panel_x = browser ? margin : ((float)tex.screen_width - panel_w) * 0.5f;
    float panel_y = browser ? margin : ((float)tex.screen_height - panel_h) * 0.5f;
    if (panel_y < 8.0f) panel_y = 8.0f;
    int font = row_h < 26.0f ? 16 : 18;

    ray::DrawRectangle((int)panel_x, (int)panel_y, (int)panel_w, (int)panel_h, ray::Color{8, 10, 18, 230});
    ray::DrawRectangleLines((int)panel_x, (int)panel_y, (int)panel_w, (int)panel_h, ray::Color{220, 180, 60, 255});

    std::string title = "Workout";
    if (mode == Mode::FIELD) title += "  -  stopped";
    title += spec.kind == 1 ? "   Drill" : "   Pattern";
    ray::DrawText(title.c_str(), (int)panel_x + (int)pad, (int)panel_y + 14, 26, ray::Color{255, 220, 120, 255});

    float y = panel_y + header;
    for (int i = 0; i < (int)rows.size(); ++i) {
        bool selected = i == index;
        if (selected)
            ray::DrawRectangle((int)panel_x + 8, (int)y, (int)panel_w - 16, (int)row_h - 2, ray::Color{255, 210, 80, 40});
        ray::Color color = selected ? ray::Color{255, 236, 170, 255} : ray::WHITE;
        std::string label = row_label(rows[i]);
        if (selected && adjusting && is_numeric(rows[i])) label += "  (adjust)";
        ray::DrawText(label.c_str(), (int)panel_x + (int)pad, (int)y + 4, font, color);

        std::string value = row_value(rows[i]);
        float value_x = panel_x + 230.0f;
        bool icons = rows[i] == Row::SEQUENCE && !spec.sequence.empty();
        if (icons) {
            float ix = value_x;
            float iy = y + 4.0f;
            float isize = std::max(16.0f, row_h - 10.0f);
            float max_x = panel_x + panel_w - pad - (is_adjustable(rows[i]) ? 78.0f : 8.0f);
            std::string phrase;
            auto flush = [&]() {
                if (phrase.empty()) return;
                if (ix > value_x) ix += 8.0f;
                ix = draw_phrase_icons(phrase, ix, iy, isize, max_x);
                phrase.clear();
            };
            for (char c : spec.sequence) {
                if (c == '|') flush();
                else phrase.push_back(c);
            }
            flush();
        } else if (!value.empty()) {
            ray::DrawText(value.c_str(), (int)value_x, (int)y + 4, font, color);
        }

        bool adjustable = is_adjustable(rows[i]);
        float buttons_x = panel_x + panel_w - pad - 68.0f;
        Hit body;
        body.x = panel_x + 8;
        body.y = y;
        body.w = adjustable ? (buttons_x - body.x - 6.0f) : (panel_w - 16.0f);
        body.h = row_h - 2;
        body.row = i;
        body.part = 0;
        if (adjustable) {
            ray::DrawRectangle((int)buttons_x, (int)y + 2, 28, (int)row_h - 6, ray::Color{255, 255, 255, 30});
            ray::DrawText("-", (int)buttons_x + 8, (int)y + 4, font, ray::WHITE);
            ray::DrawRectangle((int)(buttons_x + 36), (int)y + 2, 28, (int)row_h - 6, ray::Color{255, 255, 255, 30});
            ray::DrawText("+", (int)(buttons_x + 44), (int)y + 4, font, ray::WHITE);
            hits.push_back(Hit{buttons_x, y + 2, 28, row_h - 6, i, 1});
            hits.push_back(Hit{buttons_x + 36, y + 2, 28, row_h - 6, i, 2});
        }
        hits.push_back(body);
        y += row_h;
    }

    std::string hint1 = spec.kind == 1
        ? "Fully random don and ka. A correct hit slides the lane to the next note."
        : "Interval is the note length. Pattern every is start to start, in beats.";
    std::string hint2 = "d/k don/ka   D/K big   0 rest. Shift nudges faster. " + key_name() + " plays.";
    ray::DrawText(hint1.c_str(), (int)panel_x + (int)pad, (int)(panel_y + panel_h - 52), 16, ray::Color{180, 180, 180, 255});
    ray::DrawText(hint2.c_str(), (int)panel_x + (int)pad, (int)(panel_y + panel_h - 28), 16, ray::Color{180, 180, 180, 255});

    if (browser) {
        float bx = panel_x + panel_w + gap;
        float by = margin;
        float bh = (float)tex.screen_height - margin * 2.0f;
        if (bx + browser_w_now > (float)tex.screen_width - 8.0f)
            browser_w_now = (float)tex.screen_width - 8.0f - bx;
        draw_pattern_browser(bx, by, browser_w_now, bh);
    }
}

void DrillMenu::draw_pattern_browser(float x, float y, float w, float h) const {
    browser_open = true;
    browser_x = x;
    browser_y = y;
    browser_w = w;
    browser_h = h;
    if (w < 80.0f || h < 80.0f) return;

    ray::DrawRectangle((int)x, (int)y, (int)w, (int)h, ray::Color{8, 10, 18, 230});
    ray::DrawRectangleLines((int)x, (int)y, (int)w, (int)h, ray::Color{220, 180, 60, 255});
    ray::DrawText("Patterns", (int)x + 16, (int)y + 12, 26, ray::Color{255, 220, 120, 255});
    float add_w = 96.0f;
    float add_h = 28.0f;
    float add_x = x + w - 16.0f - add_w;
    float add_y = y + 12.0f;
    ray::DrawRectangle((int)add_x, (int)add_y, (int)add_w, (int)add_h, ray::Color{70, 150, 90, 230});
    ray::DrawText("Add all", (int)add_x + 14, (int)add_y + 6, 16, ray::WHITE);
    chips.push_back(Chip{add_x, add_y, add_w, add_h, -1, -1, true});

    const char* labels[7] = {"All", "1", "2", "3", "4", "5", "6"};
    float chip_y = y + 48.0f;
    float chip_x = x + 12.0f;
    float chip_h = 28.0f;
    for (int i = 0; i < 7; ++i) {
        float chip_w = (i == 0) ? 52.0f : 36.0f;
        bool on = hit_filter == i;
        ray::DrawRectangle((int)chip_x, (int)chip_y, (int)chip_w, (int)chip_h,
                           on ? ray::Color{255, 210, 80, 220} : ray::Color{255, 255, 255, 28});
        ray::DrawText(labels[i], (int)chip_x + 8, (int)chip_y + 6, 16, on ? ray::Color{20, 16, 8, 255} : ray::WHITE);
        chips.push_back(Chip{chip_x, chip_y, chip_w, chip_h, -1, i});
        chip_x += chip_w + 6.0f;
    }
    ray::DrawText("Click a pattern to add it. Add all uses this filter.",
                  (int)x + 14, (int)chip_y + 34, 15, ray::Color{180, 180, 180, 255});

    float list_y = chip_y + 58.0f;
    float list_h = y + h - 14.0f - list_y;
    if (list_h < 20.0f) return;
    float list_x = x + 12.0f;
    float list_w = w - 24.0f;

    const auto& catalog = workout_catalog();
    const float icon = 30.0f;
    const float cell_h = 44.0f;
    float cursor_x = list_x;
    float cursor_y = list_y - pattern_scroll;
    float content_bottom = list_y;

    int sx = virtual_to_screen_x(list_x);
    int sy = virtual_to_screen_y(list_y);
    int sx2 = virtual_to_screen_x(list_x + list_w);
    int sy2 = virtual_to_screen_y(list_y + list_h);
    ray::BeginScissorMode(sx, sy, std::max(0, sx2 - sx), std::max(0, sy2 - sy));

    for (int i = 0; i < (int)catalog.size(); ++i) {
        const std::string& notes = catalog[i];
        if (hit_filter > 0 && (int)notes.size() != hit_filter) continue;
        float cell_w = std::max(48.0f, (float)notes.size() * (icon + 2.0f) + 12.0f);
        if (cursor_x > list_x && cursor_x + cell_w > list_x + list_w) {
            cursor_x = list_x;
            cursor_y += cell_h + 6.0f;
        }
        bool visible = cursor_y + cell_h >= list_y && cursor_y <= list_y + list_h;
        if (visible) {
            ray::DrawRectangle((int)cursor_x, (int)cursor_y, (int)cell_w, (int)cell_h, ray::Color{255, 255, 255, 18});
            draw_phrase_icons(notes, cursor_x + 6.0f, cursor_y + 6.0f, icon - 2.0f, cursor_x + cell_w - 4.0f);
        }
        if (cursor_y + cell_h >= list_y && cursor_y <= list_y + list_h)
            chips.push_back(Chip{cursor_x, cursor_y, cell_w, cell_h, i, -1});
        cursor_x += cell_w + 8.0f;
        content_bottom = std::max(content_bottom, cursor_y + cell_h);
    }
    ray::EndScissorMode();

    float overflow = (content_bottom + pattern_scroll) - (list_y + list_h);
    pattern_scroll_max = std::max(0.0f, overflow);
    if (pattern_scroll > pattern_scroll_max) pattern_scroll = pattern_scroll_max;
}
