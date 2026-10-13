#pragma once

// Pro select v2 — the song preview.
//
// The wheel plays a preview per focused SongBox through the music stream named
// "preview" (see box_song.cpp). Pro deliberately builds no SongBoxes, so this
// owns that same "preview" stream directly, with the same primitives
// (load_music_stream / load_music_stream_prepared + the off-thread .nus3bank
// decode) rather than inventing a parallel mechanism.
//
// The dedupe key is the SONG (the tja path), not the row: moving between two
// courses of the same song — an adjacent Oni/Ura row, the same row re-entered
// from the submenu, or the CHANGE DIFF picker — leaves the live preview
// untouched so the audio never restarts mid-song.
//
// The trigger is the CURSOR, not a selection: whatever chart row the cursor
// rests on is the song being previewed, exactly as the wheel previews its
// focused box. The preview also loops. The audio engine had no public music
// loop call, so AudioEngine::set_music_loop was added (see audio.h): the wrap
// happens inside the mixer's refill, so it is gapless, and it returns to the
// stream's own preview point (the tja DEMOSTART / the bank's point) rather
// than the file intro.

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

    // The cursor came to rest on this chart row. `song_path` is the dedupe key;
    // `clip` its audio. The same song as the current preview is a no-op (keeps
    // playing), and a song already given up on is left alone rather than retried
    // every frame.
    void hover(const std::string& song_path, const Clip& clip);
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
