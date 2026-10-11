#include "box_back.h"

BackBox::BackBox(const fs::path& path, const BoxDef& box_def) : BaseBox(path, box_def) {
    this->text_name = "BACK_BOX";
}
