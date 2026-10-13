#include "pro_preview.h"

#include <spdlog/spdlog.h>

#include <utility>

namespace {
bool is_bank(const fs::path& wave) {
    const std::string ext = wave.extension().string();
    return ext == ".nus3bank" || ext == ".nub";
}
}  // namespace

ProPreview::~ProPreview() {
    stop_stream();
    pending_ = false;
    if (thread_.joinable()) thread_.join();
    load_.reset();
}

void ProPreview::start_playback(float start_seconds) {
    playing_ = true;
    audio.stop_sound("bgm");                                  // the preview takes the slot
    // Loop at the mixer: the wrap happens inside the audio callback (no
    // main-thread polling, no gap) and returns to the preview point, so the
    // preview section repeats instead of the file's intro. Must be set while
    // the stream is loaded and before play_music_stream().
    audio.set_music_loop("preview", true, start_seconds);
    audio.play_music_stream("preview", VolumePreset::MUSIC);
    audio.seek_music_stream("preview", start_seconds);
}

void ProPreview::stop_stream() {
    if (audio.is_music_stream_valid("preview")) {
        audio.stop_music_stream("preview");
        audio.unload_music_stream("preview");
    }
    playing_ = false;
}

void ProPreview::hover(const std::string& song_path, const Clip& clip) {
    // Keyed on the SONG alone: an adjacent Oni/Ura row, the same row re-entered
    // from the submenu, and the CHANGE DIFF picker all carry the same song_path,
    // so the live preview is left untouched and never restarts. A song that has
    // already been attempted is left alone too -- hovering it again every frame
    // must not re-decode a bank or spam the log for one that has no preview.
    if (song_ == song_path) return;

    stop_stream();
    if (thread_.joinable()) thread_.join();   // a superseded bank decode ends here
    load_.reset();
    pending_ = false;
    song_ = song_path;

    if (!clip.ok || clip.wave.empty()) {
        spdlog::info("[pro] preview: no audio for '{}'", song_path);
        return;   // the caller restores the bgm
    }

    if (is_bank(clip.wave)) {
        // A .nus3bank is decoded off the audio thread exactly as box_song does it;
        // update() starts playback when the decode lands.
        pending_demostart_ = clip.demostart;
        load_ = std::make_shared<Load>();
        thread_ = std::thread([state = load_, wave = clip.wave] {
            state->ok = audio.prepare_nus3bank_pcm(wave, state->pcm, true);
            state->done.store(true, std::memory_order_release);
        });
        pending_ = true;
        spdlog::info("[pro] preview start: '{}' wave='{}' kind=bank",
                     song_path, clip.wave.filename().string());
    } else {
        audio.load_music_stream(clip.wave, "preview");
        if (audio.is_music_stream_valid("preview")) {
            start_playback(clip.demostart);
            spdlog::info("[pro] preview start: '{}' wave='{}' kind=stream demostart={:.2f}",
                         song_path, clip.wave.filename().string(), clip.demostart);
        } else {
            spdlog::warn("[pro] preview: could not open '{}'", clip.wave.string());
        }
    }
}

void ProPreview::update() {
    if (!pending_ || !load_) return;
    if (!load_->done.load(std::memory_order_acquire)) return;

    auto state = load_;
    if (thread_.joinable()) thread_.join();
    load_.reset();
    pending_ = false;

    if (!state->ok) {
        spdlog::warn("[pro] preview: bank decode failed for '{}'", song_);
        return;
    }
    // A bank carries its own preview point; fall back to the tja's DEMOSTART.
    const float start = state->pcm.preview_ms > 0
                      ? state->pcm.preview_ms / 1000.0f
                      : pending_demostart_;
    audio.load_music_stream_prepared(std::move(state->pcm), "preview");
    if (audio.is_music_stream_valid("preview")) {
        start_playback(start);
        spdlog::info("[pro] preview start: '{}' kind=bank demostart={:.2f}", song_, start);
    }
}

void ProPreview::release() {
    const bool had = playing_ || pending_ || !song_.empty();
    stop_stream();
    if (thread_.joinable()) thread_.join();
    load_.reset();
    pending_ = false;
    song_.clear();
    if (had) spdlog::info("[pro] preview slot released");
}
