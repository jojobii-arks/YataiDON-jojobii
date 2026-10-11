#include "platform_ios.h"
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#import <SDL3/SDL.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <stdexcept>

#include "../libs/input.h"
#include "../libs/global_data.h"

namespace {
std::mutex clock_mutex;
bool suspended = false;
double paused_at = 0;
double paused_duration = 0;
void (*g_ios_frame_callback)() = nullptr;

double monotonic_ms() {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

double ios_game_time_ms() {
    std::lock_guard<std::mutex> lock(clock_mutex);
    return (suspended ? paused_at : monotonic_ms()) - paused_duration;
}

void ios_set_suspended(bool value) {
    std::lock_guard<std::mutex> lock(clock_mutex);
    if (value == suspended) return;
    if (value) paused_at = monotonic_ms();
    else paused_duration += monotonic_ms() - paused_at;
    suspended = value;
}

bool ios_is_suspended() {
    std::lock_guard<std::mutex> lock(clock_mutex);
    return suspended;
}

void ios_request_audio_buffer() {
    @autoreleasepool {
        NSError* error = nil;
        if (![[AVAudioSession sharedInstance] setPreferredIOBufferDuration:0.005 error:&error]) {
            spdlog::warn("iOS latency: buffer preference rejected: {}", error.localizedDescription.UTF8String);
        }
    }
}

void ios_prepare_filesystem() {
    namespace fs = std::filesystem;
    @autoreleasepool {
        NSURL* documents = [[[NSFileManager defaultManager]
            URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask] firstObject];
        if (!documents) throw std::runtime_error("Cannot locate iOS Documents directory");
        fs::path destination(documents.fileSystemRepresentation);
        fs::path resources([NSBundle mainBundle].resourcePath.fileSystemRepresentation);
        resources /= "GameData";
        fs::create_directories(destination);

        // Keep the existing relative-path asset loaders and all writable files
        // together. Copy only missing files so upgrades preserve user content.
        std::error_code ec;
        if (!fs::is_directory(resources, ec)) {
            spdlog::warn("iOS: bundled GameData missing or not a directory: {}", resources.string());
        } else {
            for (const auto& entry : fs::recursive_directory_iterator(resources, ec)) {
                // The iterator already yields paths rooted at resources. Avoid
                // canonicalizing both paths (and querying every ancestor) per file.
                fs::path relative = entry.path().lexically_relative(resources);
                fs::path target = destination / relative;
                if (entry.is_directory(ec)) {
                    fs::create_directories(target, ec);
                } else if (entry.is_regular_file(ec)) {
                    // Parent directories were visited before their children.
                    // Shaders ship with the executable and must match its version.
                    bool shader = *relative.begin() == "shader";
                    if (shader || !fs::exists(target, ec)) {
                        fs::copy_file(entry.path(), target, shader ? fs::copy_options::overwrite_existing
                                                                  : fs::copy_options::skip_existing, ec);
                        if (ec) {
                            spdlog::warn("iOS: failed to copy {}: {}", entry.path().string(), ec.message());
                            ec.clear();
                        }
                    }
                }
            }
        }
        fs::create_directories(destination / "Songs");
        fs::current_path(destination);
    }
}

void ios_configure_window_flags(unsigned int& flags) {
    // UIKit owns the event loop. SDL_WaitEvent while minimized would prevent
    // UIKit from delivering the foreground event that wakes the game again.
    flags = ray::FLAG_VSYNC_HINT | ray::FLAG_WINDOW_ALWAYS_RUN;
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_AUDIO_CATEGORY, "playback");
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
}

void ios_initialize_after_window() {
    ios_request_audio_buffer();
    
    // Register lifecycle event handler
    SDL_AddEventWatch([](void* userdata, SDL_Event* event) -> bool {
        if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND) {
            ios_on_background();
        } else if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND) {
            ios_on_foreground();
        }
        return true;
    }, nullptr);
}

int ios_run_main_loop(void (*run_frame)()) {
    poll_touch_once();
    g_ios_frame_callback = run_frame;
    
    int window_count = 0;
    SDL_Window** windows = SDL_GetWindows(&window_count);
    if (!windows || window_count == 0) {
        SDL_free(windows);
        return 1;
    }
    
    bool registered = SDL_SetiOSAnimationCallback(windows[0], 1,
        [](void*) { g_ios_frame_callback(); }, nullptr);
    SDL_free(windows);
    
    if (!registered) return 1;
    
    // UIKit owns the loop. No background input thread may outlive main().
    return 0;
}

void ios_shutdown() {
    // iOS cleanup - UIKit manages the main loop
}

void ios_set_keyboard_visible(bool visible) {
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    if (!windows || count == 0) return;
    SDL_Window* win = windows[0];
    SDL_free(windows);
    
    if (visible) {
        SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "1");
        SDL_StartTextInput(win);
    } else {
        SDL_StopTextInput(win);
        SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
    }
}

void ios_draw_touch_navigation_labels(float screen_width, float screen_height) {
    int font_size = std::max(16, static_cast<int>(screen_height * 0.04f));
    const char* labels[] = {"Back", "Pause"};
    for (int i = 0; i < 2; ++i) {
        float x = screen_width * (0.36f + i * 0.15f);
        ray::DrawRectangleRec({x, screen_height * 0.025f, screen_width * 0.13f, screen_height * 0.10f}, ray::Fade(ray::BLACK, 0.6f));
        ray::DrawText(labels[i], static_cast<int>(x + (screen_width * 0.13f - ray::MeasureText(labels[i], font_size)) / 2),
            static_cast<int>(screen_height * 0.075f - font_size / 2), font_size, ray::WHITE);
    }
}

bool ios_is_suspended_state() {
    return ios_is_suspended();
}

void ios_on_background() {
    ios_set_suspended(true);
    ios_suspend_audio(true);
    clear_input_buffers();
}

void ios_on_foreground() {
    clear_input_buffers();
    ios_set_suspended(false);
    ios_suspend_audio(false);
}

void ios_suspend_audio(bool suspended) {
    // Implemented directly in platform layer
    #import <SDL3/SDL.h>
    extern SDL_AudioStream* g_ios_audio_stream;  // Defined in audio.cpp
    if (!g_ios_audio_stream) return;
    if (suspended) SDL_PauseAudioStreamDevice(g_ios_audio_stream);
    else SDL_ResumeAudioStreamDevice(g_ios_audio_stream);
}

int ios_handle_touch_navigation(int quadrant_vkey, float touch_x, float touch_y) {
    // The two top-center controls remain clear of landscape notches.
    bool navigation = touch_y >= 0.025f && touch_y <= 0.125f;
    if (navigation && touch_x >= 0.36f && touch_x <= 0.49f) {
        // Back button area
        if (global_data.config) return global_data.config->keys.back_key;
    } else if (navigation && touch_x >= 0.51f && touch_x <= 0.64f) {
        // Pause button area
        if (global_data.config) return global_data.config->keys.pause_key;
    }
    return quadrant_vkey;
}