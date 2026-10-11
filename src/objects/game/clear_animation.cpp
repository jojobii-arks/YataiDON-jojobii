#include "clear_animation.h"

ClearAnimation::ClearAnimation(bool is_2p) {
    if (!load("ClearAnimation", "clear_animation", is_2p)) return;
    fn_update = lua_object["update"];
    fn_draw   = lua_object["draw"];
}

void ClearAnimation::update(double current_ms) {
    call(fn_update, "ClearAnimation:update", current_ms);
}

void ClearAnimation::draw() {
    call(fn_draw, "ClearAnimation:draw");
}
