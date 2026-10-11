#pragma once

#include "../libs/screen.h"
#include "../objects/global/allnet_indicator.h"
#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include <optional>
#include <filesystem>

class LoadingScreen : public Screen {
private:
    std::atomic<bool> loading_complete{false};
    std::atomic<float> progress{0.0f};
    std::vector<fs::path> songs;

    float progress_bar_width;
    float progress_bar_height;
    float progress_bar_x;
    float progress_bar_y;

    TextureObject* t_warning = nullptr;

    // skin_config "loading_countdown": x = centre, y = top, width = digit pitch, height = seconds
    // (0 = none). The screen stays at least that long; kidou/countdown frames 0-9 are the digits,
    // frame 10 the separator, drawn as S"CC (seconds, hundredths) counting down.
    TextureObject* t_countdown = nullptr;
    double countdown_ms = 0.0;
    double start_ms = 0.0;
    // a don (1P / 2P: F J / X C by default) skips the rest of the countdown; the song scan still
    // has to finish, so a press during it is kept until then
    bool skip_requested = false;

    std::thread loading_thread;

    std::unique_ptr<FadeAnimation> fade_in;
    std::optional<AllNetIcon> allnet_indicator;

    bool skin_reloaded = false;

    void init_visuals();

    void load_song_hashes();

    void load_navigator();

public:
    LoadingScreen() : Screen("loading") {
    }

    ~LoadingScreen() override {
        if (loading_thread.joinable()) {
            loading_thread.join();
        }
    }

    void on_screen_start() override;

    Screens on_screen_end(Screens next_screen) override;

    std::optional<Screens> update() override;

    void draw() override;
};
