#include "fail_animation.h"

FailAnimation::FailAnimation(bool is_2p) {
    if (!load("FailAnimation", "fail_animation", is_2p)) return;
    fn_update = lua_object["update"];
    fn_draw   = lua_object["draw"];
}

void FailAnimation::update(double current_ms) {
    call(fn_update, "FailAnimation:update", current_ms);
}

void FailAnimation::draw() {
    call(fn_draw, "FailAnimation:draw");
}
