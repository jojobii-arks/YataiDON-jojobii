#pragma once

#include "box_song.h"
#include "../../../libs/filesystem.h"

class SongBoxOsu : public SongBox {
public:
    SongBoxOsu(const fs::path& path, const BoxDef& box_def, SongParser parser);
};
