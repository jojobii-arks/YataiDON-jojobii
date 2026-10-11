#include "texture.h"
#include "profiler.h"
#include "global_data.h"
#include "script.h"
#include "filesystem.h"
#include <spdlog/spdlog.h>
#include <charconv>
#include <thread>
#include <atomic>

namespace {

inline double json_number(const Value& v, double fallback = 0.0) {
    if (v.IsInt())    return static_cast<double>(v.GetInt());
    if (v.IsInt64())  return static_cast<double>(v.GetInt64());
    if (v.IsUint())   return static_cast<double>(v.GetUint());
    if (v.IsUint64()) return static_cast<double>(v.GetUint64());
    if (v.IsNumber()) return v.GetDouble();
    return fallback;
}

inline double json_member(const Value& o, const char* key, double fallback) {
    return o.HasMember(key) ? json_number(o[key], fallback) : fallback;
}

}  // namespace

void TextureWrapper::init(const fs::path& skin_path) {
    graphics_path = skin_path;

    if (!fs::exists(graphics_path)) {
        throw std::runtime_error("The skin path provided is not a valid path");
    }

    // Every entry below is only ever inserted/overwritten from what the new
    // skin's (and its parent's) json actually specifies. A key the previous
    // skin set but this one doesn't mention would otherwise survive the
    // switch with the old skin's value.
    skin_config.clear();
    skin_config_by_name.clear();
    options.clear();
    chara_3d_config = Chara3DConfig{};

    auto skin_config_file = read_json_file(graphics_path / "skin_config.json");

    // Derive screen dimensions from child config first so screen_scale is known.
    {
        float w = skin_config_file.HasMember("screen") && skin_config_file["screen"].HasMember("width")
                      ? skin_config_file["screen"]["width"].GetFloat() : 1280.0f;
        float h = skin_config_file.HasMember("screen") && skin_config_file["screen"].HasMember("height")
                      ? skin_config_file["screen"]["height"].GetFloat() : 720.0f;
        screen_width  = static_cast<int>(w);
        screen_height = static_cast<int>(h);
        screen_scale  = w / 1280.0f;
    }

    auto load_entry = [this](const std::string& name, const Value& v, float scale) {
        if (!v.IsObject()) return;
        float x = static_cast<float>(json_member(v, "x", 0.0)) * scale;
        float y = static_cast<float>(json_member(v, "y", 0.0)) * scale;
        int font_size = static_cast<int>(json_member(v, "font_size", 0) * scale);
        float width = static_cast<float>(json_member(v, "width", 0.0)) * scale;
        float height = static_cast<float>(json_member(v, "height", 0.0)) * scale;

        std::map<std::string, std::string> text_map;
        if (v.HasMember("text") && v["text"].IsObject()) {
            for (auto& t : v["text"].GetObject()) {
                text_map[t.name.GetString()] = t.value.GetString();
            }
        }

        float outline = static_cast<float>(json_member(v, "outline", -1.0));

        SkinInfo info(x, y, font_size, width, height, text_map, outline);

        skin_config_by_name[name] = info;

        auto sc_it = skin_config_map.find(name);
        if (sc_it != skin_config_map.end()) {
            skin_config[sc_it->second] = std::move(info);
        }
    };

    // Ancestors (parent, its parent, ...), each scaled by this skin's width over its own
    // (screen.width, default 1280). Their skin_config loads farthest first, so nearer values
    // override, and the child's below overrides them all.
    ancestor_graphics_paths.clear();
    ancestor_scales.clear();
    {
        std::vector<fs::path> chain = resolve_skin_chain(graphics_path);
        for (size_t i = 1; i < chain.size(); i++) {
            float aw = 1280.0f;
            try {
                auto cfg = read_json_file(chain[i] / "skin_config.json");
                if (cfg.HasMember("screen") && cfg["screen"].HasMember("width") && cfg["screen"]["width"].IsNumber())
                    aw = cfg["screen"]["width"].GetFloat();
            } catch (const std::exception& e) {
                spdlog::warn("skin ancestor {}: {}", chain[i].string(), e.what());
            }
            ancestor_graphics_paths.push_back(chain[i]);
            ancestor_scales.push_back(static_cast<float>(screen_width) / aw);
        }
    }
    parent_graphics_path = ancestor_graphics_paths.empty() ? graphics_path : ancestor_graphics_paths.front();
    for (size_t i = ancestor_graphics_paths.size(); i-- > 0;) {
        auto ancestor_config = read_json_file(ancestor_graphics_paths[i] / "skin_config.json");
        for (auto& m : ancestor_config.GetObject()) {
            load_entry(m.name.GetString(), m.value, ancestor_scales[i]);
        }
    }

    // Load child skin_config ??overrides parent defaults.
    for (auto& m : skin_config_file.GetObject()) {
        load_entry(m.name.GetString(), m.value, 1.0f);
    }

    if (skin_config_file.HasMember("screen") && skin_config_file["screen"].HasMember("chara_3d")) {
        const Value& c3d = skin_config_file["screen"]["chara_3d"];
        if (c3d.IsObject()) {
            if (c3d.HasMember("scale"))  chara_3d_config.scale  = c3d["scale"].GetFloat();
            if (c3d.HasMember("rot_x"))  chara_3d_config.rot_x  = c3d["rot_x"].GetFloat();
            if (c3d.HasMember("rot_y"))  chara_3d_config.rot_y  = c3d["rot_y"].GetFloat();
            if (c3d.HasMember("rot_z"))  chara_3d_config.rot_z  = c3d["rot_z"].GetFloat();
        }
    }

    if (skin_config_file.HasMember("screen") && skin_config_file["screen"].HasMember("options")) {
        const Value& opts = skin_config_file["screen"]["options"];
        if (opts.IsObject()) {
            for (auto& opt : opts.GetObject()) {
                if (opt.value.IsBool()) {
                    auto it = screen_options_map.find(opt.name.GetString());
                    if (it != screen_options_map.end()) {
                        options[it->second] = opt.value.GetBool();
                    }
                }
            }
        }
    }
}

void TextureWrapper::unload_textures() {
    lua_texture_lookup.clear();
    textures.clear();
    loaded_subsets.clear();
    animations.clear();
    copied_animations.clear();
    screen_animations.clear();
    subset_loaded_ids.clear();
    tex_id_refcount.clear();
    debug_draw_log.clear();
    debug_draw_log_prev.clear();
}

BaseAnimation* TextureWrapper::get_animation(const int id, bool is_copy) {
    if (animations.find(id) == animations.end()) {
        throw std::runtime_error("Unable to find animation: " + std::to_string(id));
    }

    if (is_copy) {
        auto new_anim = animations[id]->copy();
        if (animations[id]->isStarted()) {
            new_anim->start();
        }
        // Note: Returning raw pointer from unique_ptr requires careful management
        copied_animations.push_back(std::move(new_anim));
        return copied_animations.back().get();
    }

    return animations[id].get();
}

BaseAnimation* TextureWrapper::get_animation(const int id, const std::string& screen_name) {
    if (screen_animations.find(screen_name) == screen_animations.end()) {
        Document anim_config;
        if (read_screen_animations(screen_name, anim_config)) {
            AnimationParser parser;
            screen_animations[screen_name] = parser.parse_animations(anim_config);
        } else {
            spdlog::warn("No animation.json found for screen: {}", screen_name);
        }
    }

    auto screen_it = screen_animations.find(screen_name);
    if (screen_it == screen_animations.end()) {
        throw std::runtime_error("No animations available for screen: " + screen_name);
    }
    auto& anim_map = screen_it->second;
    auto it = anim_map.find(id);
    if (it == anim_map.end()) {
        throw std::runtime_error("Unable to find animation " + std::to_string(id) + " in screen: " + screen_name);
    }

    auto new_anim = it->second->copy();
    if (it->second->isStarted()) new_anim->start();
    copied_animations.push_back(std::move(new_anim));
    return copied_animations.back().get();
}

void TextureWrapper::read_tex_obj_data(const Value& tex_mapping, TextureObject* tex_obj, float scale) {
    if (tex_mapping.IsArray()) {
        // Check if crop data exists in the first mapping (index 0)
        bool has_crop_in_first = tex_mapping.Size() > 0 &&
                                 tex_mapping[0].IsObject() &&
                                 tex_mapping[0].HasMember("crop") &&
                                 tex_mapping[0]["crop"].IsArray() &&
                                 !tex_mapping[0]["crop"].Empty();

        std::vector<ray::Rectangle> crops;
        if (has_crop_in_first) {
            const Value& first_mapping = tex_mapping[0];
            for (SizeType j = 0; j < first_mapping["crop"].Size(); j++) {
                const Value& crop = first_mapping["crop"][j];
                if (!crop.IsArray() || crop.Size() < 4) {
                    spdlog::error("Invalid crop entry {} for texture {}", j, tex_obj->name);
                    continue;
                }
                crops.push_back(ray::Rectangle{
                    crop[0].GetFloat(), crop[1].GetFloat(),
                    crop[2].GetFloat(), crop[3].GetFloat()
                });
            }
            if (crops.empty()) {
                has_crop_in_first = false;
            } else {
                tex_obj->crop_data = crops;
                tex_obj->width = static_cast<int>(crops[0].width);
                tex_obj->height = static_cast<int>(crops[0].height);
            }
        }

        tex_obj->x.resize(tex_mapping.Size());
        tex_obj->y.resize(tex_mapping.Size());
        tex_obj->x2.resize(tex_mapping.Size());
        tex_obj->y2.resize(tex_mapping.Size());

        for (SizeType i = 0; i < tex_mapping.Size(); i++) {
            const Value& mapping = tex_mapping[i];

            int x = static_cast<int>(json_member(mapping, "x", 0) * scale);
            int y = static_cast<int>(json_member(mapping, "y", 0) * scale);
            int x2 = static_cast<int>(json_member(mapping, "x2", tex_obj->width) * scale);
            int y2 = static_cast<int>(json_member(mapping, "y2", tex_obj->height) * scale);

            tex_obj->x[i] = x;
            tex_obj->y[i] = y;
            tex_obj->x2[i] = x2;
            tex_obj->y2[i] = y2;

            // Handle frame_order
            if (mapping.HasMember("frame_order") && mapping["frame_order"].IsArray()) {
                auto* framed = dynamic_cast<FramedTexture*>(tex_obj);
                if (framed) {
                    std::vector<std::shared_ptr<ray::Texture2D>> reordered;
                    for (SizeType j = 0; j < mapping["frame_order"].Size(); j++) {
                        int idx = static_cast<int>(json_number(mapping["frame_order"][j]));
                        if (idx < 0 || idx >= static_cast<int>(framed->textures.size())) {
                            spdlog::error("Invalid frame_order index {} for texture {}", idx, tex_obj->name);
                            continue;
                        }
                        reordered.push_back(framed->textures[idx]);
                    }
                    framed->textures = reordered;
                }
            }

            // Crop sets the source cell. Destination size defaults to that cell,
            // unless this index gives an explicit x2/y2 (a 1920 skin scaling a
            // 1280-authored digit atlas up to screen_scale).
            if (has_crop_in_first) {
                if (!mapping.HasMember("x2"))
                    tex_obj->x2[i] = static_cast<int>(crops[0].width * scale);
                if (!mapping.HasMember("y2"))
                    tex_obj->y2[i] = static_cast<int>(crops[0].height * scale);
            }
        }
    } else if (tex_mapping.IsObject()) {
        if (tex_mapping.HasMember("crop") && tex_mapping["crop"].IsArray() && !tex_mapping["crop"].Empty()) {
            std::vector<ray::Rectangle> crops;
            for (SizeType j = 0; j < tex_mapping["crop"].Size(); j++) {
                const Value& crop = tex_mapping["crop"][j];
                if (!crop.IsArray() || crop.Size() < 4) {
                    spdlog::error("Invalid crop entry {} for texture {}", j, tex_obj->name);
                    continue;
                }
                crops.push_back(ray::Rectangle{
                    crop[0].GetFloat(), crop[1].GetFloat(),
                    crop[2].GetFloat(), crop[3].GetFloat()
                });
            }
            if (!crops.empty()) {
                tex_obj->crop_data = crops;
                tex_obj->width = static_cast<int>(crops[0].width);
                tex_obj->height = static_cast<int>(crops[0].height);
            }
        }

        tex_obj->x = {static_cast<int>(json_member(tex_mapping, "x", 0) * scale)};
        tex_obj->y = {static_cast<int>(json_member(tex_mapping, "y", 0) * scale)};
        tex_obj->x2 = {static_cast<int>(json_member(tex_mapping, "x2", tex_obj->width) * scale)};
        tex_obj->y2 = {static_cast<int>(json_member(tex_mapping, "y2", tex_obj->height) * scale)};

        // Handle frame_order
        if (tex_mapping.HasMember("frame_order") && tex_mapping["frame_order"].IsArray()) {
            auto* framed = dynamic_cast<FramedTexture*>(tex_obj);
            if (framed) {
                std::vector<std::shared_ptr<ray::Texture2D>> reordered;
                for (SizeType j = 0; j < tex_mapping["frame_order"].Size(); j++) {
                    int idx = static_cast<int>(json_number(tex_mapping["frame_order"][j]));
                    if (idx < 0 || idx >= static_cast<int>(framed->textures.size())) {
                        spdlog::error("Invalid frame_order index {} for texture {}", idx, tex_obj->name);
                        continue;
                    }
                    reordered.push_back(framed->textures[idx]);
                }
                framed->textures = reordered;
            }
        }
    }

    ray::TextureFilter filter = ray::TEXTURE_FILTER_BILINEAR;
    for (int i = 0; i < tex_obj->frame_count(); i++) {
        if (const ray::Texture2D* t = tex_obj->frame_texture(i)) {
            SetTextureFilter(*const_cast<ray::Texture2D*>(t), filter);
        }
    }
}

bool TextureWrapper::read_screen_animations(const std::string& screen_name, Document& out) {
    fs::path own = graphics_path / screen_name / "animation.json";
    if (fs::exists(own)) {
        out = read_json_file(own);
        return true;
    }
    for (size_t i = 0; i < ancestor_graphics_paths.size(); i++) {
        fs::path file = ancestor_graphics_paths[i] / screen_name / "animation.json";
        if (!fs::exists(file)) continue;
        out = read_json_file(file);
        const float scale = ancestor_scales[i];
        auto scale_number = [scale](Value& v) {
            if (v.IsInt()) v.SetInt(static_cast<int>(json_number(v) * scale));
            else if (v.IsDouble()) v.SetDouble(v.GetDouble() * scale);
        };
        if (out.IsArray()) {
            for (SizeType k = 0; k < out.Size(); k++) {
                Value& anim = out[k];
                if (anim.HasMember("total_distance") && !anim["total_distance"].IsObject()) scale_number(anim["total_distance"]);
                if (anim.HasMember("start_position") && !anim["start_position"].IsObject()) scale_number(anim["start_position"]);
                if (anim.HasMember("waypoints") && anim["waypoints"].IsArray()) {
                    for (auto& wp : anim["waypoints"].GetArray()) {
                        if (wp.IsObject() && wp.HasMember("value")) scale_number(wp["value"]);
                    }
                }
            }
        }
        return true;
    }
    return false;
}

void TextureWrapper::load_animations(const std::string& screen_name) {
    PROFILE_SCOPE();
    PROFILE_ZONE_TEXT(screen_name);
    Document anim_config;
    if (read_screen_animations(screen_name, anim_config)) {
        AnimationParser parser;
        animations = parser.parse_animations(anim_config);
        spdlog::info("Animations loaded for screen: {}", screen_name);
    }
}

std::unordered_map<std::string, std::weak_ptr<TextureObject>>& tex_object_cache() {
    static std::unordered_map<std::string, std::weak_ptr<TextureObject>> cache;
    return cache;
}

std::shared_ptr<ray::Texture2D> shared_texture(ray::Texture2D tex) {
    return std::shared_ptr<ray::Texture2D>(
        new ray::Texture2D(tex),
        [](ray::Texture2D* p) {
            if (p->id != 0) ray::UnloadTexture(*p);
            delete p;
        });
}

void decode_images_parallel(const std::vector<fs::path>& files, std::vector<ray::Image>& out) {
    out.assign(files.size(), ray::Image{});
    if (files.empty()) return;

    unsigned hw = std::thread::hardware_concurrency();
    size_t workers = std::min<size_t>(files.size(), hw ? hw : 4);
    if (workers <= 1) {
        for (size_t i = 0; i < files.size(); ++i)
            out[i] = ray::LoadImage(files[i].string().c_str());
        return;
    }

    std::atomic<size_t> next{0};
    std::vector<std::thread> pool;
    pool.reserve(workers);
    for (size_t w = 0; w < workers; ++w) {
        pool.emplace_back([&files, &out, &next]() {
            for (size_t i = next.fetch_add(1); i < files.size(); i = next.fetch_add(1)) {
                try {
                    out[i] = ray::LoadImage(files[i].string().c_str());
                } catch (const std::exception& e) {
                    spdlog::error("Failed to decode image {}: {}", files[i].string(), e.what());
                    out[i] = ray::Image{};
                }
            }
        });
    }
    for (auto& t : pool) t.join();
}

std::vector<fs::path> sorted_frames(const fs::path& dir) {
    std::vector<fs::path> frames;
    for (const auto& entry : fs::directory_iterator(dir))
        if (entry.is_regular_file()) frames.push_back(entry.path());
    std::sort(frames.begin(), frames.end(), [](const fs::path& a, const fs::path& b) {
        const std::string as = a.stem().string(), bs = b.stem().string();
        int an = 0, bn = 0;
        auto ar = std::from_chars(as.data(), as.data() + as.size(), an);
        auto br = std::from_chars(bs.data(), bs.data() + bs.size(), bn);
        bool a_ok = ar.ec == std::errc() && ar.ptr == as.data() + as.size();
        bool b_ok = br.ec == std::errc() && br.ptr == bs.data() + bs.size();
        if (a_ok && b_ok) return an < bn;
        if (a_ok != b_ok) return a_ok;
        return as < bs;
    });
    return frames;
}

std::unordered_set<std::string> overridden_names(const fs::path& child_folder) {
    std::unordered_set<std::string> names;
    try {
        auto cfg = read_json_file(child_folder / "texture.json");
        for (auto& m : cfg.GetObject()) {
            std::string n = m.name.GetString();
            if (fs::is_directory(child_folder / n) || fs::exists(child_folder / (n + ".png")))
                names.insert(n);
        }
    } catch (const std::exception&) {
        // Unreadable child config - fall back to loading everything from the parent.
        names.clear();
    }
    return names;
}

void TextureWrapper::load_folder(const std::string& screen_name, const std::string& subset) {
    lua_texture_lookup.clear();
    // Subset leaf name is the key prefix used in textures[] (e.g. "notes_nijiiro" from "game/notes_nijiiro")
    const std::string subset_key = fs::path(subset).filename().string();
    const std::string dedup_key = screen_name + "/" + subset_key;

    if (loaded_subsets.count(dedup_key)) return;

    int loaded_count = 0;
    std::unordered_set<std::string> ids_this_call;

    // A texture.json entry whose PNG(s) still have to be read off disk.
    struct PendingTex {
        std::string id;
        std::string name;
        const Value* mapping;
        std::string cache_key;
        size_t first_file;
        size_t file_count;
        bool framed;
    };

    auto load_from_path = [&](const fs::path& folder, float tex_scale,
                              const std::unordered_set<std::string>* skip) {
        fs::path tex_json = folder / "texture.json";
        if (!fs::exists(tex_json)) return;

        try {
            auto tex_config = read_json_file(tex_json);
            auto& cache = tex_object_cache();

            std::vector<PendingTex> pending;
            std::vector<fs::path> files;
            std::unordered_map<std::string, size_t> file_index;
            auto push_file = [&](const fs::path& path) -> size_t {
                std::string key = path.string();
                auto it = file_index.find(key);
                if (it != file_index.end()) return it->second;
                size_t idx = files.size();
                files.push_back(path);
                file_index.emplace(std::move(key), idx);
                return idx;
            };

            for (auto& m : tex_config.GetObject()) {
                std::string tex_name = m.name.GetString();

                if (skip && skip->count(tex_name)) continue;

                std::string tex_id = subset_key + "/" + tex_name;

                std::string cache_key = (folder / tex_name).string();
                auto cit = cache.find(cache_key);
                if (cit != cache.end()) {
                    if (auto shared = cit->second.lock()) {
                        textures[tex_id] = shared;
                        ids_this_call.insert(tex_id);
                        ++loaded_count;
                        continue;
                    }
                    cache.erase(cit);
                }

                fs::path tex_dir = folder / tex_name;
                fs::path tex_file = folder / (tex_name + ".png");
                // "file" lets several keys share one atlas PNG, distinguished by "crop".
                bool file_override = m.value.IsObject() && m.value.HasMember("file") && m.value["file"].IsString();
                if (file_override) tex_file = folder / m.value["file"].GetString();

                if (!file_override && fs::is_directory(tex_dir)) {
                    auto frames = sorted_frames(tex_dir);
                    size_t first = files.size();
                    for (const auto& frame : frames) push_file(frame);
                    pending.push_back({tex_id, tex_name, &m.value, cache_key,
                                       first, frames.size(), true});
                } else if (fs::exists(tex_file)) {
                    size_t first = push_file(tex_file);
                    pending.push_back({tex_id, tex_name, &m.value, cache_key,
                                       first, size_t{1}, false});
                } else {
                    auto existing = textures.find(tex_id);
                    if (existing != textures.end()) {
                        read_tex_obj_data(m.value, existing->second.get(), tex_scale);
                        ids_this_call.insert(tex_id);
                        ++loaded_count;
                    } else {
                        spdlog::error("Texture {} was not found in {}",
                               tex_name, folder.string());
                    }
                }
            }

            std::vector<ray::Image> images;
            decode_images_parallel(files, images);

            try {
                // GPU textures indexed by file, so keys that share one atlas PNG
                // (via "file" + "crop") upload it once and share the texture.
                std::vector<std::shared_ptr<ray::Texture2D>> gpu(files.size());
                for (const auto& p : pending) {
                    bool ok = true;
                    std::vector<std::shared_ptr<ray::Texture2D>> texs;
                    texs.reserve(p.file_count);
                    for (size_t i = 0; i < p.file_count; ++i) {
                        size_t fi = p.first_file + i;
                        std::shared_ptr<ray::Texture2D> tex = gpu[fi];
                        if (!tex) {
                            const ray::Image& img = images[fi];
                            if (!img.data) {
                                spdlog::error("Failed to decode image for texture {}: Frame {}", p.name, i);
                                ok = false;
                            } else {
                                ray::Texture2D t = ray::LoadTextureFromImage(img);
                                if (!ray::IsTextureValid(t)) {
                                    spdlog::error("Failed to load texture {}: Frame {}", p.name, i);
                                    ok = false;
                                } else {
                                    tex = shared_texture(t);
                                    gpu[fi] = tex;
                                }
                            }
                        }
                        texs.push_back(std::move(tex));
                    }
                    if (!ok) continue;

                    std::shared_ptr<TextureObject> obj;
                    if (p.framed) obj = std::make_shared<FramedTexture>(p.id, std::move(texs));
                    else          obj = std::make_shared<SingleTexture>(p.id, texs[0]);

                    read_tex_obj_data(*p.mapping, obj.get(), tex_scale);

                    if (!p.framed && !obj->crop_data.has_value() &&
                        p.mapping->IsObject() && !p.mapping->HasMember("x2")) {
                        const ray::Image& img = images[p.first_file];
                        if (img.data) {
                            const ray::Rectangle border = ray::GetImageAlphaBorder(img, 0.0f);
                            if (border.width > 0.0f) {
                                obj->opaque_right = (border.x + border.width) * tex_scale;
                            }
                        }
                    }

                    textures[p.id] = obj;
                    cache[p.cache_key] = obj;
                    ids_this_call.insert(p.id);
                    ++loaded_count;
                }
            } catch (...) {
                for (auto& img : images)
                    if (img.data) ray::UnloadImage(img);
                throw;
            }

            for (auto& img : images)
                if (img.data) ray::UnloadImage(img);

            spdlog::debug("Textures loaded from folder: {}", folder.string());

        } catch (const std::exception& e) {
            spdlog::error("Failed to load textures from folder {}: {}",
                   folder.string(), e.what());
        }
    };

    // Farthest ancestor first, child last (later loads overwrite), each ancestor skipping
    // the names already provided by the child or a closer ancestor.
    const fs::path own_dir = graphics_path / screen_name / subset;
    const bool child_has_folder = ancestor_graphics_paths.empty() || fs::exists(own_dir / "texture.json");
    std::vector<std::unordered_set<std::string>> skips(ancestor_graphics_paths.size());
    std::unordered_set<std::string> claimed;
    if (child_has_folder && !ancestor_graphics_paths.empty()) claimed = overridden_names(own_dir);
    for (size_t i = 0; i < ancestor_graphics_paths.size(); i++) {
        const fs::path dir = ancestor_graphics_paths[i] / screen_name / subset;
        skips[i] = claimed;
        if (fs::exists(dir / "texture.json"))
            for (const std::string& n : overridden_names(dir)) claimed.insert(n);
    }
    for (size_t i = ancestor_graphics_paths.size(); i-- > 0;) {
        const fs::path dir = ancestor_graphics_paths[i] / screen_name / subset;
        if (fs::exists(dir / "texture.json")) load_from_path(dir, ancestor_scales[i], &skips[i]);
    }
    if (child_has_folder) {
        load_from_path(own_dir, 1.0f, nullptr);
    }

    if (loaded_count == 0) {
        spdlog::error("No textures loaded for {}/{}", screen_name, subset);
    } else {
        loaded_subsets.insert(dedup_key);
        subset_loaded_ids[dedup_key] = ids_this_call;
        for (const std::string& id : ids_this_call) ++tex_id_refcount[id];
    }
}

void TextureWrapper::unload_folder(const std::string& screen_name, const std::string& subset) {
    lua_texture_lookup.clear();
    const std::string subset_key = fs::path(subset).filename().string();
    const std::string dedup_key = screen_name + "/" + subset_key;

    if (!loaded_subsets.count(dedup_key)) return;

    // A subset name can be shared by several screens (they resolve to the same
    // "subset/name" keys), so only drop a texture once every
    // screen/subset that loaded it has also unloaded it.
    auto& refcount = tex_id_refcount;
    auto ids_it = subset_loaded_ids.find(dedup_key);
    if (ids_it != subset_loaded_ids.end()) {
        for (const std::string& id : ids_it->second) {
            auto rc_it = refcount.find(id);
            if (rc_it == refcount.end()) continue;
            if (--rc_it->second <= 0) {
                textures.erase(id);
                refcount.erase(rc_it);
            }
        }
        subset_loaded_ids.erase(ids_it);
    }

    loaded_subsets.erase(dedup_key);
    spdlog::info("Textures unloaded for folder: {}/{}", screen_name, subset);
}

void TextureWrapper::load_screen_textures(const std::string& screen_name) {
    PROFILE_SCOPE();
    PROFILE_ZONE_TEXT(screen_name);
    std::vector<fs::path> roots{graphics_path};
    roots.insert(roots.end(), ancestor_graphics_paths.begin(), ancestor_graphics_paths.end());
    std::vector<fs::path> screen_dirs;
    for (const fs::path& root : roots)
        if (fs::exists(root / screen_name)) screen_dirs.push_back(root / screen_name);

    if (screen_dirs.empty()) {
        spdlog::warn("Textures for Screen {} do not exist", screen_name);
        return;
    }

    load_animations(screen_name);

    // Subsets of the child and every ancestor (load_folder dedups by screen/subset)
    for (const fs::path& dir : screen_dirs) {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_directory()) {
                load_folder(screen_name, entry.path().filename().string());
            }
        }
    }

    spdlog::info("Screen textures loaded for: {}", screen_name);
}

void TextureWrapper::clear_screen(const ray::Color& color) {
    ray::ClearBackground(color);
}
// Language-suffixed textures (`combo/combo_<lang>`): a skin rarely ships every language, so
// a name ending in the current language falls back to the `_ja` and then the `_en` variant
// instead of the warning placeholder. Names without the suffix are returned unchanged.
std::vector<std::string> TextureWrapper::language_variants(const std::string& name) const {
    std::vector<std::string> out{name};
    if (!global_data.config) return out;
    const std::string& lang = global_data.config->general.language;
    const std::string suffix = "_" + lang;
    if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) return out;
    const std::string base = name.substr(0, name.size() - suffix.size());
    for (const char* fb : {"ja", "en"})   // the cabinet's own order: untranslated rows draw from the Japanese set
        if (lang != fb) out.push_back(base + "_" + fb);
    return out;
}

TextureObject* TextureWrapper::get_texture(const std::string& name) {
    return get_texture_shared(name).get();
}

std::shared_ptr<TextureObject> TextureWrapper::get_texture_shared(const std::string& name) {
    // first variant that is actually loaded, else fall back to the warning placeholder
    for (const auto& v : language_variants(name)) {
        auto it = textures.find(v);
        if (it != textures.end()) return it->second;
    }
    spdlog::warn("Texture not found: {}", name);
    auto it = textures.find("kidou/warning");
    return it != textures.end() ? it->second : nullptr;
}

bool TextureWrapper::has_texture(const std::string& name) {
    for (const auto& v : language_variants(name)) {
        if (textures.find(v) != textures.end()) return true;
    }
    return false;
}

void TextureWrapper::draw_texture(TextureObject* tex_obj, const DrawTextureParams& params) {
    if (!tex_obj) return;

    const float mirror_x = (params.mirror == Mirror::HORIZONTAL) ? -1.0f : 1.0f;
    const float mirror_y = (params.mirror == Mirror::VERTICAL) ? -1.0f : 1.0f;

    const ray::Color final_color = (params.fade != 1.1f) ? Fade(params.color, params.fade) : params.color;

    ray::Rectangle source_rect;
    if (params.src.has_value()) {
        source_rect = params.src.value();
    } else if (tex_obj->crop_data.has_value()) {
        try {
            source_rect = tex_obj->crop_data->at(params.frame);
            source_rect.width  -= 1.0f;
            source_rect.height -= 1.0f;
            source_rect.x += mirror_x < 0 ? -0.5f : 0.5f;
            source_rect.y += mirror_y < 0 ? -0.5f : 0.5f;
            source_rect.width  *= mirror_x;
            source_rect.height *= mirror_y;
        } catch (const std::out_of_range& e) {
            spdlog::error("Frame index out of range for texture {}", tex_obj->name);
            spdlog::error("Frame index: {}, Number of frames: {}", params.frame, tex_obj->crop_data->size());
            throw;
        }
    } else {
        const float width = static_cast<float>(tex_obj->width);
        const float height = static_cast<float>(tex_obj->height);
        source_rect = ray::Rectangle{0, 0, width * mirror_x, height * mirror_y};
    }

    if (params.index < 0 || static_cast<size_t>(params.index) >= tex_obj->x.size()) {
        spdlog::error("Draw index {} out of range for texture {}", params.index, tex_obj->name);
        return;
    }

    // Calculate destination rectangle with reduced redundant calculations
    const float base_x = tex_obj->x[params.index];
    const float base_y = tex_obj->y[params.index];

    ray::Rectangle dest_rect;
    if (params.center) {
        const float x2v = tex_obj->x2[params.index];
        const float y2v = tex_obj->y2[params.index];
        const float half_width = x2v * 0.5f;
        const float half_height = y2v * 0.5f;
        const float scaled_half_width = (x2v * params.scale) * 0.5f;
        const float scaled_half_height = (y2v * params.scale) * 0.5f;

        dest_rect = ray::Rectangle{
            base_x + draw_offset_x + half_width - scaled_half_width + params.x,
            base_y + draw_offset_y + half_height - scaled_half_height + params.y,
            x2v * params.scale + params.x2,
            y2v * params.scale + params.y2
        };
    } else {
        dest_rect = ray::Rectangle{
            base_x + draw_offset_x + params.x,
            base_y + draw_offset_y + params.y,
            tex_obj->x2[params.index] * params.scale + params.x2,
            tex_obj->y2[params.index] * params.scale + params.y2
        };
    }

    if (debug_log_draws) {
        debug_draw_log.push_back({.name = tex_obj->name, .rect = dest_rect, .tex_obj = tex_obj,
                                  .index = params.index, .offset_x = params.x, .offset_y = params.y,
                                  .scale = params.scale, .center = params.center,
                                  .origin = params.origin, .rotation = params.rotation});
        log_lua_site(debug_draw_log.back(), script_lua_state());
    }

    const ray::Texture2D* frame_tex = tex_obj->frame_texture(params.frame);
    if (frame_tex) {
        if (params.blend.has_value()) {
            ray::BeginBlendMode(params.blend.value());
            DrawTexturePro(*frame_tex, source_rect, dest_rect,
                          params.origin, params.rotation, final_color);
            ray::EndBlendMode();
            return;
        }
        DrawTexturePro(*frame_tex, source_rect, dest_rect,
                      params.origin, params.rotation, final_color);
    }
}

TextureWrapper tex;

TextureWrapper global_tex;
