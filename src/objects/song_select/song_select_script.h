#pragma once
#include "../../libs/script.h"

class BaseBox;
class SongSelectPlayer;
class Navigator;
class DiffSortSelect;
class GenreBG;

class SongSelectScript : public LuaScript {
private:
    double clock_offset_ms = 0.0;
    std::optional<double> fast_open_started_ms;

    double animation_ms();

    template<typename... Args>
    void call(sol::protected_function& fn, std::string_view context, Args&&... args) {
        struct ClockScope {
            std::optional<double> previous = script_manager.current_ms_override;

            ~ClockScope() { script_manager.current_ms_override = previous; }
        } scope;

        script_manager.current_ms_override = animation_ms();

        LuaScript::call(fn, context, std::forward<Args>(args)...);
    }

    sol::protected_function fn_update;
    sol::protected_function fn_restart_text_fade;
    sol::protected_function fn_draw_footer;
    sol::protected_function fn_draw_overlays;
    sol::protected_function fn_draw_top;
    sol::protected_function fn_draw_box;
    sol::protected_function fn_draw_box_bg;
    sol::protected_function fn_draw_background;
    sol::protected_function fn_draw_genre_bg;
    sol::protected_function fn_draw_selector;
    sol::protected_function fn_draw_option_panel;
    sol::protected_function fn_draw_sort_window;

    sol::object box_to_lua(BaseBox* box);

public:
    SongSelectScript();
    void start_folder_open();
    void finish_folder_open();
    void update(double current_ms);
    void restart_text_fade();
    void draw_footer();
    void draw_overlays(int state);
    void draw_top(float dan_progress);

    bool draw_box(BaseBox* box);
    bool has_draw_box() const { return fn_draw_box.valid(); }   // the skin draws the wheel itself
    bool draw_box_bg(BaseBox* box);
    bool has_box_bg() const { return fn_draw_box_bg.valid(); }
    bool draw_background(Navigator* nav);
    bool draw_genre_bg(GenreBG* genre_bg, float start_pos, float end_pos, BaseBox* folder);
    bool draw_selector(SongSelectPlayer* player, bool is_half, float fade_in, int pass);
    bool draw_option_panel(SongSelectPlayer* player, int kind);

    bool has_sort_window() const { return fn_draw_sort_window.valid(); }
    bool draw_sort_window(DiffSortSelect* window);
};
