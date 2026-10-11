#include "ura_switch.h"

UraSwitchAnimation::UraSwitchAnimation() {
    if (!load("UraSwitchAnimation", "ura_switch")) return;
    fn_start  = lua_object["start"];
    fn_update = lua_object["update"];
    fn_draw   = lua_object["draw"];
}

void UraSwitchAnimation::start(bool is_backwards) {
    call(fn_start, "UraSwitchAnimation:start", is_backwards);
}

void UraSwitchAnimation::update(double current_ms) {
    call(fn_update, "UraSwitchAnimation:update", current_ms);
}

void UraSwitchAnimation::draw() {
    call(fn_draw, "UraSwitchAnimation:draw");
}
