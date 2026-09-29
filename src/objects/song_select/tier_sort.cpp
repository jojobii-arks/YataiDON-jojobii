#include "tier_sort.h"
#include "../../libs/audio.h"
#include "../../libs/text.h"
#include "../../libs/global_data.h"

TierSortSelect::TierSortSelect(std::unordered_map<std::string, int> counts,
                               const std::string& prev_tier)
    : local_counts(std::move(counts)), prev_tier(prev_tier) {
    tier_names = tier_list.tiers();
    if (tier_names.empty()) {
        tier_names = {"SSS", "SS", "S+", "S", "A", "B", "C", "D", "E", "F", "X"};
    }
    num_boxes = (int)tier_names.size();

    selected_box = -1;
    for (int i = 0; i < num_boxes; i++) {
        if (tier_names[i] == prev_tier) {
            selected_box = i;
            break;
        }
    }

    bg_resize = (TextureResizeAnimation*)tex.get_animation(19);
    fade_in = (FadeAnimation*)tex.get_animation(20);
    box_flicker = (FadeAnimation*)tex.get_animation(21);
    bg_resize->start();
    fade_in->start();
    box_flicker->start();

    t_background = tex.get_texture("diff_sort/background");

    int font_size = (int)(28 * tex.screen_scale);
    title_label = std::make_unique<OutlinedText>("Tier Sort", (int)(36 * tex.screen_scale),
                                                 ray::WHITE, ray::BLACK, false);
    back_label = std::make_unique<OutlinedText>("Back", font_size, ray::WHITE, ray::BLACK, false);
    for (const auto& name : tier_names) {
        tier_labels.push_back(std::make_unique<OutlinedText>(name, font_size, ray::WHITE, ray::BLACK, false));
        tier_labels_hi.push_back(std::make_unique<OutlinedText>(name, font_size, ray::YELLOW, ray::BLACK, false));
    }
    refresh_count_label();

    audio.play_sound("voice_diff_sort_enter", VolumePreset::VOICE);
}

void TierSortSelect::refresh_count_label() {
    int n = 0;
    if (selected_box >= 0 && selected_box < (int)tier_names.size()) {
        auto it = local_counts.find(tier_names[selected_box]);
        if (it != local_counts.end()) n = it->second;
    }
    std::string text = (selected_box < 0) ? "Cancel" : (std::to_string(n) + " charts");
    count_label = std::make_unique<OutlinedText>(text, (int)(22 * tex.screen_scale),
                                                 ray::WHITE, ray::BLACK, false);
}

const std::string& TierSortSelect::tier_at(int index) const {
    static const std::string empty;
    if (index < 0 || index >= (int)tier_names.size()) return empty;
    return tier_names[index];
}

void TierSortSelect::update(double current_ms) {
    bg_resize->update(current_ms);
    fade_in->update(current_ms);
    box_flicker->update(current_ms);
}

std::optional<int> TierSortSelect::input_select() {
    if (selected_box == -1) return -1;
    return selected_box;
}

void TierSortSelect::input_left() {
    selected_box = std::max(selected_box - 1, -1);
    refresh_count_label();
}

void TierSortSelect::input_right() {
    selected_box = std::min(selected_box + 1, num_boxes - 1);
    refresh_count_label();
}

void TierSortSelect::draw() {
    ray::DrawRectangle(0, 0, tex.screen_width, tex.screen_height, ray::Fade(ray::BLACK, 0.6f));
    if (t_background)
        tex.draw_texture(t_background, {.scale=(float)bg_resize->attribute, .center=true});

    float alpha = (float)fade_in->attribute;
    float title_x = (tex.screen_width - title_label->width) * 0.5f;
    title_label->draw({.x=title_x, .y=tex.screen_height * 0.12f, .fade=alpha});

    // Back + tier letters in a single row (absolute screen coords).
    float box_w = 72.0f * tex.screen_scale;
    float box_h = 88.0f * tex.screen_scale;
    float gap = 6.0f * tex.screen_scale;
    int total = num_boxes + 1;
    float row_w = total * box_w + (total - 1) * gap;
    float start_x = (tex.screen_width - row_w) * 0.5f;
    float y = tex.screen_height * 0.42f;

    float cursor_x = start_x;
    auto draw_cell = [&](bool selected, OutlinedText* label) {
        ray::Color fill = selected ? ray::Color{220, 180, 60, 255} : ray::Color{55, 55, 65, 255};
        ray::DrawRectangleRounded(
            ray::Rectangle{cursor_x, y, box_w, box_h}, 0.15f, 6,
            ray::Fade(fill, alpha));
        if (selected) {
            float pulse = (float)box_flicker->attribute;
            ray::DrawRectangleRoundedLinesEx(
                ray::Rectangle{cursor_x, y, box_w, box_h}, 0.15f, 6, 2.0f * tex.screen_scale,
                ray::Fade(ray::WHITE, pulse * alpha));
        }
        if (label) {
            label->draw({
                .x = cursor_x + (box_w - label->width) * 0.5f,
                .y = y + (box_h - label->height) * 0.5f,
                .fade = alpha
            });
        }
        cursor_x += box_w + gap;
    };

    draw_cell(selected_box == -1, back_label.get());
    for (int i = 0; i < num_boxes; i++) {
        OutlinedText* label = (selected_box == i) ? tier_labels_hi[i].get() : tier_labels[i].get();
        draw_cell(selected_box == i, label);
    }

    if (count_label) {
        float cx = (tex.screen_width - count_label->width) * 0.5f;
        count_label->draw({.x=cx, .y=y + box_h + 24.0f * tex.screen_scale, .fade=alpha});
    }
}
