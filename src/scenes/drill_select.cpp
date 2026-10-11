#include "drill_select.h"
#include "../libs/input.h"

void DrillSelectScreen::on_screen_start() {
    Screen::on_screen_start();
    tex.load_folder("game", "notes");
    menu.open(DrillMenu::Mode::SELECT);
}

std::optional<Screens> DrillSelectScreen::update() {
    if (auto early = Screen::update()) return early;

    bool start_key = false;
    if (global_data.config && !menu.is_editing())
        start_key = check_key_pressed(global_data.config->keys.drill_key);

    DrillMenu::Action action = menu.update(true);
    if (start_key) action = DrillMenu::Action::START;

    if (action == DrillMenu::Action::START)
        return on_screen_end(Screens::GAME_DRILL);
    if (action == DrillMenu::Action::BACK)
        return on_screen_end(Screens::ENTRY);
    return std::nullopt;
}

void DrillSelectScreen::draw() {
    ray::ClearBackground(ray::Color{16, 18, 28, 255});
    menu.draw();
}
