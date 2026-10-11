#pragma once

#include <filesystem>

// iOS-only platform services. Paths are resolved anew on every launch because
// installing an update can relocate both the bundle and the data container.
//
// Prepares the sandboxed Documents directory and copies bundled game data into
// it (skipping files that already exist, except shaders which are always
// refreshed to match the running binary), then chdirs into it. Throws
// std::runtime_error if the Documents directory cannot be located; other
// per-file copy failures are logged and skipped rather than fatal.
void ios_prepare_filesystem();

// Monotonic (steady_clock-based) milliseconds since an arbitrary epoch, frozen
// while ios_set_suspended(true) is in effect. Safe to call from any thread.
double ios_game_time_ms();

// Suspend/resume state shared with ios_game_time_ms()/ios_is_suspended(); all
// three are internally mutex-guarded, so calling them from different threads
// (e.g. the app lifecycle thread vs. the game/audio thread) is safe.
void ios_set_suspended(bool suspended);
bool ios_is_suspended();

// Requests a low-latency preferred I/O buffer duration from AVAudioSession.
// This does not hand back or take ownership of any audio buffer - it only
// sets a latency hint on the shared AVAudioSession and returns synchronously;
// failures are logged (via spdlog::warn) and otherwise ignored.
void ios_request_audio_buffer();

// iOS-specific window configuration.
// Sets up SDL hints for iOS (orientations, audio category, etc.)
void ios_configure_window_flags(unsigned int& flags);

// iOS-specific initialization after window creation.
void ios_initialize_after_window();

// iOS-specific main loop (UIKit owns the event loop).
// Returns exit code, or -1 if not on iOS.
int ios_run_main_loop(void (*run_frame)());

// iOS-specific shutdown/cleanup.
void ios_shutdown();

// iOS keyboard visibility control.
void ios_set_keyboard_visible(bool visible);

// iOS touch drum navigation labels (Back/Pause buttons).
void ios_draw_touch_navigation_labels(float screen_width, float screen_height);

// Handles iOS-specific touch navigation (Back/Pause buttons in top corners).
// Returns the vkey to use (may be different from the original quadrant vkey).
int ios_handle_touch_navigation(int quadrant_vkey, float touch_x, float touch_y);

// Checks if iOS is currently suspended.
bool ios_is_suspended_state();

// Called when app enters background.
void ios_on_background();

// Called when app enters foreground.
void ios_on_foreground();

// iOS audio session suspension/resumption.
void ios_suspend_audio(bool suspended);

// Checks if we're on iOS (for compile-time platform detection).
constexpr bool ios_is_ios_platform() { return true; }
