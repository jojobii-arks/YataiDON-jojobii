#pragma once

#include "../global/nameplate.h"
#include "../global/chara_3d.h"
#include "neiro.h"
#include "modifier.h"
#include "ura_switch.h"
#include "diff_sort.h"
#include "tier_sort.h"
#include "../../libs/texture.h"

class SongSelectScript;

class SongSelectPlayer {
public:
    PlayerNum player_num;
    PlayerData player_data;
    Difficulty selected_difficulty;
    Difficulty prev_diff;
    std::vector<Difficulty> curr_diffs;
    double last_moved;
    bool selected_song;
    bool is_ready;
    bool is_ura;
    int ura_toggle;
    bool diff_select_move_right;
    std::string search_string;

    bool search_guard = false;
    double search_guard_until_ms = 0.0;

    std::optional<NeiroSelector> neiro_selector;
    std::optional<ModifierSelector> modifier_selector;
    std::optional<UraSwitchAnimation> ura_switch;

    MoveAnimation* diff_selector_move_1;
    MoveAnimation* diff_selector_move_2;
    MoveAnimation* selected_diff_bounce;
    FadeAnimation* selected_diff_fadein;
    FadeAnimation* selected_diff_highlight_fade;
    TextureResizeAnimation* selected_diff_text_resize;
    FadeAnimation* selected_diff_text_fadein;

    std::unique_ptr<Chara3D> chara;
    Nameplate nameplate;

    SongSelectScript* script = nullptr;
    bool selector_handled_by_lua = false;

    bool option_panel_by_lua = false;

    SongSelectPlayer(PlayerNum player_num);

    // Don-chan's size multiplier. The chara's own art is ~270 px tall at 1.0 and
    // reaches up to y 684, which collides with a tall left-hand panel on the pro
    // screen; a screen can ask for a smaller character without touching the wheel.
    float chara_scale = 1.0f;
    // A screen can shift Don and his nameplate without moving the wheel station.
    // Positive moves them down.
    float chara_y = 0.0f;
    float nameplate_y = 0.0f;

    void update(double current_time);
    bool is_voice_playing();

    bool difficulty_decided() const { return selected_difficulty >= Difficulty::EASY && (voice_played || is_ready); }

    SongSelectState select_song();
    void sync_ura(bool ura);
    void init_diff_cursor();
    void reset_selection();
    SongSelectState handle_input_browsing(double current_ms);
    SongSelectState handle_input_selecting();
    std::optional<std::pair<int,int>> handle_input_diff_sort(DiffSortSelect* diff_sort_selector);
    std::optional<std::pair<int, std::string>> handle_input_tier_sort(TierSortSelect* tier_sort_selector);
    std::optional<std::string> handle_input_search();

    void draw_selector(bool is_half, float fade_in);
    void try_lua_selector(bool is_half, float fade_in, int pass);
    void draw_background_diffs(SongSelectState state);
    void draw(SongSelectState state, bool is_half, float diff_fade_in);

private:
    bool voice_played;
    void navigate_difficulty_left();
    void navigate_difficulty_right();
    void toggle_ura_mode();
    void start_background_diffs();

    TextureObject* t_balloon = nullptr;
    TextureObject* t_balloon_half = nullptr;
    TextureObject* t_outline = nullptr;
    TextureObject* t_outline_half = nullptr;
    TextureObject* t_outline_back = nullptr;
    TextureObject* t_outline_back_half = nullptr;
    TextureObject* t_background_diff = nullptr;
    TextureObject* t_background_diff_highlight = nullptr;
    TextureObject* t_bg_diff_text_bg = nullptr;
};
