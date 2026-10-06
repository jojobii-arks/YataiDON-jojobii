#pragma once

#include "../../libs/config.h"
#include "../../libs/global_data.h"
#include "../../libs/scores.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

// F3 overlay for tuning the Hidden modifier.
// - Hide distance (% of approach lane) — player data + optional live apply callback
// - Fade smoothness (% of hide zone) — persisted in config.toml [hidden]
class HiddenHelper {
public:
    bool open = false;
    int selected = 0; // 0 = distance, 1 = fade
    int distance = 0;
    int fade = 25;

    // Set by the main loop when a GameScreen (or subclass) is active so distance
    // changes apply mid-song without restarting.
    std::function<void(int)> apply_live_distance;

    static constexpr int DISTANCE_STEPS[] = {0, 10, 15, 20, 25, 30, 35, 40, 50, 60};
    static constexpr int DISTANCE_STEP_COUNT = (int)(sizeof(DISTANCE_STEPS) / sizeof(DISTANCE_STEPS[0]));

    struct RowRects { ray::Rectangle label{}, minus{}, plus{}, value{}; };
    RowRects row_rects[2]{};

    void toggle() {
        open = !open;
        if (open) {
            selected = 0;
            sync_from_sources();
        } else {
            persist();
        }
    }

    void sync_from_sources() {
        if (_scores_manager_ptr) {
            auto pd = scores_manager.get_player_data(scores_manager.player_1);
            distance = pd ? std::clamp(pd->modifier_hidden, 0, 100) : 0;
        }
        if (global_data.config)
            fade = std::clamp(global_data.config->hidden.fade_percent, 0, 100);
    }

    void persist() {
        if (global_data.config) {
            global_data.config->hidden.fade_percent = std::clamp(fade, 0, 100);
            save_config(*global_data.config);
        }
        save_distance_to_player();
    }

    void save_distance_to_player() {
        if (!_scores_manager_ptr) return;
        auto pd = scores_manager.get_player_data(scores_manager.player_1).value_or(PlayerData{});
        pd.player_id = scores_manager.player_1;
        pd.modifier_hidden = distance;
        scores_manager.player_1_data.modifier_hidden = distance;
        scores_manager.save_player_data(pd);
    }

    static int nearest_distance_index(int value) {
        int best = 0;
        int best_d = std::abs(DISTANCE_STEPS[0] - value);
        for (int i = 1; i < DISTANCE_STEP_COUNT; i++) {
            int d = std::abs(DISTANCE_STEPS[i] - value);
            if (d < best_d) { best = i; best_d = d; }
        }
        return best;
    }

    void step_distance(int dir) {
        int idx = nearest_distance_index(distance);
        idx = (idx + dir + DISTANCE_STEP_COUNT) % DISTANCE_STEP_COUNT;
        distance = DISTANCE_STEPS[idx];
        save_distance_to_player();
        if (apply_live_distance) apply_live_distance(distance);
    }

    void step_fade(int dir) {
        int v = std::clamp(fade, 0, 100);
        v = ((v + 4) / 5) * 5;
        v += dir * 5;
        if (v < 0) v = 100;
        if (v > 100) v = 0;
        fade = v;
        if (global_data.config)
            global_data.config->hidden.fade_percent = fade;
    }

    void handle_click(ray::Vector2 mouse) {
        for (int i = 0; i < 2; i++) {
            if (ray::CheckCollisionPointRec(mouse, row_rects[i].minus)) {
                selected = i;
                if (i == 0) step_distance(-1); else step_fade(-1);
                return;
            }
            if (ray::CheckCollisionPointRec(mouse, row_rects[i].plus)) {
                selected = i;
                if (i == 0) step_distance(+1); else step_fade(+1);
                return;
            }
            if (ray::CheckCollisionPointRec(mouse, row_rects[i].label)) {
                selected = i;
                return;
            }
        }
    }

    void update(ray::Vector2 mouse_world) {
        if (ray::IsKeyPressed(ray::KEY_F3)) {
            toggle();
            return;
        }
        if (!open) return;

        if (ray::IsKeyPressed(ray::KEY_UP) || ray::IsKeyPressed(ray::KEY_W) ||
            ray::IsKeyPressed(ray::KEY_DOWN) || ray::IsKeyPressed(ray::KEY_S)) {
            selected = (selected + 1) % 2;
        }

        const bool left  = ray::IsKeyPressed(ray::KEY_LEFT)  || ray::IsKeyPressed(ray::KEY_A);
        const bool right = ray::IsKeyPressed(ray::KEY_RIGHT) || ray::IsKeyPressed(ray::KEY_D);
        if (left || right) {
            int dir = right ? +1 : -1;
            if (selected == 0) step_distance(dir);
            else               step_fade(dir);
        }

        if (ray::IsMouseButtonPressed(ray::MOUSE_BUTTON_LEFT))
            handle_click(mouse_world);
    }

    void draw() {
        if (!open) return;

        const float panel_w = 360.0f;
        const float panel_h = 176.0f;
        const float x = 24.0f;
        const float y = 24.0f;
        const float row_h = 40.0f;
        const float pad = 12.0f;

        ray::DrawRectangleRounded({x, y, panel_w, panel_h}, 0.08f, 8, ray::Fade(ray::BLACK, 0.78f));
        ray::DrawRectangleRoundedLinesEx({x, y, panel_w, panel_h}, 0.08f, 8, 2.0f, ray::Fade(ray::SKYBLUE, 0.7f));

        ray::DrawText("Hidden Helper  (F3)", (int)(x + pad), (int)(y + 10), 18, ray::SKYBLUE);
        ray::DrawText("Up/Down select   Left/Right adjust", (int)(x + pad), (int)(y + 32), 12, ray::LIGHTGRAY);

        const char* labels[2] = {"Hide distance", "Fade smoothness"};
        std::string values[2] = {
            distance <= 0 ? std::string("Off") : (std::to_string(distance) + "%"),
            std::to_string(fade) + "%"
        };
        const char* hints[2] = {
            "How far from the judge notes vanish",
            "0% = hard cut, 100% = long soft fade"
        };

        for (int i = 0; i < 2; i++) {
            float ry = y + 54.0f + i * row_h;
            bool sel = selected == i;
            if (sel)
                ray::DrawRectangleRec({x + 6, ry - 2, panel_w - 12, row_h - 4}, ray::Fade(ray::SKYBLUE, 0.18f));

            ray::DrawText(labels[i], (int)(x + pad), (int)(ry + 2), 16, sel ? ray::YELLOW : ray::WHITE);

            float btn = 22.0f;
            float vx = x + panel_w - pad - 110.0f;
            row_rects[i].minus = {vx, ry + 2, btn, btn};
            row_rects[i].value = {vx + btn + 4, ry + 2, 56.0f, btn};
            row_rects[i].plus  = {vx + btn + 4 + 56.0f + 4, ry + 2, btn, btn};
            row_rects[i].label = {x + pad, ry, panel_w * 0.45f, row_h};

            ray::DrawRectangleRec(row_rects[i].minus, ray::Fade(ray::WHITE, 0.2f));
            ray::DrawText("-", (int)row_rects[i].minus.x + 6, (int)row_rects[i].minus.y + 2, 16, ray::WHITE);
            ray::DrawRectangleRec(row_rects[i].plus, ray::Fade(ray::WHITE, 0.2f));
            ray::DrawText("+", (int)row_rects[i].plus.x + 5, (int)row_rects[i].plus.y + 2, 16, ray::WHITE);

            int tw = ray::MeasureText(values[i].c_str(), 16);
            ray::DrawText(values[i].c_str(),
                          (int)(row_rects[i].value.x + (row_rects[i].value.width - tw) * 0.5f),
                          (int)row_rects[i].value.y + 3, 16, ray::WHITE);

            ray::DrawText(hints[i], (int)(x + pad), (int)(ry + 22), 11, ray::GRAY);
        }

        // Preview: left = judge, red = fully hidden, orange = fade, grey = visible
        float preview_y = y + panel_h - 22.0f;
        float preview_x = x + pad;
        float preview_w = panel_w - pad * 2;
        ray::DrawRectangle((int)preview_x, (int)preview_y, (int)preview_w, 8, ray::Fade(ray::WHITE, 0.12f));
        float hide_w = preview_w * (distance <= 0 ? 0.0f : distance / 100.0f);
        float fade_w = hide_w * (fade / 100.0f);
        ray::DrawRectangle((int)preview_x, (int)preview_y, (int)std::max(0.0f, hide_w - fade_w), 8, ray::Fade(ray::RED, 0.4f));
        constexpr int segs = 12;
        for (int s = 0; s < segs; s++) {
            float t0 = (float)s / segs;
            float a = 1.0f - t0;
            float sx = preview_x + (hide_w - fade_w) + fade_w * t0;
            float sw = fade_w / segs + 1.0f;
            if (fade_w > 0.0f)
                ray::DrawRectangle((int)sx, (int)preview_y, (int)sw, 8, ray::Fade(ray::ORANGE, 0.15f + 0.5f * a));
        }
        ray::DrawText("judge", (int)preview_x, (int)(preview_y - 12), 10, ray::GRAY);
        ray::DrawText("lane ->", (int)(preview_x + preview_w - 40), (int)(preview_y - 12), 10, ray::GRAY);
    }
};

inline HiddenHelper hidden_helper;
