#pragma once

#include "../../libs/texture.h"
#include "../../libs/text.h"
#include "../../libs/tier_list.h"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Tier-letter picker modelled on DiffSortSelect: open from the Tier Sort
// collection folder, pick a letter, filter the song wheel to that tier.
class TierSortSelect {
private:
    int selected_box = -1;  // -1 = back, 0..n-1 = tier
    int num_boxes = 0;
    std::vector<std::string> tier_names;
    std::unordered_map<std::string, int> local_counts;
    std::string prev_tier;

    TextureResizeAnimation* bg_resize = nullptr;
    FadeAnimation* fade_in = nullptr;
    FadeAnimation* box_flicker = nullptr;

    TextureObject* t_background = nullptr;

    std::vector<std::unique_ptr<OutlinedText>> tier_labels;
    std::vector<std::unique_ptr<OutlinedText>> tier_labels_hi;
    std::unique_ptr<OutlinedText> back_label;
    std::unique_ptr<OutlinedText> title_label;
    std::unique_ptr<OutlinedText> count_label;

    void refresh_count_label();

public:
    TierSortSelect(std::unordered_map<std::string, int> counts,
                   const std::string& prev_tier = "");

    void update(double current_ms);
    // nullopt = no confirm yet; -1 = cancel/back; >=0 = tier index
    std::optional<int> input_select();
    void input_left();
    void input_right();
    void draw();

    const std::string& tier_at(int index) const;
    int tier_count() const { return (int)tier_names.size(); }
};
