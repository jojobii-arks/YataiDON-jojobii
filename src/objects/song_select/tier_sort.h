#pragma once

#include "../../libs/texture.h"
#include "../../libs/text.h"
#include "../../libs/tier_list.h"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Two-stage picker modelled on DiffSortSelect: pick a star level (★6..★10),
// then a tier letter within that level. Opened from the Tier Sort collection.
class TierSortSelect {
private:
    bool in_tier_select = false;  // false = level stage, true = tier stage
    int selected_box = -1;        // -1 = back, 0..n-1 = item in current stage
    int selected_level = 0;       // star level once chosen (6..10)
    int num_boxes = 0;

    std::vector<int> levels;                 // e.g. {6,7,8,9,10}
    std::vector<std::string> tier_names;     // letters for selected_level
    // level -> (tier letter -> local chart count)
    std::unordered_map<int, std::unordered_map<std::string, int>> all_counts;
    int prev_level = 0;
    std::string prev_tier;

    TextureResizeAnimation* bg_resize = nullptr;
    FadeAnimation* fade_in = nullptr;
    FadeAnimation* box_flicker = nullptr;

    TextureObject* t_background = nullptr;

    std::vector<std::unique_ptr<OutlinedText>> item_labels;
    std::vector<std::unique_ptr<OutlinedText>> item_labels_hi;
    std::unique_ptr<OutlinedText> back_label;
    std::unique_ptr<OutlinedText> title_label;
    std::unique_ptr<OutlinedText> count_label;

    void rebuild_level_labels();
    void rebuild_tier_labels();
    void refresh_count_label();
    void enter_tier_select(int level);
    void return_to_level_select();
    int level_index(int level) const;
    int count_for_level(int level) const;
    int count_for_tier(const std::string& tier) const;

public:
    TierSortSelect(std::unordered_map<int, std::unordered_map<std::string, int>> counts,
                   int prev_level = 0, const std::string& prev_tier = "");

    void update(double current_ms);
    // nullopt = still navigating stages; {-1,""} = cancel; {level,tier} = confirm
    std::optional<std::pair<int, std::string>> input_select();
    void input_left();
    void input_right();
    void draw();
};
