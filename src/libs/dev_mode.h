#pragma once

#include <string>
#include <vector>

// Dev-mode options for automated/headless screenshot runs and live skin
// iteration. Everything here stays at its default (all features off) unless a
// dev flag is passed on the command line, so a normal build is unaffected.
//
// Isolation is the caller's job: the game sets its working directory to its
// own executable directory, so a dev run should live in its own folder with
// junctions to Skins/data/shader plus its own config.toml and scores.db --
// that way it can never write into the playable build.
struct DevOptions {
    bool enabled = false;            // any dev flag was passed
    bool no_audio = false;           // skip audio device init (silent, no ASIO contention)
    bool offline = false;            // never touch the network (no register / sync)
    bool hidden_window = false;      // create the window hidden (render offscreen only)
    std::string window_pos;          // "x,y": park a visible dev window elsewhere
    std::string screen;              // screen to enter once the song library is ready
    std::string shot_path;           // exact PNG path for the automatic screenshot
    double shot_wait_seconds = 2.0;  // settle time on the target screen before the shot
    bool exit_after_shot = true;     // quit once the shot is written
    std::vector<std::string> keys;   // inputs to inject before the shot (don/kat/left/right)
    double key_gap_seconds = 0.4;    // spacing between injected inputs
    bool reload_requested = false;   // set to re-run the skin reload between frames
    bool quit = false;               // set to leave the main loop
};

inline DevOptions g_dev;