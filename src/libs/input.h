#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include "ray.h" // IWYU pragma: keep
#include "global_data.h"

extern std::atomic<bool> input_thread_running;
extern std::thread input_thread;

extern std::atomic<bool> touch_drum_pressed;

void input_polling_thread();
void poll_keyboard_once();
void poll_touch_once();

// Check if a key was pressed since the last check
// This consumes the key press event
bool check_key_pressed(int key);

// Check if a key was released since the last check
// This consumes the key release event
bool check_key_released(int key);

// Most recent controller button press, as the bare button number the
// config stores (-1 if none since the last call, which this consumes; 0 is
// a valid real button, so it can't double as the "no event" sentinel).
// Covers SDL joysticks too, unlike raylib's GetGamepadButtonPressed which
// only sees devices it has a gamepad mapping for.
int take_gamepad_button_pressed();

double get_last_input_ms();

// Clear all buffered input events
// Useful when changing screens or locking input
void clear_input_buffers();

// Dev/screenshot runs: queue a synthetic input press. Names: don, kat,
// ldon, rdon, lkat, rkat (aliases: enter/left/right). Injected directly
// into the press buffer, so no OS-level key events are emitted and the
// user's own session / other game instances can never see them.
void dev_inject_input(const std::string& name);
void shutdown_sdl_joysticks();
void android_set_keyboard_visible(bool visible);

// Enable/disable touch drum
void set_touch_drum_enabled(bool enabled);

// Draw the touch drum overlay (returns true if drawn, false if hidden)
bool draw_touch_drum();

bool is_input_key_pressed(const std::vector<int>& keys, const std::vector<int>& gamepad_buttons);
bool is_l_don_pressed(PlayerNum player_num = PlayerNum::ALL);
bool is_r_don_pressed(PlayerNum player_num = PlayerNum::ALL);
bool is_l_kat_pressed(PlayerNum player_num = PlayerNum::ALL);
bool is_r_kat_pressed(PlayerNum player_num = PlayerNum::ALL);

namespace ray {
inline bool operator==(const Color& a, const Color& b)
{
    return a.r == b.r &&
           a.g == b.g &&
           a.b == b.b &&
           a.a == b.a;
}

inline bool operator!=(const Color& a, const Color& b)
{
    return !(a == b);
}
}  // namespace ray
