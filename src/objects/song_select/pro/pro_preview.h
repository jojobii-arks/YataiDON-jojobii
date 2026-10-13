#pragma once

// Pro select v2 — the song preview.
//
// The wheel plays a preview per focused SongBox through the music stream named
// "preview" (see box_song.cpp). Pro deliberately builds no SongBoxes, so this
// owns that same "preview" stream directly, with the same primitives
// (load_music_stream / load_music_stream_prepared + the off-thread .nus3bank
// decode) rather than inventing a parallel mechanism.
//
// The dedupe key is the SONG (the tja path), not the row: selecting another
// course of the same song — an adjacent Oni/Ura row, or the CHANGE DIFF picker
// — leaves the live preview untouched so the audio never restarts mid-song.

#include "../../../libs/audio.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

class ProPreview {
public:
    // The chart's own audio, resolved from its tja on demand (the model caches
    // the parse; see ProListModel::song_audio).
    struct Clip {
        fs::path wave;
        float    demostart = 0.0f;   // seconds, the tja's DEMOSTART
        bool     ok = false;         // wave non-empty and on disk
    };

    ~ProPreview();

    // A chart row became selected. `song_path` is the dedupe key; `clip` its
    // audio. The same song as the current preview is a no-op (keeps playing).
    void select(const std::string& song_path, const Clip& clip);
    // Per-frame: starts playback once an off-thread .nus3bank decode lands.
    void update();
    // Hand the audio slot back: stop + unload "preview" and clear the key.
    // The caller decides whether the bgm returns (see the screen's helper).
    void release();

    bool playing() const { return playing_; }
    bool pending() const { return pending_; }
    const std::string& song() const { return song_; }

private:
    struct Load {
        std::atomic<bool>        done{false};
        bool                     ok = false;
        AudioEngine::PreparedPCM pcm;
    };

    void start_playback(float start_seconds);
    void stop_stream();

    std::string song_;                              // key of the loaded preview ("" = none)
    bool        playing_ = false;
    bool        pending_ = false;                   // an off-thread decode is in flight
    float       pending_demostart_ = 0.0f;          // the tja DEMOSTART for a pending bank
    std::shared_ptr<Load> load_;
    std::thread thread_;
};
