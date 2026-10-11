#pragma once

#include "box_base.h"
#include "../../../libs/global_data.h"

class DanBox : public BaseBox {
public:
    std::string dan_title;
    int dan_color = 0;
    int dan_rank = -1;   // -1 = unearned/not yet ranked
    int dan_index = -1;  // -1 = unselected
    bool gaiden = false;
    std::vector<DanSongEntry> songs;
    std::vector<Exam> exams;
    int total_notes = 0;
    std::vector<std::pair<std::string, std::string>> song_titles;

    std::unique_ptr<OutlinedText> hori_name;
    std::vector<std::pair<std::unique_ptr<OutlinedText>, std::unique_ptr<OutlinedText>>> song_texts;

    DanBox(const fs::path& path, const std::string& title, int color,
           const std::vector<DanSongEntry>& songs, const std::vector<Exam>& exams,
           int total_notes);

    void load_text() override;
    void update(double current_ms) override;
};
