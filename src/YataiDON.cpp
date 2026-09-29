#include <cstdlib>
#include <iostream>
#include <rlgl.h>

#include "libs/animation.h"
#include "libs/audio.h"
#include "libs/global_data.h"
#include "libs/filesystem.h"
#include "libs/input.h"
#include "libs/logging.h"
#include "libs/camera_utils.h"
#include "libs/network.h"
#include "libs/screen.h"
#include "libs/script.h"
#include "libs/song_parser.h"
#include "libs/tier_list.h"

#ifdef _WIN32
#include "platform/platform_windows.h"
#elif defined(__ANDROID__)
#include "platform/platform_android.h"
#elif defined(__EMSCRIPTEN__)
#include "platform/platform_emscripten.h"
#endif

#include "scenes/dan_result.h"
#include "scenes/dan_select.h"
#include "scenes/entry.h"
#include "scenes/game.h"
#include "scenes/game_2p.h"
#include "scenes/game_dan.h"
#include "scenes/game_practice.h"
#include "scenes/input_cali.h"
#include "scenes/input_test.h"
#include "scenes/loading.h"
#include "scenes/result.h"
#include "scenes/result_2p.h"
#include "scenes/settings.h"
#include "scenes/song_select.h"
#include "scenes/song_select_2p.h"
#include "scenes/song_select_practice.h"
#include "scenes/title.h"
#include "scenes/game_over.h"

#include "objects/global/debug_menu.h"
#include "objects/global/fps_counter.h"

void draw_outer_border(int screen_width, int screen_height, ray::Color last_color) {
    DrawRectangle(-screen_width, 0, screen_width, screen_height, last_color);
    DrawRectangle(screen_width, 0, screen_width, screen_height, last_color);
    DrawRectangle(0, -screen_height, screen_width, screen_height, last_color);
    DrawRectangle(0, screen_height, screen_width, screen_height, last_color);
}

[[noreturn]] static void exit_now(int code) {
    std::cout.flush();
    std::cerr.flush();
    if (auto logger = spdlog::default_logger()) logger->flush();
    std::_Exit(code);
}

static std::filesystem::path path_from_arg(const std::string& arg) {
#ifdef _WIN32
    return win32_path_from_utf8(arg);
#else
    try {
        return std::filesystem::path(arg);
    } catch (const std::exception&) {
        return {};
    }
#endif
}

Screens check_args(int argc, char* argv[]) {
    if (argc == 1) {
        return Screens::LOADING;
    }

    std::string song_path;
    std::optional<int> difficulty;
    bool auto_play = false;
    bool practice = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--auto") {
            auto_play = true;
        } else if (arg == "--practice") {
            practice = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " <song_path> [difficulty] [--auto] [--practice]\n";
            std::cout << "  song_path   : Path to the TJA song file\n";
            std::cout << "  difficulty  : Difficulty level (optional, defaults to max difficulty)\n";
            std::cout << "  --auto      : Enable auto mode\n";
            std::cout << "  --practice  : Start in practice mode\n";
            exit_now(0);
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            exit_now(1);
        } else if (song_path.empty()) {
            song_path = arg;
        } else if (!difficulty.has_value()) {
            try {
                size_t pos = 0;
                int value = std::stoi(arg, &pos);
                if (pos != arg.size()) throw std::invalid_argument(arg);
                difficulty = value;
            } catch (const std::exception& e) {
                std::cerr << "Error: Invalid difficulty value: " << arg << "\n";
                exit_now(1);
            }
        } else {
            std::cerr << "Error: Unexpected extra argument: " << arg << "\n";
            exit_now(1);
        }
    }

    if (song_path.empty()) {
        std::cerr << "Error: song_path is required\n";
        std::cerr << "Use --help for usage information\n";
        exit_now(1);
    }

    std::filesystem::path path = path_from_arg(song_path);
    if (path.empty()) {
        std::cerr << "Error: Song path could not be interpreted: " << song_path << "\n";
        exit_now(1);
    }
    std::error_code path_ec;
    if (!std::filesystem::exists(path, path_ec) || path_ec) {
        std::cerr << "Error: Song file not found: " << song_path << "\n";
        exit_now(1);
    }

    {
        std::error_code abs_ec;
        std::filesystem::path abs = std::filesystem::absolute(path, abs_ec);
        if (!abs_ec) path = abs;
    }
    SongParser tja = [&]() -> SongParser {
        try {
            return SongParser(path);
        } catch (const std::exception& e) {
            std::cerr << "Error: Failed to parse song file: " << e.what() << "\n";
            exit_now(1);
        }
    }();

    int selected_difficulty;
    if (difficulty.has_value()) {
        auto& course_data = tja.metadata.course_data;
        if (course_data.find(difficulty.value()) == course_data.end()) {
            std::cerr << "Error: Invalid difficulty: " << difficulty.value() << ". Available: ";
            for (const auto& [key, value] : course_data) {
                std::cerr << key << " ";
            }
            std::cerr << "\n";
            exit_now(1);
        }
        selected_difficulty = difficulty.value();
    } else {
        if (tja.metadata.course_data.empty()) {
            selected_difficulty = static_cast<int>(Difficulty::EASY);
        } else {
            selected_difficulty = std::max_element(
                tja.metadata.course_data.begin(),
                tja.metadata.course_data.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; }
            )->first;
        }
    }

    Screens current_screen = practice ? Screens::GAME_PRACTICE : Screens::GAME;
    global_data.session_data[(int)PlayerNum::P1].selected_song = path;
    global_data.session_data[(int)PlayerNum::P1].selected_difficulty = selected_difficulty;
    if (auto_play) global_data.force_auto_play = true;

    return current_screen;
}

struct LoopState {
    std::unordered_map<Screens, std::unique_ptr<Screen>> screens;
    Screens current_screen = Screens::LOADING;
    ray::Camera2D camera   = {};
    std::chrono::steady_clock::duration target_duration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / 60.0));
    std::chrono::time_point<std::chrono::steady_clock> next_frame_time = std::chrono::steady_clock::now();
    FPSCounter fps_counter;
    ray::Color last_color = ray::BLACK;
    double screen_fade_start = 0.0;
};

static bool screen_fade_applies(Screens from, Screens to) {
    switch (to) {
        case Screens::GAME: case Screens::GAME_2P: case Screens::GAME_DAN:
        case Screens::GAME_PRACTICE: case Screens::AI_GAME:
        case Screens::RESULT: case Screens::RESULT_2P: case Screens::DAN_RESULT:
            return false;
        default: break;
    }
    if (from == Screens::LOADING) return false;
    if (from == Screens::SONG_SELECT && to == Screens::DAN_SELECT) return false;
    if (from == Screens::ENTRY && to == Screens::DAN_SELECT) return false;
    return true;
}

static LoopState* g_loop = nullptr;
double g_frame_ms = 0.0;

static void populate_screens(std::unordered_map<Screens, std::unique_ptr<Screen>>& screens,
                              std::optional<Screens> except = std::nullopt) {
    if (except != Screens::ENTRY)           screens[Screens::ENTRY]           = std::make_unique<EntryScreen>();
    if (except != Screens::TITLE)           screens[Screens::TITLE]           = std::make_unique<TitleScreen>();
    if (except != Screens::SONG_SELECT)     screens[Screens::SONG_SELECT]     = std::make_unique<SongSelectScreen>();
    if (except != Screens::SONG_SELECT_2P)  screens[Screens::SONG_SELECT_2P]  = std::make_unique<SongSelect2PScreen>();
    if (except != Screens::LOADING)         screens[Screens::LOADING]         = std::make_unique<LoadingScreen>();
    if (except != Screens::GAME)            screens[Screens::GAME]            = std::make_unique<GameScreen>();
    if (except != Screens::GAME_2P)         screens[Screens::GAME_2P]         = std::make_unique<Game2PScreen>();
    if (except != Screens::GAME_PRACTICE)   screens[Screens::GAME_PRACTICE]   = std::make_unique<PracticeGameScreen>();
    if (except != Screens::PRACTICE_SELECT) screens[Screens::PRACTICE_SELECT] = std::make_unique<PracticeSongSelectScreen>();
    if (except != Screens::RESULT)          screens[Screens::RESULT]          = std::make_unique<ResultScreen>();
    if (except != Screens::RESULT_2P)       screens[Screens::RESULT_2P]       = std::make_unique<Result2PScreen>();
    if (except != Screens::DAN_SELECT)      screens[Screens::DAN_SELECT]      = std::make_unique<DanSelectScreen>();
    if (except != Screens::GAME_DAN)        screens[Screens::GAME_DAN]        = std::make_unique<DanGameScreen>();
    if (except != Screens::DAN_RESULT)      screens[Screens::DAN_RESULT]      = std::make_unique<DanResultScreen>();
    if (except != Screens::SETTINGS)        screens[Screens::SETTINGS]        = std::make_unique<SettingsScreen>();
    if (except != Screens::INPUT_CALI)      screens[Screens::INPUT_CALI]      = std::make_unique<InputCaliScreen>();
    if (except != Screens::GAME_OVER)       screens[Screens::GAME_OVER]       = std::make_unique<GameOverScreen>();
    if (except != Screens::INPUT_TEST)      screens[Screens::INPUT_TEST]      = std::make_unique<InputTestScreen>();
}

void drop_other_screens_for_skin_reload() {
    if (!g_loop) return;
    for (auto it = g_loop->screens.begin(); it != g_loop->screens.end(); ) {
        if (it->first != g_loop->current_screen) it = g_loop->screens.erase(it);
        else ++it;
    }
}

void reload_skin_screens() {
    if (!g_loop) return;
    populate_screens(g_loop->screens, g_loop->current_screen);
}

static void run_frame() {
    LoopState& L = *g_loop;

    g_frame_ms = get_current_ms();

    ray::PollInputEvents();
#if defined(YATAIDON_PLATFORM_IOS)
    if (ios_is_suspended_state()) return;
    poll_keyboard_once();
#endif
    poll_touch_once();

    if (check_key_pressed(global_data.config->keys.fullscreen_key)) {
        ray::ToggleFullscreen();
        spdlog::info("Toggled fullscreen");
    } else if (check_key_pressed(global_data.config->keys.borderless_key)) {
        ray::ToggleBorderlessWindowed();
        spdlog::info("Toggled borderless windowed mode");
    }

    L.camera = compute_camera2d(tex.screen_width, tex.screen_height);
    debug_menu.update(L.camera);

    ray::BeginDrawing();

    if (global_data.camera.border_color != L.last_color) {
        ray::ClearBackground(global_data.camera.border_color);
        L.last_color = global_data.camera.border_color;
    }

    ray::BeginMode2D(L.camera);
    ray::BeginBlendMode(ray::BLEND_CUSTOM_SEPARATE);

    auto screen_it = L.screens.find(L.current_screen);
    Screen* screen = (screen_it != L.screens.end()) ? screen_it->second.get() : nullptr;
    if (!screen) {
        static Screens last_logged_screen = L.current_screen;
        static double last_log_ms = -1e9;
        if (L.current_screen != last_logged_screen || g_frame_ms - last_log_ms > 1000.0) {
            spdlog::error("Active screen {} is not available, attempting recovery", L.current_screen);
            last_logged_screen = L.current_screen;
            last_log_ms = g_frame_ms;
        }
        populate_screens(L.screens);
        screen_it = L.screens.find(L.current_screen);
        screen = (screen_it != L.screens.end()) ? screen_it->second.get() : nullptr;
    }
    if (!screen) {
        ray::EndBlendMode();
        ray::EndMode2D();
        ray::EndDrawing();
        return;
    }

    network.update(g_frame_ms);
    std::optional<Screens> next_screen = screen->update();

    if (!next_screen.has_value() && debug_menu.requested_screen.has_value()) {
        next_screen = screen->on_screen_end(debug_menu.requested_screen.value());
        debug_menu.requested_screen.reset();
    }

    if (screen->screen_init) {
        screen->_do_draw();
    }
    if (L.screen_fade_start > 0.0) {
        const SkinInfo* cfg = tex.skin_entry("screen_fade_ms");
        const double dur = (cfg && cfg->x > 0) ? cfg->x : 500.0;
        const double t = (g_frame_ms - L.screen_fade_start) / dur;
        if (t >= 1.0) {
            L.screen_fade_start = 0.0;
        } else {
            const float a = static_cast<float>(1.0 - (t < 0.0 ? 0.0 : t));
            ray::DrawRectangle(0, 0, tex.screen_width, tex.screen_height, ray::Fade(ray::BLACK, a));
        }
    }

    if (next_screen.has_value()) {
        spdlog::info("Screen changed from {} to {}", L.current_screen, next_screen.value());
        clear_input_buffers();
        if (tex.skin_entry("screen_fade_ms") && screen_fade_applies(L.current_screen, next_screen.value()))
            L.screen_fade_start = g_frame_ms;
        else
            L.screen_fade_start = 0.0;
        global_data.previous_screen = global_data.current_screen;
        L.current_screen = next_screen.value();
        global_data.current_screen = screens_to_string(L.current_screen);
        reset_input_lock();
    }

    draw_touch_drum();

    if (global_data.config->general.fps_counter) {
        L.fps_counter.update();
        L.fps_counter.draw();
    }

    debug_menu.draw();

    draw_outer_border(tex.screen_width, tex.screen_height, L.last_color);

    ray::EndBlendMode();
    ray::EndMode2D();
    ray::EndDrawing();

    if (!next_screen.has_value()) {
        ray::SwapScreenBuffer();
    }

    if (ray::IsKeyPressed(ray::KEY_F12)) {
        static int screenshot_counter = 0;
        ray::Image image = ray::LoadImageFromScreen();
        if (L.current_screen == Screens::RESULT) {
            ray::ImageCrop(&image, {0, 0, (float)image.width, (float)image.height / 2.0f});
        }
        ray::ExportImage(image, ray::TextFormat("screenshot%03i.png", screenshot_counter));
        ray::UnloadImage(image);
        screenshot_counter++;
        spdlog::info("Screenshot saved");
    }

#if !defined(__EMSCRIPTEN__) && !defined(YATAIDON_PLATFORM_IOS)
    if (L.target_duration.count() > 0) {
        L.next_frame_time += L.target_duration;
        auto now = std::chrono::steady_clock::now();
        if (L.next_frame_time < now) {
            L.next_frame_time = now;
        }
        auto spin_start = L.next_frame_time - std::chrono::microseconds(500);
        if (spin_start > now) {
            std::this_thread::sleep_until(spin_start);
        }
        while (std::chrono::steady_clock::now() < L.next_frame_time) { }
    }
#endif
}

int main(int argc, char* argv[]) {
    spdlog::info("Starting YataiDON");
    set_working_directory_to_executable();
    global_data.config = new Config(get_config());
    Screens initial_screen = check_args(argc, argv);
    init_scores_manager(global_data.config->general.score_method == ScoreMethod::GEN3);
    init_tier_list();
    unsigned int flags = ray::FLAG_WINDOW_RESIZABLE;
    if (global_data.config->video.vsync) {
        flags |= ray::FLAG_VSYNC_HINT;
        spdlog::info("VSync enabled");
    }
#ifdef YATAIDON_PLATFORM_IOS
    ios_configure_window_flags(flags);
#endif
    ray::SetConfigFlags(flags);
    ray::SetTraceLogLevel(ray::LOG_ERROR);
    setup_logging(global_data.config->general.log_level);

    ray::InitWindow(1280, 720, "YataiDON");
    load_skin();

    scores_manager.player_1 = global_data.config->general.player_1_id;
    scores_manager.player_2 = global_data.config->general.player_2_id;
    if (auto pd = scores_manager.get_player_data(scores_manager.player_1))
        scores_manager.player_1_data = *pd;
    if (auto pd = scores_manager.get_player_data(scores_manager.player_2))
        scores_manager.player_2_data = *pd;

#ifdef PLATFORM_ANDROID
    android_check_and_install_update();
    android_check_skin_updates();
#endif

#ifdef YATAIDON_PLATFORM_IOS
    ios_initialize_after_window();
#endif

    const bool net_ok = network.probe_online();
    if (net_ok && global_data.config->network.access_code.empty()) {
        std::string access_code = network.register_user(scores_manager.player_1_data.username);
        if (!access_code.empty()) {
            global_data.config->network.access_code = access_code;
            save_config(*global_data.config);
        }
    }

    if (net_ok && !global_data.config->network.access_code.empty() &&
        network.check_import_requested(global_data.config->network.access_code)) {
        spdlog::info("hiroba requested a score import, exporting scores.db");
        scores_manager.export_to_hiroba(global_data.config->network.access_code, scores_manager.player_1);
        network.clear_import_flag(global_data.config->network.access_code);
    }

    if (net_ok && !global_data.config->network.access_code.empty()) {
        ray::Color chara_color_1, chara_color_2, chara_color_3;
        if (network.fetch_chara_colors(global_data.config->network.access_code, chara_color_1, chara_color_2, chara_color_3)) {
            scores_manager.player_1_data.chara_color_1 = chara_color_1;
            scores_manager.player_1_data.chara_color_2 = chara_color_2;
            scores_manager.player_1_data.chara_color_3 = chara_color_3;
            scores_manager.save_player_data(scores_manager.player_1_data);
        }

        std::string server_username;
        if (network.fetch_username(global_data.config->network.access_code, server_username) &&
            !server_username.empty() && server_username != scores_manager.player_1_data.username) {
            scores_manager.player_1_data.username = server_username;
            scores_manager.save_player_data(scores_manager.player_1_data);
        }

        std::string server_title;
        if (network.fetch_title(global_data.config->network.access_code, server_title) &&
            !server_title.empty() && server_title != scores_manager.player_1_data.title) {
            scores_manager.player_1_data.title = server_title;
            scores_manager.save_player_data(scores_manager.player_1_data);
        }

        int server_title_bg;
        if (network.fetch_title_bg(global_data.config->network.access_code, server_title_bg) &&
            server_title_bg != scores_manager.player_1_data.title_bg) {
            scores_manager.player_1_data.title_bg = server_title_bg;
            scores_manager.save_player_data(scores_manager.player_1_data);
        }

        int head_index = scores_manager.player_1_data.chara_head_index;
        int body_index = scores_manager.player_1_data.chara_body_index;
        int cos_index = scores_manager.player_1_data.chara_cos_index;
        bool is_costume = scores_manager.player_1_data.chara_is_costume;
        if (network.fetch_costume(global_data.config->network.access_code, head_index, body_index, cos_index, is_costume) &&
            (head_index != scores_manager.player_1_data.chara_head_index ||
             body_index != scores_manager.player_1_data.chara_body_index ||
             cos_index != scores_manager.player_1_data.chara_cos_index ||
             is_costume != scores_manager.player_1_data.chara_is_costume)) {
            scores_manager.player_1_data.chara_head_index = head_index;
            scores_manager.player_1_data.chara_body_index = body_index;
            scores_manager.player_1_data.chara_cos_index = cos_index;
            scores_manager.player_1_data.chara_is_costume = is_costume;
            scores_manager.save_player_data(scores_manager.player_1_data);
        }

        if (global_data.config->network.sync_scores) {
            scores_manager.sync_from_server(global_data.config->network.access_code);
        }
    }

    double target_fps = global_data.config->video.target_fps;
    if (target_fps != -1) {
        spdlog::info("Target FPS set to {}", target_fps);
    }

    g_loop = new LoopState();
    LoopState& L = *g_loop;

    L.current_screen     = initial_screen;
    global_data.current_screen = screens_to_string(initial_screen);
    L.target_duration    = (target_fps > 0.0)
        ? std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / target_fps))
        : std::chrono::steady_clock::duration::zero();

    populate_screens(L.screens);

    L.camera = compute_camera2d(tex.screen_width, tex.screen_height);

#if !defined(__EMSCRIPTEN__) && !defined(YATAIDON_PLATFORM_IOS)
    if (global_data.config->video.borderless) {
        ray::ToggleBorderlessWindowed();
        spdlog::info("Borderless window enabled");
    }
    if (global_data.config->video.fullscreen) {
        ray::ToggleFullscreen();
        spdlog::info("Fullscreen enabled");
    }
#endif

    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
#if defined(PLATFORM_ANDROID) || defined(YATAIDON_PLATFORM_IOS) || defined(__EMSCRIPTEN__)
    ray::SetExitKey(ray::KEY_NULL);
#else
    ray::SetExitKey(global_data.config->keys.exit_key);
#endif
    ray::HideCursor();

    L.next_frame_time = std::chrono::steady_clock::now();
#ifdef __EMSCRIPTEN__
    return emscripten_run_main_loop(run_frame);
#elif defined(YATAIDON_PLATFORM_IOS)
    return ios_run_main_loop(run_frame);
#else
    input_thread = std::thread(input_polling_thread);

    while (!ray::WindowShouldClose() && !check_key_pressed(global_data.config->keys.exit_key)) {
        run_frame();
    }

    input_thread_running = false;
    if (input_thread.joinable()) {
        input_thread.join();
    }
    network.shutdown();
    shutdown_sdl_joysticks();
    delete g_loop;
    delete global_data.config;
    global_data.config = nullptr;
    global_tex.unload_textures();
    tex.unload_textures();
    script_manager.shutdown();
    ray::CloseWindow();
    audio.close_audio_device();
    spdlog::info("Game closed");
#endif
}
