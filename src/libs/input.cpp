#include "input.h"
#include "dev_mode.h"
#include <string>
#include "animation.h"
#include "spdlog/spdlog.h"
#include "texture.h"
#include "time.h"
#include <array>
#include <unordered_set>

#ifdef _WIN32
#include "../platform/platform_windows.h"
#elif defined(__ANDROID__)
#include "../platform/platform_android.h"
#else
#include "../platform/platform_linux.h"
#endif


#include <SDL3/SDL.h>
std::atomic<bool> input_thread_running{true};
std::thread input_thread;

static std::atomic<double> last_input_ms{0.0};

static std::unordered_map<SDL_JoystickID, SDL_Joystick*> sdl_joysticks;
// keyed by joy_id * 256 + button_index
static std::unordered_map<int64_t, bool>  sdl_prev_button;
// keyed by joy_id * 256 + axis_index
static std::unordered_map<int64_t, float> sdl_prev_axis;
static bool sdl_joysticks_init_done = false;

static void refresh_sdl_joysticks() {
    bool& init_done = sdl_joysticks_init_done;
    if (!init_done) {
        if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK)) {
            spdlog::error("Failed to init SDL joystick subsystem: {}", SDL_GetError());
            return; // retry on the next call instead of latching a failed init
        }
        init_done = true;
    }

    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    std::unordered_set<SDL_JoystickID> current(ids, ids + count);

    for (auto it = sdl_joysticks.begin(); it != sdl_joysticks.end();) {
        if (!current.count(it->first)) {
            SDL_CloseJoystick(it->second);
            it = sdl_joysticks.erase(it);
        } else {
            ++it;
        }
    }

    for (int i = 0; i < count; i++) {
        if (SDL_IsGamepad(ids[i])) continue; // already handled by raylib
        if (!sdl_joysticks.count(ids[i])) {
            SDL_Joystick* joy = SDL_OpenJoystick(ids[i]);
            if (joy) {
                sdl_joysticks[ids[i]] = joy;
                spdlog::info("Joystick fallback: opened '{}' (id {})",
                             SDL_GetJoystickName(joy), (int)ids[i]);
            }
        }
    }

    SDL_free(ids);
}

// Not exposed in input.h: every access must go through check_key_pressed/
// check_key_released/clear_input_buffers below, which hold input_mutex --
// keeping these file-local prevents external code from touching the
// containers without the lock.
static std::mutex input_mutex;
static std::unordered_multiset<int> pressed_keys;
static std::unordered_multiset<int> released_keys;

static const int TOUCH_L_KAT = 40001;
static const int TOUCH_R_KAT = 40002;
static const int TOUCH_L_DON = 40003;
static const int TOUCH_R_DON = 40004;

static std::unordered_map<SDL_FingerID, int> touch_id_to_vkey;

std::atomic<bool> touch_drum_pressed{false};

// Track if touch drum is enabled (drawing + input, controlled by screens)
static std::atomic<bool> touch_drum_enabled{true};

void set_touch_drum_enabled(bool enabled) {
    touch_drum_enabled.store(enabled, std::memory_order_relaxed);
}

static std::array<bool, 349> previous_key_states{};
static std::array<std::array<bool, 18>, 4> previous_gamepad_states{};

// Gamepad/joystick buttons and axes are folded into the key space at these
// offsets (config stores the bare button number).
static constexpr int GAMEPAD_VKEY_BASE = 10000;
static constexpr int AXIS_VKEY_BASE    = 20000;
static std::atomic<int> last_gamepad_vkey{0};

bool is_input_key_pressed(const std::vector<int>& keys, const std::vector<int>& gamepad_buttons) {

    for (int key : keys) {
        if (check_key_pressed(key)) return true;
    }

    // Check gamepad buttons (offset by 10000)
    if (gamepad_buttons.empty()) return false;

    for (int button : gamepad_buttons) {
        if (check_key_pressed(10000 + button)) return true;
    }
    return false;
}

bool is_l_don_pressed(PlayerNum player_num) {
    if (player_num == PlayerNum::P1) {
        bool a = is_input_key_pressed(global_data.config->keys_1p.left_don, global_data.config->gamepad_1p.left_don);
        bool b = check_key_pressed(TOUCH_L_DON);
        return a || b;
    } else if (player_num == PlayerNum::P2) {
        return is_input_key_pressed(global_data.config->keys_2p.left_don, global_data.config->gamepad_2p.left_don);
    } else if (player_num != PlayerNum::ALL) {
        return false;
    }
    std::vector<int> keys = global_data.config->keys_1p.left_don;
    const auto& keys_2p = global_data.config->keys_2p.left_don;
    keys.insert(keys.end(), keys_2p.begin(), keys_2p.end());
    std::vector<int> gamepad_buttons = global_data.config->gamepad_1p.left_don;
    const auto& gp2 = global_data.config->gamepad_2p.left_don;
    gamepad_buttons.insert(gamepad_buttons.end(), gp2.begin(), gp2.end());
    bool a = is_input_key_pressed(keys, gamepad_buttons);
    bool b = check_key_pressed(TOUCH_L_DON);
    return a || b;
}

bool is_r_don_pressed(PlayerNum player_num) {
    if (player_num == PlayerNum::P1) {
        bool a = is_input_key_pressed(global_data.config->keys_1p.right_don, global_data.config->gamepad_1p.right_don);
        bool b = check_key_pressed(TOUCH_R_DON);
        return a || b;
    } else if (player_num == PlayerNum::P2) {
        return is_input_key_pressed(global_data.config->keys_2p.right_don, global_data.config->gamepad_2p.right_don);
    } else if (player_num != PlayerNum::ALL) {
        return false;
    }
    std::vector<int> keys = global_data.config->keys_1p.right_don;
    const auto& keys_2p = global_data.config->keys_2p.right_don;
    keys.insert(keys.end(), keys_2p.begin(), keys_2p.end());
    std::vector<int> gamepad_buttons = global_data.config->gamepad_1p.right_don;
    const auto& gp2 = global_data.config->gamepad_2p.right_don;
    gamepad_buttons.insert(gamepad_buttons.end(), gp2.begin(), gp2.end());
    bool a = is_input_key_pressed(keys, gamepad_buttons);
    bool b = check_key_pressed(TOUCH_R_DON);
    return a || b;
}

bool is_l_kat_pressed(PlayerNum player_num) {
    if (player_num == PlayerNum::P1) {
        bool a = is_input_key_pressed(global_data.config->keys_1p.left_kat, global_data.config->gamepad_1p.left_kat);
        bool b = check_key_pressed(TOUCH_L_KAT);
        return a || b;
    } else if (player_num == PlayerNum::P2) {
        return is_input_key_pressed(global_data.config->keys_2p.left_kat, global_data.config->gamepad_2p.left_kat);
    } else if (player_num != PlayerNum::ALL) {
        return false;
    }
    std::vector<int> keys = global_data.config->keys_1p.left_kat;
    const auto& keys_2p = global_data.config->keys_2p.left_kat;
    keys.insert(keys.end(), keys_2p.begin(), keys_2p.end());
    std::vector<int> gamepad_buttons = global_data.config->gamepad_1p.left_kat;
    const auto& gp2 = global_data.config->gamepad_2p.left_kat;
    gamepad_buttons.insert(gamepad_buttons.end(), gp2.begin(), gp2.end());
    bool a = is_input_key_pressed(keys, gamepad_buttons);
    bool b = check_key_pressed(TOUCH_L_KAT);
    return a || b;
}

bool is_r_kat_pressed(PlayerNum player_num) {
    if (player_num == PlayerNum::P1) {
        bool a = is_input_key_pressed(global_data.config->keys_1p.right_kat, global_data.config->gamepad_1p.right_kat);
        bool b = check_key_pressed(TOUCH_R_KAT);
        return a || b;
    } else if (player_num == PlayerNum::P2) {
        return is_input_key_pressed(global_data.config->keys_2p.right_kat, global_data.config->gamepad_2p.right_kat);
    } else if (player_num != PlayerNum::ALL) {
        return false;
    }
    std::vector<int> keys = global_data.config->keys_1p.right_kat;
    const auto& keys_2p = global_data.config->keys_2p.right_kat;
    keys.insert(keys.end(), keys_2p.begin(), keys_2p.end());
    std::vector<int> gamepad_buttons = global_data.config->gamepad_1p.right_kat;
    const auto& gp2 = global_data.config->gamepad_2p.right_kat;
    gamepad_buttons.insert(gamepad_buttons.end(), gp2.begin(), gp2.end());
    bool a = is_input_key_pressed(keys, gamepad_buttons);
    bool b = check_key_pressed(TOUCH_R_KAT);
    return a || b;
}

static int touch_quadrant_vkey(ray::Vector2 pos, int sw, int sh) {
    bool left = pos.x < sw / 2.0f;
    bool top  = pos.y < sh / 2.0f;
    if (top) return left ? TOUCH_L_KAT : TOUCH_R_KAT;

    float render_scale = std::min((float)sw / tex.screen_width, (float)sh / tex.screen_height);
    float cx = sw * 0.5f;
    float cy = (float)sh * 0.5f + tex.screen_height * render_scale * 0.5f;

    float rx = tex.screen_width * 0.262f * render_scale;
    float ry = tex.screen_width * 0.242f * render_scale;

    float dx = pos.x - cx;
    float dy = pos.y - cy;

    float nx = dx / rx;
    float ny = dy / ry;
    bool in_drum = (nx * nx + ny * ny) <= 1.0f;
    if (in_drum) return left ? TOUCH_L_DON : TOUCH_R_DON;
    return left ? TOUCH_L_KAT : TOUCH_R_KAT;
}

static bool touch_watch_registered = false;

static int char_to_raylib_key(unsigned char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 32 && c <= 96) return c;
    return 0;
}

static bool SDLCALL touch_event_watch(void* /*userdata*/, SDL_Event* event) {
    if (is_input_locked()) return 1;

    #ifdef __linux__
    if (linux_handle_text_input(event, is_input_locked(), input_mutex, pressed_keys, released_keys)) {
        return 1;
    }
    #endif

    if (event->type == SDL_EVENT_KEY_DOWN &&
        event->key.scancode == SDL_SCANCODE_AC_BACK) {
        std::lock_guard<std::mutex> lock(input_mutex);
        pressed_keys.insert(ray::KEY_ESCAPE);
        return 0;
    }
    if (event->type == SDL_EVENT_KEY_UP &&
        event->key.scancode == SDL_SCANCODE_AC_BACK) {
        std::lock_guard<std::mutex> lock(input_mutex);
        released_keys.insert(ray::KEY_ESCAPE);
        return 0;
    }

    if (event->type == SDL_EVENT_FINGER_DOWN) {
        if (!global_data.config || !global_data.config->general.touch_input) return 1;
        if (!touch_drum_enabled.load(std::memory_order_relaxed)) return 1;
        SDL_FingerID id = event->tfinger.fingerID;
        std::lock_guard<std::mutex> lock(input_mutex);
        if (!touch_id_to_vkey.count(id)) {
            int sw = ray::GetScreenWidth();
            int sh = ray::GetScreenHeight();
            ray::Vector2 pos = { event->tfinger.x * sw, event->tfinger.y * sh };
            int vkey = touch_quadrant_vkey(pos, sw, sh);
#ifdef YATAIDON_PLATFORM_IOS
            vkey = ios_handle_touch_navigation(vkey, event->tfinger.x, event->tfinger.y);
#endif
            touch_id_to_vkey[id] = vkey;
            touch_drum_pressed.store(true, std::memory_order_relaxed);
            last_input_ms.store(get_current_ms(), std::memory_order_relaxed);
            pressed_keys.insert(vkey);
        }
    } else if (event->type == SDL_EVENT_FINGER_UP ||
               event->type == SDL_EVENT_FINGER_CANCELED) {
        SDL_FingerID id = event->tfinger.fingerID;
        std::lock_guard<std::mutex> lock(input_mutex);
        auto it = touch_id_to_vkey.find(id);
        if (it != touch_id_to_vkey.end()) {
            released_keys.insert(it->second);
            touch_id_to_vkey.erase(it);
        }
        touch_drum_pressed.store(!touch_id_to_vkey.empty(), std::memory_order_relaxed);
    }
    return 1;
}

bool draw_touch_drum() {
    if (!touch_drum_enabled.load(std::memory_order_relaxed)) return false;
    if (!global_data.config || !global_data.config->general.touch_input) return false;

    auto* touch_drum_resize = static_cast<TextureResizeAnimation*>(global_tex.get_animation(66));
    if (!touch_drum_resize) return false;

    if (!touch_drum_resize->isStarted()) touch_drum_resize->start();
    if (touch_drum_pressed.exchange(false, std::memory_order_relaxed))
        touch_drum_resize->restart();
    touch_drum_resize->update(get_current_ms());
    const float scale = (float)touch_drum_resize->attribute;
    float y_fix = 0.0f;
    auto drum_it = global_tex.textures.find("overlay/touch_drum");
    if (drum_it != global_tex.textures.end())
        y_fix = drum_it->second->height * 0.5f * (1.0f - scale);
    global_tex.draw_texture(global_tex.get_texture("overlay/touch_drum"), {.scale=scale, .center=true, .y=y_fix, .fade=0.5f});
#ifdef YATAIDON_PLATFORM_IOS
    ios_draw_touch_navigation_labels(static_cast<float>(ray::GetScreenWidth()), static_cast<float>(ray::GetScreenHeight()));
#endif
    return true;
}

void poll_touch_once() {
    if (!touch_watch_registered) {
        SDL_AddEventWatch(touch_event_watch, nullptr);
        touch_watch_registered = true;
    }
}

// Scan all keyboard keys once and push press/release events.
// Used by the polling thread on desktop and called directly per-frame on web.
void poll_keyboard_once() {
    if (is_input_locked()) return;
    thread_local std::vector<int> local_pressed;
    thread_local std::vector<int> local_released;
    local_pressed.clear();
    local_released.clear();

#ifdef _WIN32
    for (int key = 32; key < 349; key++) {
        bool current_state  = win32_is_key_down_native(key);
        bool previous_state = previous_key_states[key];
        if (current_state  && !previous_state) local_pressed.push_back(key);
        if (!current_state && previous_state)  local_released.push_back(key);
        previous_key_states[key] = current_state;
    }
#else
    for (int key = 32; key < 349; key++) {
        bool current_state  = ray::IsKeyDown(key);
        bool previous_state = previous_key_states[key];
        if (current_state  && !previous_state) local_pressed.push_back(key);
        if (!current_state && previous_state)  local_released.push_back(key);
        previous_key_states[key] = current_state;
    }
#endif

    for (int gamepad = 0; gamepad < 4; gamepad++) {
        if (!ray::IsGamepadAvailable(gamepad)) continue;
        for (int btn = 1; btn <= 17; btn++) {
            int key = 10000 + btn;
            bool current_state  = ray::IsGamepadButtonDown(gamepad, btn);
            bool previous_state = previous_gamepad_states[gamepad][btn];
            if (current_state  && !previous_state) local_pressed.push_back(key);
            if (!current_state && previous_state)  local_released.push_back(key);
            previous_gamepad_states[gamepad][btn] = current_state;
        }

    }

    refresh_sdl_joysticks();
    constexpr float JOYSTICK_AXIS_THRESHOLD = 0.5f;

    for (auto& [joy_id, joy] : sdl_joysticks) {
        int num_buttons = SDL_GetNumJoystickButtons(joy);
        for (int btn = 0; btn < num_buttons && btn < 32; btn++) {
            int64_t state_key = (int64_t)joy_id * 256 + btn;
            // Use 1-indexed encoding to match raylib's gamepad button convention
            int vkey = 10000 + btn + 1;
            bool cur  = SDL_GetJoystickButton(joy, btn);
            bool prev = sdl_prev_button[state_key];
            if (cur  && !prev) local_pressed.push_back(vkey);
            if (!cur && prev)  local_released.push_back(vkey);
            sdl_prev_button[state_key] = cur;
        }

        int num_axes = SDL_GetNumJoystickAxes(joy);
        for (int axis = 0; axis < num_axes && axis < 8; axis++) {
            int64_t state_key = (int64_t)joy_id * 256 + axis;
            float value = SDL_GetJoystickAxis(joy, axis) / 32767.0f;
            float prev  = sdl_prev_axis[state_key];
            int key_pos = 20000 + axis * 2;
            int key_neg = 20000 + axis * 2 + 1;
            bool cur_pos  = value >  JOYSTICK_AXIS_THRESHOLD;
            bool cur_neg  = value < -JOYSTICK_AXIS_THRESHOLD;
            bool prev_pos = prev  >  JOYSTICK_AXIS_THRESHOLD;
            bool prev_neg = prev  < -JOYSTICK_AXIS_THRESHOLD;
            if (cur_pos  && !prev_pos) local_pressed.push_back(key_pos);
            if (!cur_pos && prev_pos)  local_released.push_back(key_pos);
            if (cur_neg  && !prev_neg) local_pressed.push_back(key_neg);
            if (!cur_neg && prev_neg)  local_released.push_back(key_neg);
            sdl_prev_axis[state_key] = value;
        }
    }

    // Remember the newest controller press separately from pressed_keys so
    // the keybind screen can read it: the pressed_keys entry is consumed by
    // whatever navigates the menu before the option box ever sees it.
    for (int vkey : local_pressed) {
        if (vkey >= GAMEPAD_VKEY_BASE && vkey < AXIS_VKEY_BASE) {
            last_gamepad_vkey.store(vkey, std::memory_order_relaxed);
            break;
        }
    }

    if (!local_pressed.empty()) last_input_ms.store(get_current_ms(), std::memory_order_relaxed);

    if (!local_pressed.empty() || !local_released.empty()) {
        std::lock_guard<std::mutex> lock(input_mutex);
        pressed_keys.insert(local_pressed.begin(), local_pressed.end());
        released_keys.insert(local_released.begin(), local_released.end());
    }
}

double get_last_input_ms() {
    return last_input_ms.load(std::memory_order_relaxed);
}

int take_gamepad_button_pressed() {
    int vkey = last_gamepad_vkey.exchange(0, std::memory_order_relaxed);
    if (vkey < GAMEPAD_VKEY_BASE || vkey >= AXIS_VKEY_BASE) return -1;
    return vkey - GAMEPAD_VKEY_BASE;
}

void input_polling_thread() {
    while (input_thread_running) {
        poll_keyboard_once();
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
}

// Dev/screenshot runs: queue a synthetic press in the same buffer the
// polling thread fills, so screens react exactly as they would to a real hit.
// Never emits OS-level events.
void dev_inject_input(const std::string& name) {
    int code = 0;
    if (name == "reload") { g_dev.reload_requested = true; return; }
    if      (name == "don"   || name == "ldon" || name == "enter") code = TOUCH_L_DON;
    else if (name == "rdon")                                        code = TOUCH_R_DON;
    else if (name == "kat"   || name == "lkat" || name == "left")  code = TOUCH_L_KAT;
    else if (name == "rkat"  || name == "right")                    code = TOUCH_R_KAT;
    else {
        spdlog::warn("dev_inject_input: unknown input '{}'", name);
        return;
    }
    std::lock_guard<std::mutex> lock(input_mutex);
    pressed_keys.insert(code);
}

bool check_key_pressed(int key) {
    std::lock_guard<std::mutex> lock(input_mutex);
    auto it = pressed_keys.find(key);
    if (it != pressed_keys.end()) {
        pressed_keys.erase(it);
        return true;
    }
    return false;
}

bool check_key_released(int key) {
    std::lock_guard<std::mutex> lock(input_mutex);
    auto it = released_keys.find(key);
    if (it != released_keys.end()) {
        released_keys.erase(it);
        return true;
    }
    return false;
}

void clear_input_buffers() {
    std::lock_guard<std::mutex> lock(input_mutex);
    pressed_keys.clear();
    released_keys.clear();
}

// Caller must join input_polling_thread (which owns refresh_sdl_joysticks())
// before calling this -- sdl_joysticks is unsynchronized, so concurrent
// access here would race on the map and use-after-free a closed joystick.
void shutdown_sdl_joysticks() {
    for (auto& [id, joy] : sdl_joysticks) {
        SDL_CloseJoystick(joy);
    }
    sdl_joysticks.clear();
    sdl_prev_button.clear();
    sdl_prev_axis.clear();
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    sdl_joysticks_init_done = false;
}

void android_set_keyboard_visible(bool visible) {
#if defined(__ANDROID__)
    // On Android, this is handled by the platform-specific Java code
    // The C++ side just signals the intent
    (void)visible;
#else
    // On iOS and other platforms, use SDL directly
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
#endif
}
