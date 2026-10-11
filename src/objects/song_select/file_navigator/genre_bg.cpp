#include "genre_bg.h"

GenreBG::GenreBG(const std::string& text_name, std::optional<ray::Color> color, TextureIndex texture_index, float distance)
: texture_index(texture_index) {
    float base_font_size = (float)tex.skin_config[SC::SONG_BOX_NAME].font_size;
    float font_size = base_font_size;
    float name_outline = 5.0f;
    if (utf8_char_count(text_name) >= 30) {
        font_size = base_font_size - 10.0f * tex.screen_scale;
        name_outline = 5.0f * (font_size / base_font_size);
    }
    name = make_unique<OutlinedText>(text_name, (int)font_size, ray::WHITE, ray::BLACK, false, name_outline);

    if (color.has_value()) {
        shader = load_shader("shader/dummy.vs", "shader/colortransform.fs");
        float src[3] = { 142 / 255.0f, 212 / 255.0f, 30 / 255.0f };
        float tgt[3] = { color.value().r / 255.0f, color.value().g / 255.0f, color.value().b / 255.0f };
        int source_loc = ray::GetShaderLocation(shader, "sourceColor");
        int target_loc = ray::GetShaderLocation(shader, "targetColor");
        ray::SetShaderValue(shader, source_loc, src, ray::SHADER_UNIFORM_VEC3);
        ray::SetShaderValue(shader, target_loc, tgt, ray::SHADER_UNIFORM_VEC3);
        shader_loaded = ray::IsShaderValid(shader);
    }

    const double duration_scale = global_data.config->general.fast_transitions ? 0.2 : 1.0;
    stretch = std::make_unique<MoveAnimation>(333 * duration_scale, 20 * tex.screen_scale, false, false, 0, 0, 0, std::nullopt, EaseType::Cubic);
    scale = std::make_unique<TextureResizeAnimation>(100 * duration_scale, 0.9f, false, false, 1.0);
    move = std::make_unique<MoveAnimation>(600 * duration_scale, std::min((float)tex.screen_width, distance) * tex.screen_scale, false, false, 0, stretch->duration*1.5);
    fade = std::make_unique<FadeAnimation>(100 * duration_scale, 0.0, false, false, 1.0);
    stretch->start();
    scale->start();
    move->start();
    fade->start();
    move_left  = nullptr;
    move_right = nullptr;
}

void GenreBG::exit(float left_position, float right_position, FolderBox* center_box) {
    float left_start  = (left_position  < 0.f || left_position  > tex.screen_width) ? 0.f              : left_position;
    float right_start = (right_position < 0.f || right_position > tex.screen_width) ? tex.screen_width : right_position;

    float boundary_left_exit  = tex.skin_config[SC::GENRE_BG_SELECT_BOUNDARY].x;
    float boundary_right_exit = tex.skin_config[SC::GENRE_BG_SELECT_BOUNDARY].width;
    int left_distance  = (int)((boundary_left_exit  - left_start)  * tex.screen_scale);
    int right_distance = (int)((boundary_right_exit - right_start) * tex.screen_scale);

    move_left  = std::make_unique<MoveAnimation>(200, left_distance,  false, false, (int)left_start,  166);
    move_right = std::make_unique<MoveAnimation>(200, right_distance, false, false, (int)right_start, 166);
    float delay = std::max(move_left->duration, move_right->duration);
    fade = std::make_unique<FadeAnimation>(100, 1.0, false, false, 0.0, delay);
    move_left->start();
    move_right->start();
    fade->start();
    center_box->exit_box();
    center_box->fade_in(delay / 2);
}

void GenreBG::fade_out() {
    fade = std::make_unique<FadeAnimation>(300);
    fade->start();
}
void GenreBG::fade_in() {
    fade = std::make_unique<FadeAnimation>(300, 0.0, false, false, 1.0);
    fade->start();
}

bool GenreBG::is_finished() {
    return move->is_finished;
}

bool GenreBG::is_complete() {
    if (move_left != nullptr && move_right != nullptr) {
        return move_left->is_finished && move_right->is_finished;
    }
    return true;
}

void GenreBG::update(double current_ms, FolderBox* box) {
    stretch->update(current_ms);
    scale->update(current_ms);
    move->update(current_ms);
    fade->update(current_ms);
    if (move_left != nullptr) {
        move_left->update(current_ms);
    }
    if (move_right != nullptr) {
        move_right->update(current_ms);
    }
    if (box != nullptr) {
        box->update(current_ms);
    }
}
