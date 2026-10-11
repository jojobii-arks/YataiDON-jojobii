#pragma once

#include "game.h"
#include "../libs/drill.h"
#include "../objects/game/drill_menu.h"

// Real judgment and drum. No chart file, no music file, no score save.
// Pattern mode scrolls on a clock. Drill mode advances one note per correct hit.
class DrillGameScreen : public GameScreen {
public:
    DrillGameScreen() : GameScreen("game") {}

    void on_screen_start() override;
    Screens on_screen_end(Screens next_screen) override;
    std::optional<Screens> update() override;
    void draw() override;

private:
    DrillChart chart;
    DrillMenu menu;
    bool engaged = false;
    bool finished = false;
    bool click_loaded = false;
    bool fresh_next = true;
    long long last_beat = -1;
    int cursor = 0;
    int click_cursor = -1;
    double visual_ms = 0;
    double slide_from = 0;
    double slide_to = 0;
    double slide_start = 0;
    bool sliding = false;
    double session_origin = 0;
    double session_bank = 0;
    double shown_now = 0;

    void arm_chart(bool create_player);
    void engage();
    void disengage();
    void tick_click();
    void tick_extend();
    double elapsed_sec(double now) const;
    void draw_stats() const;
};
