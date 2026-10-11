#pragma once

#include "texture.h"
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class FontManager {
private:
    fs::path font_path;

    struct SizedFont {
        ray::Font font{};                    // texture + recs + glyph structs; glyph images are fresh crops, not aliases of `cache`
        std::unordered_set<int> codepoints;  // only what was asked for AT THIS SIZE
        std::vector<ray::GlyphInfo> cache;   // rasterized once per codepoint (images owned here)
        bool atlas_dirty = false;
        uint64_t last_used = 0;
        bool loaded = false;
    };

    std::vector<unsigned char> font_data;    // the .ttf bytes, read once

    bool rasterize_new(SizedFont& entry, int font_size, const std::vector<int>& cps);   // font_mutex held
    void rebuild_atlas(SizedFont& entry, int font_size);                                // font_mutex held
    void release_font(SizedFont& entry);                                                // font_mutex held
    void release_cache(SizedFont& entry);                                               // font_mutex held
    bool register_codepoints(SizedFont& entry, int font_size, const std::string& text); // font_mutex held

    std::unordered_map<int, SizedFont> fonts;
    uint64_t use_clock = 0;

    ray::Texture sentinel_texture{};

    mutable std::mutex font_mutex;

    static constexpr size_t MAX_SIZED_FONTS = 32;

    SizedFont& acquire(const std::string& text, int font_size);  // font_mutex held
    void evict_lru(int keep_size);                               // font_mutex held

public:
    FontManager();
    void init(const fs::path& font_path);
    void unload();
    ray::Font get_font(const std::string& text, int font_size);
    ray::Font copy_font(const std::string& text, int font_size);
    void register_text(const std::string& text, int font_size);
};

enum class LocalizedTextFallback {
    NONE,
    ENGLISH,
    ENGLISH_OR_FIRST,
    REQUIRED_ENGLISH,
};

inline std::string localized_text(const std::map<std::string, std::string>& values,
                                  const std::string& language, LocalizedTextFallback fallback) {
    if (auto it = values.find(language); it != values.end()) {
        return it->second;
    }

    if (fallback == LocalizedTextFallback::NONE) return "";
    if (fallback == LocalizedTextFallback::REQUIRED_ENGLISH) return values.at("en");

    if (auto it = values.find("en"); it != values.end()) {
        return it->second;
    }

    if (fallback == LocalizedTextFallback::ENGLISH_OR_FIRST && !values.empty()) {
        return values.begin()->second;
    }

    return "";
}

class OutlinedText {
private:
    std::string text;
    float font_size;
    float outline_thickness;
    float v_advance = 1.0f;

    ray::Font worker_font;

    std::optional<ray::Image> pending_image;
    mutable std::mutex pending_mutex;

    std::optional<ray::Texture> texture;

    std::future<void> build_future;

    struct BuildData { ray::Image img; };

    BuildData build_horizontal_text(ray::Color color, ray::Color outline_color, float spacing);
    BuildData build_vertical_text  (ray::Color color, ray::Color outline_color, float spacing);

public:
    float width  = 0.0f;
    float height = 0.0f;
    float x_offset = 0.0f;
    float y_offset = 0.0f;

    OutlinedText(std::string text, int font_size,
                 ray::Color color, ray::Color outline_color,
                 bool is_vertical,
                 float outline_thickness = 5.0f,
                 float spacing = 2.0f,
                 float v_advance = 1.0f);

    ~OutlinedText();

    bool upload_pending();

    bool is_ready() const { return texture.has_value(); }

    const std::string& get_text() const { return text; }

    void finish();

    void draw(const DrawTextureParams& = {});
};

extern FontManager font_manager;
