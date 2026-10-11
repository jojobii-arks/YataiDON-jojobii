#pragma once

#include "../../../libs/scores.h"
#include "../../../libs/script.h"

#include <array>
#include <optional>

class ScoreHistory : public LuaScript {
public:
    ScoreHistory(const std::array<std::optional<Score>, 5>& scores, double current_ms);

    void update(double current_ms);
    void draw();

private:
    sol::protected_function fn_update, fn_draw;
};
