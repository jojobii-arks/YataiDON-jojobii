#pragma once

#include "../libs/screen.h"
#include "../objects/game/drill_menu.h"

class DrillSelectScreen : public Screen {
public:
    DrillSelectScreen() : Screen("drill_select") {}

    void on_screen_start() override;
    std::optional<Screens> update() override;
    void draw() override;

private:
    DrillMenu menu;
};
