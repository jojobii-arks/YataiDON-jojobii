#include "tier_sort.h"
#include "../../libs/audio.h"
#include "../../libs/text.h"
#include "../../libs/global_data.h"

TierSortSelect::TierSortSelect(std::unordered_map<int, std::unordered_map<std::string, int>> counts,
                               int prev_level, const std::string& prev_tier)
    : all_counts(std::move(counts)), prev_level(prev_level), prev_tier(prev_tier) {
    levels = tier_list.levels();
    if (levels.empty())
        levels = {6, 7, 8, 9, 10};

    in_tier_select = false;
    selected_box = level_index(prev_level);  // -1 if prev unknown / unset
    num_boxes = (int)levels.size();

    bg_resize = (TextureResizeAnimation*)tex.get_animation(19);
    fade_in = (FadeAnimation*)tex.get_animation(20);
    box_flicker = (FadeAnimation*)tex.get_animation(21);
    bg_resize->start();
    fade_in->start();
    box_flicker->start();

    t_background = tex.get_texture("diff_sort/background");

    title_label = std::make_unique<OutlinedText>("Tier Sort — Level", (int)(36 * tex.screen_scale),
                                                 ray::WHITE, ray::BLACK, false);
    back_label = std::make_unique<OutlinedText>("Back", (int)(28 * tex.screen_scale),
                                                ray::WHITE, ray::BLACK, false);
    rebuild_level_labels();
    refresh_count_label();

    audio.play_sound("voice_diff_sort_enter", VolumePreset::VOICE);
}

int TierSortSelect::level_index(int level) const {
    for (int i = 0; i < (int)levels.size(); i++)
        if (levels[i] == level) return i;
    return -1;
}

int TierSortSelect::count_for_level(int level) const {
    auto it = all_counts.find(level);
    if (it == all_counts.end()) return 0;
    int n = 0;
    for (const auto& [_, c] : it->second) n += c;
    return n;
}

int TierSortSelect::count_for_tier(const std::string& tier) const {
    auto lit = all_counts.find(selected_level);
    if (lit == all_counts.end()) return 0;
    auto it = lit->second.find(tier);
    return it != lit->second.end() ? it->second : 0;
}

void TierSortSelect::rebuild_level_labels() {
    item_labels.clear();
    item_labels_hi.clear();
    int font_size = (int)(28 * tex.screen_scale);
    for (int level : levels) {
        std::string label = "\xE2\x98\x85" + std::to_string(level);  // ★N
        item_labels.push_back(std::make_unique<OutlinedText>(label, font_size, ray::WHITE, ray::BLACK, false));
        item_labels_hi.push_back(std::make_unique<OutlinedText>(label, font_size, ray::YELLOW, ray::BLACK, false));
    }
    num_boxes = (int)levels.size();
}

void TierSortSelect::rebuild_tier_labels() {
    item_labels.clear();
    item_labels_hi.clear();
    int font_size = (int)(28 * tex.screen_scale);
    for (const auto& name : tier_names) {
        item_labels.push_back(std::make_unique<OutlinedText>(name, font_size, ray::WHITE, ray::BLACK, false));
        item_labels_hi.push_back(std::make_unique<OutlinedText>(name, font_size, ray::YELLOW, ray::BLACK, false));
    }
    num_boxes = (int)tier_names.size();
}

void TierSortSelect::refresh_count_label() {
    int n = 0;
    std::string text;
    if (selected_box < 0) {
        text = in_tier_select ? "Back to levels" : "Cancel";
    } else if (!in_tier_select) {
        n = count_for_level(levels[selected_box]);
        text = std::to_string(n) + " charts";
    } else {
        n = count_for_tier(tier_names[selected_box]);
        text = std::to_string(n) + " charts";
    }
    count_label = std::make_unique<OutlinedText>(text, (int)(22 * tex.screen_scale),
                                                 ray::WHITE, ray::BLACK, false);
}

void TierSortSelect::enter_tier_select(int level) {
    selected_level = level;
    in_tier_select = true;
    tier_names = tier_list.tiers_for_level(level);
    if (tier_names.empty()) {
        // Fail-soft: still show an empty stage rather than crash.
        tier_names = {};
    }
    selected_box = -1;
    if (level == prev_level) {
        for (int i = 0; i < (int)tier_names.size(); i++) {
            if (tier_names[i] == prev_tier) {
                selected_box = i;
                break;
            }
        }
    }
    title_label = std::make_unique<OutlinedText>(
        "Tier Sort — \xE2\x98\x85" + std::to_string(level),
        (int)(36 * tex.screen_scale), ray::WHITE, ray::BLACK, false);
    rebuild_tier_labels();
    refresh_count_label();
    fade_in->start();
    box_flicker->start();
}

void TierSortSelect::return_to_level_select() {
    in_tier_select = false;
    selected_box = level_index(selected_level);
    title_label = std::make_unique<OutlinedText>("Tier Sort — Level", (int)(36 * tex.screen_scale),
                                                 ray::WHITE, ray::BLACK, false);
    rebuild_level_labels();
    refresh_count_label();
    fade_in->start();
    box_flicker->start();
}

void TierSortSelect::update(double current_ms) {
    bg_resize->update(current_ms);
    fade_in->update(current_ms);
    box_flicker->update(current_ms);
}

std::optional<std::pair<int, std::string>> TierSortSelect::input_select() {
    if (!in_tier_select) {
        if (selected_box < 0) return std::pair<int, std::string>{-1, ""};
        enter_tier_select(levels[selected_box]);
        return std::nullopt;
    }
    if (selected_box < 0) {
        return_to_level_select();
        return std::nullopt;
    }
    return std::pair<int, std::string>{selected_level, tier_names[selected_box]};
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

    // Back + items in a single row (absolute screen coords).
    float box_w = 72.0f * tex.screen_scale;
    float box_h = 88.0f * tex.screen_scale;
    float gap = 6.0f * tex.screen_scale;
    // Tier letters like "SSS" / "S+" need a bit more width than level boxes.
    if (in_tier_select) box_w = 78.0f * tex.screen_scale;
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
        OutlinedText* label = (selected_box == i) ? item_labels_hi[i].get() : item_labels[i].get();
        draw_cell(selected_box == i, label);
    }

    if (count_label) {
        float cx = (tex.screen_width - count_label->width) * 0.5f;
        count_label->draw({.x=cx, .y=y + box_h + 24.0f * tex.screen_scale, .fade=alpha});
    }
}
