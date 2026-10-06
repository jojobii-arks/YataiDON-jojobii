#pragma once

#include <toml++/toml.h>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct GeneralConfig {
    bool fps_counter = false;
    int audio_offset = 0;
    int visual_offset = 0;
    std::string language;
    bool timer_frozen = false;
    bool song_timer = false;
    bool judge_counter = false;
    std::string log_level;
    int practice_mode_bar_delay = 0;
    std::string score_method;
    bool display_bpm = false;
    int song_limit = 0;
    int webcam_number = -1;
    int player_1_id = 1;
    int player_2_id = 1;
    bool touch_input = false;
};

struct NetworkConfig {
    std::string access_code;
    bool online_play = false;
    bool sync_scores = false;
};

struct PathsConfig {
    std::vector<fs::path> tja_path;
    fs::path skin;
};

struct KeysConfig {
    int exit_key = 0;
    int fullscreen_key = 0;
    int borderless_key = 0;
    int pause_key = 0;
    int back_key = 0;
    int restart_key = 0;
};

struct Keys1PConfig {
    std::vector<int> left_kat;
    std::vector<int> left_don;
    std::vector<int> right_don;
    std::vector<int> right_kat;
};

struct Keys2PConfig {
    std::vector<int> left_kat;
    std::vector<int> left_don;
    std::vector<int> right_don;
    std::vector<int> right_kat;
};

struct GamepadConfig {
    std::vector<int> left_kat;
    std::vector<int> left_don;
    std::vector<int> right_don;
    std::vector<int> right_kat;
};

struct AudioConfig {
    int device_type = 0;
    int sample_rate = 44100;
    int buffer_size = 512;
    std::vector<int> asio_channel;
};

struct VolumeConfig {
    float global = 1.0f;
    float sound = 1.0f;
    float music = 1.0f;
    float voice = 1.0f;
    float hitsound = 1.0f;
    float attract_mode = 1.0f;
};

struct VideoConfig {
    bool fullscreen = false;
    bool borderless = false;
    int target_fps = 60;
    bool vsync = true;
};

// Visual tuning for the Hidden modifier (F3 helper).
// fade_percent: share of the hide zone used for the soft fade-out (0 = hard cut, 100 = fade across the whole zone).
struct HiddenConfig {
    int fade_percent = 25;
};

struct Config {
    GeneralConfig general;
    NetworkConfig network;
    PathsConfig paths;
    KeysConfig keys;
    Keys1PConfig keys_1p;
    Keys2PConfig keys_2p;
    GamepadConfig gamepad_1p;
    GamepadConfig gamepad_2p;
    AudioConfig audio;
    VolumeConfig volume;
    VideoConfig video;
    HiddenConfig hidden;
};

std::string getKeyString(int key_code);

std::vector<int> parseIntArray(const toml::array& arr);

Config get_config();

void save_config(const Config& config);
