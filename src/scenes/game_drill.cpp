#include "game_drill.h"
#include "../libs/audio.h"
#include "../libs/input.h"
#include "../libs/time.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
constexpr double JUDGE_TAIL_MS = 150.0;
constexpr double DRILL_SLIDE_MS = 140.0;

const char* grade_for(double accuracy) {
    if (accuracy >= 99.5) return "SSS";
    if (accuracy >= 98.0) return "SS";
    if (accuracy >= 95.0) return "S";
    if (accuracy >= 85.0) return "A";
    if (accuracy >= 70.0) return "B";
    if (accuracy >= 60.0) return "C";
    if (accuracy >= 50.0) return "D";
    return "F";
}

std::string clock_text(double seconds) {
    int sec = (int)std::max(0.0, seconds);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", sec / 60, sec % 60);
    return buf;
}
}

void DrillGameScreen::arm_chart(bool create_player) {
    chart = build_drill_chart(global_data.config->drill);
    parser = SongParser(make_synthetic_parser(chart));
    bpm = (float)chart.bpm;
    ms_from_start = 0;
    last_beat = -1;
    finished = false;
    fresh_next = true;
    cursor = 0;
    click_cursor = -1;
    session_bank = 0;
    session_origin = 0;
    if (create_player) {
        Modifiers mods;
        if (global_data.force_auto_play) mods.auto_play = true;
        players.push_back(std::make_unique<Player>(
            parser, global_data.player_num, DRILL_DIFFICULTY, false, mods));
    } else if (!players.empty() && players[0]) {
        players[0]->reload_chart(parser, DRILL_DIFFICULTY);
    }
    if (!players.empty() && players[0]) {
        players[0]->configure_workout(true, chart.hold, chart.spec.alternate, false);
        players[0]->reset_workout_stats();
    }
}

void DrillGameScreen::on_screen_start() {
    Screen::on_screen_start();
    if (global_data.player_num != PlayerNum::P1 && global_data.player_num != PlayerNum::P2)
        global_data.player_num = PlayerNum::P1;

    mask_shader = load_shader("shader/dummy.vs", "shader/mask.fs");
    JudgePos::X = tex.skin_config[SC::JUDGE_POS].x;
    JudgePos::Y = tex.skin_config[SC::JUDGE_POS].y;
    song_started = false;
    paused = false;
    score_saved = true;
    engaged = false;
    finished = false;
    ms_from_start = 0;
    start_ms = 0;
    shown_now = 0;

    load_hitsounds();
    click_loaded = load_drill_click();
    arm_chart(true);
    background.emplace(global_data.player_num, bpm, "");
    menu.open(DrillMenu::Mode::FIELD);
}

Screens DrillGameScreen::on_screen_end(Screens next_screen) {
    if (click_loaded) audio.stop_sound("drill_click");
    engaged = false;
    if (!players.empty() && players[0])
        players[0]->configure_workout(false, false, false, false);
    return GameScreen::on_screen_end(next_screen);
}

void DrillGameScreen::engage() {
    if (finished) arm_chart(false);
    if (!chart.hold && chart.steps.empty()) return;
    bool fresh = fresh_next;
    fresh_next = false;
    double now = get_current_ms();
    if (fresh) {
        cursor = 0;
        click_cursor = -1;
        session_bank = 0;
        if (!players.empty() && players[0]) players[0]->reset_workout_stats();
    }
    if (chart.hold && !chart.steps.empty()) {
        if (cursor < 0 || cursor >= (int)chart.steps.size()) cursor = 0;
        visual_ms = chart.steps[cursor];
        sliding = false;
        ms_from_start = visual_ms;
    }
    engaged = true;
    finished = false;
    session_origin = now;
    start_ms = now - ms_from_start;
    if (chart.beat_ms <= 0.0 || ms_from_start <= 0.0001) last_beat = -1;
    else last_beat = (long long)std::floor(ms_from_start / chart.beat_ms + 1e-9);
    if (!players.empty() && players[0])
        players[0]->configure_workout(true, chart.hold, chart.spec.alternate, false);
}

void DrillGameScreen::disengage() {
    if (engaged && session_origin > 0) {
        session_bank += std::max(0.0, (get_current_ms() - session_origin) / 1000.0);
        session_origin = 0;
    }
    engaged = false;
    if (click_loaded) audio.stop_sound("drill_click");
}

double DrillGameScreen::elapsed_sec(double now) const {
    double live = (engaged && session_origin > 0) ? std::max(0.0, (now - session_origin) / 1000.0) : 0.0;
    return session_bank + live;
}

void DrillGameScreen::tick_click() {
    if (chart.hold || !chart.spec.metronome || !click_loaded || chart.beat_ms <= 0.0 || ms_from_start < 0.0) return;
    long long beat = (long long)std::floor(ms_from_start / chart.beat_ms + 1e-9);
    if (beat <= last_beat) return;
    last_beat = beat;
    double beat_time = (double)beat * chart.beat_ms;
    if (!chart.endless && beat_time > chart.generated_until_ms + 0.5) return;
    audio.play_sound("drill_click", VolumePreset::SOUND);
}

void DrillGameScreen::tick_extend() {
    if (!chart.endless) return;
    if (chart.hold) {
        if ((int)chart.steps.size() - cursor >= 50) return;
    } else if (ms_from_start <= chart.place_t - chart.beat_ms * 8.0) {
        return;
    }
    int extra = chart.hold ? 100 : (chart.spec.random_pool ? 12 : DRILL_ENDLESS_CHUNK);
    NoteList added = extend_drill_chart(chart, extra);
    if (added.notes.empty() || !parser.has_value()) return;
    parser->set_synthetic_notes(chart.notes);
    if (!players.empty() && players[0]) {
        players[0]->adopt_parser(parser);
        players[0]->append_notes(added);
    }
}

std::optional<Screens> DrillGameScreen::update() {
    if (auto early = Screen::update()) return early;
    if (players.empty() || !players[0]) return on_screen_end(Screens::DRILL_SELECT);

    double now = get_frame_ms();
    shown_now = now;
    bool toggle = false;
    if (global_data.config && !menu.is_editing())
        toggle = check_key_pressed(global_data.config->keys.drill_key);
    if (toggle) {
        menu.close_editing();
        if (engaged) disengage();
        else engage();
    }

    if (!engaged) {
        DrillMenu::Action action = menu.update(true);
        if (action == DrillMenu::Action::SPEC_CHANGED) {
            disengage();
            arm_chart(false);
        } else if (action == DrillMenu::Action::RESTART) {
            disengage();
            arm_chart(false);
            engage();
        } else if (action == DrillMenu::Action::PLAY) {
            menu.close_editing();
            engage();
        } else if (action == DrillMenu::Action::BACK) {
            return on_screen_end(Screens::DRILL_SELECT);
        }
    } else {
        menu.update(false);
    }

    if (engaged) {
        if (chart.hold) {
            tick_extend();
            if (chart.steps.empty() || cursor >= (int)chart.steps.size()) {
                disengage();
                finished = true;
            } else {
                double target = chart.steps[cursor];
                if (sliding && std::abs(slide_to - target) > 0.5) {
                    slide_from = visual_ms;
                    slide_to = target;
                    slide_start = now;
                } else if (!sliding && std::abs(visual_ms - target) > 0.5) {
                    slide_from = visual_ms;
                    slide_to = target;
                    slide_start = now;
                    sliding = true;
                }
                if (sliding) {
                    double t = (now - slide_start) / DRILL_SLIDE_MS;
                    if (t >= 1.0) {
                        sliding = false;
                        visual_ms = slide_to;
                    } else {
                        double u = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
                        visual_ms = slide_from + (slide_to - slide_from) * u;
                    }
                } else {
                    visual_ms = target;
                }
                ms_from_start = visual_ms;
                start_ms = now - ms_from_start;
                if (cursor != click_cursor) {
                    click_cursor = cursor;
                    if (click_loaded) audio.play_sound("drill_click", VolumePreset::SOUND);
                }
            }
        } else {
            ms_from_start = now - start_ms;
            tick_click();
            tick_extend();
            if (!chart.endless && ms_from_start >= chart.generated_until_ms + JUDGE_TAIL_MS) {
                disengage();
                finished = true;
            }
        }
    }

    int good_before = players[0]->get_good();
    if (background.has_value()) background->update(now, bpm);
    players[0]->update(ms_from_start, now, background);

    if (engaged && chart.hold && players[0]->get_good() > good_before) {
        double from = visual_ms;
        cursor++;
        if (cursor >= (int)chart.steps.size()) {
            if (chart.endless) tick_extend();
            if (cursor >= (int)chart.steps.size()) {
                disengage();
                finished = true;
                sliding = false;
            }
        }
        if (engaged && cursor >= 0 && cursor < (int)chart.steps.size()) {
            slide_from = from;
            slide_to = chart.steps[cursor];
            slide_start = now;
            sliding = true;
        }
    }

    if (engaged && players[0]) {
        int limit = std::max(1, chart.spec.max_misses);
        bool tripped = chart.hold
            ? (players[0]->get_bad() >= limit || players[0]->workout.wrong >= limit)
            : (players[0]->get_bad() >= limit);
        if (chart.spec.no_miss && tripped) {
            if (chart.spec.miss_restart) {
                arm_chart(false);
                engage();
            } else {
                disengage();
                finished = true;
            }
        } else if (chart.spec.timer_on && elapsed_sec(now) >= chart.spec.timer_sec) {
            disengage();
            finished = true;
        }
    }
    return std::nullopt;
}

void DrillGameScreen::draw_stats() const {
    if (players.empty() || !players[0]) return;
    const Player& player = *players[0];
    const auto& stats = player.workout;
    int good = player.get_good();
    int ok = player.get_ok();
    int bad = player.get_bad();
    int wrong = stats.wrong;
    int judged = good + ok + bad + wrong;
    double accuracy = judged ? (double)(good + ok) / (double)judged * 100.0 : 0.0;
    double elapsed = elapsed_sec(shown_now > 0 ? shown_now : get_current_ms());
    double notes_per_min = elapsed > 0.5 ? (good + ok) / (elapsed / 60.0) : 0.0;
    int hands = stats.hands[0] + stats.hands[1] + stats.hands[2] + stats.hands[3];
    int left = stats.hands[0] + stats.hands[2];
    int don = stats.hands[0] + stats.hands[1];
    auto pct = [](int n, int d) { return d ? (int)std::lround(100.0 * n / d) : 0; };

    int done = chart.hold ? cursor : (good + ok + bad);
    std::string progress = chart.endless
        ? (std::to_string(done) + " notes")
        : (std::to_string(done) + " / " + std::to_string(chart.hold ? chart.spec.total_notes : (int)chart.steps.size()));

    char perf[160];
    std::snprintf(perf, sizeof(perf), "Performance  %.1f%% %s    Good %d  Okay %d  Miss %d  Wrong %d",
                  accuracy, grade_for(accuracy), good, ok, bad, wrong);
    char hand[160];
    std::snprintf(hand, sizeof(hand), "Hands  L %d%%   R %d%%   Don %d%%   Ka %d%%",
                  pct(left, hands), pct(hands - left, hands), pct(don, hands), pct(hands - don, hands));
    char session[180];
    std::snprintf(session, sizeof(session), "Session  %s    %s    %.0f notes/min    streak %d (best %d)    %d FPS",
                  progress.c_str(), clock_text(elapsed).c_str(), notes_per_min,
                  player.get_combo(), player.get_max_combo(), ray::GetFPS());

    std::vector<std::string> lines;
    lines.push_back(perf);
    if (!chart.hold) {
        std::string live = "Live  ";
        if (!stats.has_last) live += "-";
        else {
            const float good_ms = Timing::GOOD;
            int abs_off = std::abs(stats.last_offset);
            int on_time = std::max(10, (int)std::lround(good_ms * 0.5f));
            int good_lim = (int)std::lround(good_ms);
            if (abs_off <= on_time) live += "On time";
            else if (stats.last_offset < 0 && abs_off <= good_lim) live += "Slightly early " + std::to_string(abs_off) + " ms";
            else if (stats.last_offset < 0) live += "Early " + std::to_string(abs_off) + " ms";
            else if (abs_off <= good_lim) live += "Slightly late " + std::to_string(abs_off) + " ms";
            else live += "Late " + std::to_string(abs_off) + " ms";
        }
        lines.push_back(live);

        int early_n = 0;
        int late_n = 0;
        int nonzero = 0;
        double sum = 0;
        int timing_chrono = stats.timing_n < 32 ? 0 : stats.timing_i;
        for (int i = 0; i < stats.timing_n; ++i) {
            int off = stats.timing_off[(timing_chrono + i) % 32];
            sum += off;
            if (off < 0) { early_n++; nonzero++; }
            else if (off > 0) { late_n++; nonzero++; }
        }
        int early_pct = nonzero ? (int)std::lround(100.0 * early_n / nonzero) : 50;
        int late_pct = nonzero ? 100 - early_pct : 50;
        char balance[96];
        if (stats.timing_n == 0) {
            std::snprintf(balance, sizeof(balance), "Early 50%%   Late 50%%   avg -");
        } else {
            std::snprintf(balance, sizeof(balance), "Early %d%%   Late %d%%   avg %+.1f ms",
                          early_pct, late_pct, sum / stats.timing_n);
        }
        lines.push_back(balance);

        std::string recent = "Recent  ";
        int show = std::min(stats.recent_n, 24);
        int chrono = stats.recent_n < 32 ? 0 : stats.recent_i;
        int skip = stats.recent_n - show;
        for (int i = 0; i < show; ++i) {
            int code = stats.recent[(chrono + skip + i) % 32];
            recent.push_back(code == 1 ? 'G' : code == 2 ? 'O' : code == 3 ? 'M' : 'W');
        }
        if (stats.recent_n == 0) recent += "-";
        lines.push_back(recent);
        const float good_ms = Timing::GOOD;
        const float ok_ms = Timing::OK;
        const float bad_ms = Timing::BAD;
        char window[96];
        std::snprintf(window, sizeof(window), "Window  Good +/-%.0f ms   Okay +/-%.0f ms   Miss +/-%.0f ms",
                      good_ms, ok_ms, bad_ms);
        lines.push_back(window);
    }
    if (chart.spec.alternate) {
        int expect = player.workout_next_hand();
        lines.push_back(expect == 1 ? "Expect  Left" : expect == 2 ? "Expect  Right" : "Expect  either hand");
    }
    lines.push_back(hand);
    lines.push_back(session);

    int width = chart.hold ? 640 : 760;
    int height = (int)lines.size() * 22 + 16;
    ray::DrawRectangle(12, 40, width, height, ray::Color{0, 0, 0, 150});
    int y = 48;
    for (const std::string& line : lines) {
        ray::DrawText(line.c_str(), 24, y, 18, ray::WHITE);
        y += 22;
    }
}

void DrillGameScreen::draw() {
    ray::ClearBackground(ray::BLACK);
    if (background.has_value()) background->draw_back();
    draw_players();
    if (background.has_value()) background->draw_fore();

    std::string key = "f2";
    if (global_data.config) {
        try { key = getKeyString(global_data.config->keys.drill_key); }
        catch (const std::exception&) { key = "f2"; }
    }
    std::string status = engaged ? "Playing" : finished ? "Finished" : "Stopped";
    if (!engaged && !chart.hold && chart.steps.empty()) status = "Add a pattern";
    std::string mods;
    if (chart.spec.alternate) mods += " alt";
    if (!chart.hold && chart.spec.invert) mods += " invert";
    if (!chart.hold && chart.spec.random_pool) mods += " random";
    if (!chart.hold && !chart.spec.metronome) mods += " no-click";
    if (chart.endless) mods += " endless";
    if (chart.spec.no_miss) mods += " no-miss";
    if (chart.spec.timer_on) mods += " timer";
    std::string line = status + "   Workout " + std::string(chart.hold ? "Drill" : "Pattern")
        + "   " + std::to_string(chart.spec.bpm) + " BPM   scroll " +
        ([&] {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1f", chart.spec.scroll);
            return std::string(buf);
        })()
        + mods + "   " + key + (engaged ? " stop" : " play");
    if (finished && !players.empty() && players[0]) {
        int judged = players[0]->get_good() + players[0]->get_ok() + players[0]->get_bad() + players[0]->workout.wrong;
        double accuracy = judged
            ? (double)(players[0]->get_good() + players[0]->get_ok()) / (double)judged * 100.0
            : 0.0;
        char tail[48];
        std::snprintf(tail, sizeof(tail), "   %.1f%% %s", accuracy, grade_for(accuracy));
        line += tail;
    }
    ray::DrawText(line.c_str(), 24, 16, 20, ray::Color{255, 236, 180, 255});

    if (engaged) draw_stats();
    if (!engaged) menu.draw();
}
