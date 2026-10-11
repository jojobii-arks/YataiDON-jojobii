#include "loading.h"
#include <unordered_set>
#include "../libs/global_data.h"
#include "../libs/scores.h"
#include "../libs/filesystem.h"
#include "../libs/input.h"
#include "../libs/skin_updater.h"
#include "../libs/song_parser.h"
#include "../objects/song_select/file_navigator/navigator.h"

void LoadingScreen::on_screen_start() {
    init_visuals();

    start_ms = get_current_ms();
#ifdef __EMSCRIPTEN__
    load_song_hashes();
#else
    loading_thread = std::thread(&LoadingScreen::load_song_hashes, this);
#endif
}

// Loads this screen's textures/sounds and (re)grabs the texture pointers and
// geometry from the current skin. Split out of on_screen_start() so it can be
// run again after a skin update forces a skin reload.
void LoadingScreen::init_visuals() {
    tex.load_screen_textures("loading");
    audio.load_screen_sounds("loading");

    progress_bar_width = tex.screen_width * 0.43;
    progress_bar_height = 50 * tex.screen_scale;
    progress_bar_x = (tex.screen_width - progress_bar_width) / 2;
    progress_bar_y = tex.screen_height * 0.85;

    if (const SkinInfo* bar = tex.skin_entry("loading_progress_bar")) {
        if (bar->width > 0 && bar->height > 0) {
            progress_bar_x = bar->x;
            progress_bar_y = bar->y;
            progress_bar_width = bar->width;
            progress_bar_height = bar->height;
        }
    }

    double fade_ms = 1000;
    if (const SkinInfo* f = tex.skin_entry("loading_fade_ms")) {
        if (f->x > 0) fade_ms = f->x;
    }
    fade_in = std::make_unique<FadeAnimation>(fade_ms, 0.0, false, false, 1.0);
    allnet_indicator.emplace();
    t_warning = tex.get_texture("kidou/warning");

    countdown_ms = tex.skin_config[SC::LOADING_COUNTDOWN].height * 1000.0;
    t_countdown = (countdown_ms > 0.0) ? tex.get_texture("kidou/countdown") : nullptr;
}

void LoadingScreen::load_song_hashes() {
    try {
        // Listed here, not in on_screen_start: the screen keeps drawing while a large library is walked
        songs = get_song_files(global_data.config->paths.tja_path);
        std::atomic<int> songs_loaded = 0;
        const int thread_count = std::max(1u, std::thread::hardware_concurrency());
        std::vector<std::thread> threads;
        std::mutex scores_mutex;

        // Files whose size and modification time match the cache are not parsed again; the
        // workers only read `cache`, new or changed entries are written after they finish.
        const std::unordered_map<std::string, SongCacheEntry> cache = scores_manager.load_song_cache();
        std::vector<std::pair<std::string, SongCacheEntry>> fresh;
        std::atomic<int> cache_hits = 0;

        auto worker = [&](int start, int end) {
            for (int i = start; i < end; i++) {
                auto u8 = songs[i].u8string();
                const std::string path(u8.begin(), u8.end());

                std::error_code ec;
                const auto mtime = std::filesystem::last_write_time(songs[i], ec);
                if (ec) {
                    spdlog::error("Could not stat {}: {}", path, ec.message());
                    continue;
                }
                const auto size = std::filesystem::file_size(songs[i], ec);
                SongCacheEntry stamp;
                stamp.mtime = static_cast<int64_t>(mtime.time_since_epoch().count());
                stamp.size  = ec ? -1 : static_cast<int64_t>(size);

                std::array<std::string, 5> hashes;
                std::string title, subtitle;
                SongCacheEntry meta;   // handed to Navigator::preload

                auto cached = cache.find(path);
                const bool from_cache = cached != cache.end() && cached->second.mtime == stamp.mtime &&
                                        cached->second.size == stamp.size;
                if (from_cache) {
                    hashes   = cached->second.hashes;
                    title    = cached->second.title;
                    subtitle = cached->second.subtitle;
                    meta     = cached->second;
                    cache_hits++;
                } else try {
                    SongParser parser(songs[i]);
                    spdlog::debug("Parsing song: {}", path);
                    if (songs[i].extension() == ".osu") {
                        hashes[0] = parser.get_diff_hash(0);
                    } else {
                        for (const auto& [course, course_data] : parser.metadata.course_data) {
                            if (course < 0 || course >= static_cast<int>(hashes.size()))
                                continue;
                            hashes[course] = parser.get_diff_hash(course);
                        }
                    }
                    title    = parser.metadata.title.count("en") ? parser.metadata.title.at("en") : "";
                    subtitle = parser.metadata.subtitle.count("en") ? parser.metadata.subtitle.at("en") : "";
                    stamp.hashes   = hashes;
                    stamp.title    = title;
                    stamp.subtitle = subtitle;
                    for (const auto& [lang, t] : parser.metadata.title)    stamp.titles.emplace_back(lang, t);
                    for (const auto& [lang, t] : parser.metadata.subtitle) stamp.subtitles.emplace_back(lang, t);
                    for (const auto& [course, data] : parser.metadata.course_data)
                        if (course >= 0 && course <= 4) stamp.levels[course] = (int)data.level;
                    meta = stamp;
                    std::lock_guard<std::mutex> lock(scores_mutex);
                    fresh.emplace_back(path, std::move(stamp));
                } catch (const std::exception& e) {
                    spdlog::error("Failed to parse song {}: {}", path, e.what());
                    continue;
                }

                try {
                    std::lock_guard<std::mutex> lock(scores_mutex);
                    // a cached file's songs row was written when it was parsed
                    if (!from_cache) scores_manager.add_song(hashes, title, subtitle);
                    scores_manager.add_path_binding(songs[i], hashes);
                    scores_manager.set_song_meta(path, meta);

                    progress = (float)++songs_loaded / songs.size();
                } catch (const std::exception& e) {
                    spdlog::error("Failed to record song {}: {}", path, e.what());
                }
            }
        };

        scores_manager.begin_transaction();
#ifdef __EMSCRIPTEN__
        worker(0, songs.size());
#else
        int chunk = songs.size() / thread_count;
        for (int i = 0; i < thread_count; i++) {
            int start = i * chunk;
            int end = (i == thread_count - 1) ? songs.size() : start + chunk;
            threads.emplace_back(worker, start, end);
        }
        for (auto& t : threads) t.join();
#endif
        for (const auto& [path, entry] : fresh) scores_manager.store_song_cache(path, entry);
        // forget files that are gone, so the table does not grow with deleted songs
        if (!cache.empty()) {
            std::unordered_set<std::string> present;
            present.reserve(songs.size());
            for (const auto& song : songs) {
                auto u8 = song.u8string();
                present.emplace(u8.begin(), u8.end());
            }
            for (const auto& [path, entry] : cache) {
                if (!present.count(path)) scores_manager.remove_song_cache(path);
            }
        }
        scores_manager.commit();
        spdlog::info("Song scan: {} files, {} from cache, {} parsed", songs.size(), cache_hits.load(), fresh.size());

        if (fs::exists(fs::path("scores_pytaiko.db"))) {
            scores_manager.py_taiko_import(fs::path("scores_pytaiko.db"));
            fs::remove(fs::path("scores_pytaiko.db"));
        }

        load_navigator();
    } catch (const std::exception& e) {
        spdlog::error("Loading failed: {}", e.what());
    }
    loading_complete = true;
}

void LoadingScreen::load_navigator() {
    navigator.preload(global_data.config->paths.tja_path, songs);
}

Screens LoadingScreen::on_screen_end(Screens next_screen) {
    if (loading_thread.joinable()) {
        loading_thread.join();
    }
    return Screen::on_screen_end(next_screen);
}

std::optional<Screens> LoadingScreen::update() {
    Screen::update();
    if (allnet_indicator) allnet_indicator->update(get_current_ms());

    if (is_l_don_pressed() || is_r_don_pressed()) skip_requested = true;

    if (!skin_reloaded && loading_complete &&
        skin_updater.finished() && skin_updater.updated_this_pass()) {
        skin_reloaded = true;
        spdlog::info("Skin update applied; reloading skin before leaving the loading screen");
        try {
            allnet_indicator.reset();
            t_warning = nullptr;
            t_countdown = nullptr;
            drop_other_screens_for_skin_reload();
            navigator.reset_for_skin_reload();
            unload_skin();
            load_skin();
            reload_skin_screens();
        } catch (const std::exception& e) {
            spdlog::error("Skin reload after update failed: {}", e.what());
        }
        init_visuals();
    }

    const bool skin_updater_blocking = skin_updater.applying_updates();

    if (loading_complete && !fade_in->isStarted() && !skin_updater_blocking &&
        (skip_requested || get_current_ms() - start_ms >= countdown_ms)) {
        fade_in->start();
    }

    fade_in->update(get_current_ms());
    if (fade_in->is_finished) {
#ifdef __EMSCRIPTEN__
        return on_screen_end(Screens::ENTRY);
#else
        return on_screen_end(Screens::TITLE);
#endif
    }

    return std::nullopt;
}

void LoadingScreen::draw() {
    ray::DrawRectangle(0, 0, tex.screen_width, tex.screen_height, ray::BLACK);

    ray::DrawRectangle(progress_bar_x, progress_bar_y, progress_bar_width, progress_bar_height, ray::Color(101, 0, 0, 255));

    float clamped_progress = std::max(0.0f, std::min(1.0f, progress.load()));
    float fill_width = progress_bar_width * clamped_progress;
    if (fill_width > 0) {
        ray::DrawRectangle(progress_bar_x, progress_bar_y, fill_width, progress_bar_height, ray::RED);
    }
    tex.draw_texture(t_warning);

    if (t_countdown) {
        const SkinInfo& cd = tex.skin_config[SC::LOADING_COUNTDOWN];
        const double left = std::max(0.0, countdown_ms - (get_current_ms() - start_ms));
        const int secs = static_cast<int>(left / 1000.0);
        const int hundredths = static_cast<int>(left / 10.0) % 100;
        std::vector<int> frames;
        for (char c : std::to_string(secs)) frames.push_back(c - '0');
        frames.insert(frames.end(), {10, hundredths / 10, hundredths % 10});
        const float x0 = cd.x - cd.width * frames.size() / 2.0f;
        for (size_t i = 0; i < frames.size(); i++) {
            tex.draw_texture(t_countdown, {.frame = frames[i], .x = x0 + cd.width * i, .y = cd.y});
        }
    }

    ray::DrawRectangle(0, 0, tex.screen_width, tex.screen_height, ray::Fade(ray::WHITE, fade_in->attribute));
    if (allnet_indicator) allnet_indicator->draw();
}
