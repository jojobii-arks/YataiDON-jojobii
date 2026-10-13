#pragma once
#include "song_select.h"
#include "../libs/config.h"
#include "../libs/input.h"
#include "../libs/audio.h"
#include "../objects/song_select/file_navigator/box_song.h"
#include "../objects/song_select/pro/pro_list.h"
#include "../objects/song_select/pro/pro_preview.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <string>

// Pro select v2 — a real screen with its own main-menu entry (see the vault
// handoff `Handoff - YataiDON Pro Select v2.md`). This screen owns the list
// model and the interaction states; the skin draws it through
// SongSelectScript::draw_pro. It subclasses SongSelectScreen for the inherited
// asset set only (`screen_name` stays "song_select"), and never runs the
// wheel: no navigator.init, no box plan.
class ProSongSelectScreen : public SongSelectScreen {
public:
    ProSongSelectScreen() : SongSelectScreen("song_select") {}

    Screens play_origin() override { return Screens::PRO_SELECT; }
    Screens get_game_screen_target() override { return game_target_; }

    void on_screen_start() override {
        Screen::on_screen_start();
        // This screen object outlives a visit (the screens table is built once at
        // startup), so every field that tracks a single visit must be reset here
        // -- the wheel does the same in SongSelectScreen::on_screen_start. Without
        // this, a Transition left over from the previous play is still in
        // game_transition and already finished, so the first update() after
        // re-entry fired on_screen_end(get_game_screen_target()) immediately:
        // re-entering pro select from the main menu jumped straight back into the
        // selected song (or, after ESC in practice, straight back into practice
        // from bar 1, which read as "practice just reloaded the scene").
        if (search_box.has_value()) set_keyboard_visible(false);
        search_box.reset();
        game_transition.reset();
        dan_transition.reset();
        game_target_ = Screens::GAME;
        state_ = UiState::BROWSING;
        last_moved_ = -1e9;
        search_guard_until_ = 0.0;
        clear_input_buffers();
        // and the preview's own slot, so a re-entry can never inherit a stream
        // that the previous visit left playing
        preview_.release();
        SongBox::reset_bgm_slot();
        audio.play_sound("bgm", VolumePreset::MUSIC);
        script = std::make_unique<SongSelectScript>();
        // The engine's own player object. It owns Don-chan (a `chara` built from
        // the saved player data) and the nameplate card, and it draws the option
        // panel itself. Pro borrows the engine's one rather than reimplementing
        // any of it, so the character and the card land on the same skin_config
        // stations the wheel uses (song_select_chara_1p / _nameplate_1p).
        player = std::make_unique<SongSelectPlayer>(global_data.player_num);
        player->script = script.get();
        // The pro layout runs a panel down to y 730; Don-chan at full size reaches
        // up to y 684 and would sit over it. 0.8 brings his visible top to ~732.
        player->chara_scale = 0.8f;
        // The boot loading screen preloads the navigator's library scan; this
        // call early-returns when that already ran. Pro reads the library
        // through the seam either way — it never builds the wheel.
        navigator.preload(global_data.config->paths.tja_path);
        model = std::make_unique<ProListModel>(&navigator);
        model->set_hide_dan(hides_dan());
        load_pro_config();
        if (navigator.song_files_ready.load()) {
            library_ready_ = true;
            model->reset_root();
            spdlog::info("[pro] library ready at entry; root rows: {}", model->row_count());
        }
        spdlog::info("pro_select: entered (v2) - transient state reset (transition cleared, state BROWSING)");
    }

    Screens on_screen_end(Screens next_screen) override {
        if (search_box.has_value()) set_keyboard_visible(false);
        // The "preview" stream is ours while it plays: stop it before the base
        // unloads all audio, and never leave another screen a song playing.
        preview_.release();
        spdlog::info("pro_select: leaving to {}", next_screen);
        return Screen::on_screen_end(next_screen);
    }

    std::optional<Screens> update() override {
        Screen::update();
        const double now = get_current_ms();
        if (player) player->update(now);   // Don-chan + nameplate animation

        if (!library_ready_ && navigator.song_files_ready.load()) {
            library_ready_ = true;
            model->reset_root();
            spdlog::info("[pro] library ready; root rows: {}", model->row_count());
        }
        if (library_ready_) model->update();

        // TAB toggles the Filter/Sort dialog (the filter card row does the same).
        if (!game_transition.has_value() && check_key_pressed(ray::KEY_TAB)) {
            if (state_ == UiState::FILTER) {
                model->close_filter();
                state_ = UiState::BROWSING;
                spdlog::info("[pro] filter dialog closed (tab)");
            } else if (state_ == UiState::BROWSING && library_ready_ && !model->building()) {
                model->open_filter();
                state_ = UiState::FILTER;
                spdlog::info("[pro] filter dialog opened (tab)");
            }
        }

        if (check_key_pressed(global_data.config->keys.back_key)) {
            if (state_ == UiState::PICKER) {
                model->cancel_picker();
                state_ = UiState::SELECTED;
            } else if (state_ == UiState::FILTER) {
                model->close_filter();
                state_ = UiState::BROWSING;
            } else if (state_ == UiState::OPTIONS) {
                close_options();
            } else if (state_ == UiState::SEARCH) {
                close_search();
            } else if (state_ == UiState::SELECTED) {
                model->leave_selected();
                state_ = UiState::BROWSING;
            } else if (model->in_folder()) {
                spdlog::info("[pro] back to root");
                model->back_to_root();
            } else {
                return on_screen_end(Screens::ENTRY);
            }
        }

        if (state_ == UiState::OPTIONS) update_options(now);
        if (state_ == UiState::SEARCH) update_search(now);

        if (library_ready_) handle_input(now);

        // The preview follows the song, not the row (see sync_preview_scope).
        preview_.update();          // lands an off-thread .nus3bank decode
        sync_preview_scope();

        if (game_transition.has_value()) {
            game_transition->update(now);
            if (game_transition->is_finished()) {
                return on_screen_end(get_game_screen_target());
            }
        }
        return std::nullopt;
    }

    void draw() override {
        bool drawn = false;
        if (script && library_ready_) drawn = script->draw_pro(model.get());
        if (!drawn) draw_fallback();
        // Don-chan + the player card, and the Play Options panel when it is open
        // (SongSelectPlayer::draw calls the skin's draw_option_panel itself).
        // The player goes last because song_select's own backdrop is a full-screen
        // texture: drawn first, the character is simply painted over.
        if (player) player->draw(state, false, 1.0f);
        if (search_box.has_value()) search_box->draw();
        if (game_transition.has_value()) game_transition->draw();
    }

private:
    enum class UiState { BROWSING, SELECTED, PICKER, OPTIONS, FILTER, SEARCH };

    std::unique_ptr<ProListModel> model;
    ProPreview preview_;              // owns the "preview" stream, keyed on the song
    Screens game_target_ = Screens::GAME;
    bool library_ready_ = false;
    UiState state_ = UiState::BROWSING;
    double last_moved_ = -1e9;
    double search_guard_until_ = 0.0;

    static constexpr double kSkipWindowMs = 50.0;   // two kats inside 50 ms = ±10 skip

    // ---- view settings persistence ([pro] in config.toml) ---------------
    void load_pro_config() {
        const ProViewConfig& pc = global_data.config->pro;
        ProListModel::Filter f;
        f.diff = { pc.diff_easy, pc.diff_normal, pc.diff_hard, pc.diff_oni, pc.diff_ura };
        f.star = pc.star;
        f.tier = pc.tier;
        model->filter() = f;
        if (pc.sort == "SCORE")      model->set_sort(ProListModel::Sort::SCORE);
        else if (pc.sort == "CROWN") model->set_sort(ProListModel::Sort::CROWN);
        else if (pc.sort == "TIER")  model->set_sort(ProListModel::Sort::TIER);
        else                         model->set_sort(ProListModel::Sort::ALPHA);
    }

    void save_pro_config() {
        ProViewConfig& pc = global_data.config->pro;
        const ProListModel::Filter& f = model->filter();
        pc.diff_easy = f.diff[0];
        pc.diff_normal = f.diff[1];
        pc.diff_hard = f.diff[2];
        pc.diff_oni = f.diff[3];
        pc.diff_ura = f.diff[4];
        pc.star = f.star;
        pc.tier = f.tier;
        switch (model->sort()) {
            case ProListModel::Sort::SCORE: pc.sort = "SCORE"; break;
            case ProListModel::Sort::CROWN: pc.sort = "CROWN"; break;
            case ProListModel::Sort::TIER:  pc.sort = "TIER";  break;
            case ProListModel::Sort::ALPHA: pc.sort = "ALPHA"; break;
        }
        save_config(*global_data.config);
    }

    void handle_input(double now) {
        if (!model || model->building() || game_transition.has_value()) {
            clear_input_buffers();
            return;
        }
        if (state_ == UiState::SEARCH) return;   // the search box owns the keyboard

        const bool l_kat = is_l_kat_pressed(global_data.player_num) || ray::IsKeyPressed(ray::KEY_LEFT);
        const bool r_kat = is_r_kat_pressed(global_data.player_num) || ray::IsKeyPressed(ray::KEY_RIGHT);
        const bool don = is_l_don_pressed(global_data.player_num) ||
                         is_r_don_pressed(global_data.player_num) ||
                         ray::IsKeyPressed(ray::KEY_ENTER);

        if (state_ == UiState::OPTIONS) {
            if (player && player->modifier_selector.has_value()) {
                if (l_kat) { audio.play_sound("kat", VolumePreset::SOUND); player->modifier_selector->left(); }
                if (r_kat) { audio.play_sound("kat", VolumePreset::SOUND); player->modifier_selector->right(); }
                if (don)   { audio.play_sound("don", VolumePreset::SOUND); player->modifier_selector->confirm(); }
            }
            return;
        }

        if (state_ == UiState::FILTER) {
            if (l_kat) { model->move_filter_cursor(-1); audio.play_sound("kat", VolumePreset::SOUND); }
            if (r_kat) { model->move_filter_cursor(+1); audio.play_sound("kat", VolumePreset::SOUND); }
            if (don) {
                audio.play_sound("don", VolumePreset::SOUND);
                model->advance_filter_value();
                save_pro_config();
                spdlog::info("[pro] filter row {} advance (star {}, tier '{}', sort {})",
                             model->filter_cursor(), model->filter().star, model->filter().tier,
                             model->sort_name());
            }
            return;
        }

        if (state_ == UiState::PICKER) {
            if (l_kat) { model->move_picker(-1); audio.play_sound("kat", VolumePreset::SOUND); }
            if (r_kat) { model->move_picker(+1); audio.play_sound("kat", VolumePreset::SOUND); }
            if (don) {
                audio.play_sound("don", VolumePreset::SOUND);
                spdlog::info("[pro] change diff -> course {}", model->picker_cursor());
                model->commit_picker();
                state_ = UiState::SELECTED;
            }
            return;
        }

        if (state_ == UiState::SELECTED) {
            if (l_kat && !r_kat) { model->move_control(-1); audio.play_sound("kat", VolumePreset::SOUND); }
            else if (r_kat && !l_kat) { model->move_control(+1); audio.play_sound("kat", VolumePreset::SOUND); }
            if (don) activate_control();
            return;
        }

        // BROWSING — the wheel's own semantics: one kat = one row, two kats
        // inside 50 ms = a ±10 skip (player.cpp:227/:239 on last_moved + 50).
        bool navigated = false;
        if (ray::IsKeyPressed(ray::KEY_LEFT_CONTROL) || (l_kat && now <= last_moved_ + kSkipWindowMs)) {
            audio.play_sound("skip", VolumePreset::SOUND);
            model->skip_cursor(-1);
            last_moved_ = now;
            navigated = true;
            spdlog::info("[pro] skip -10 -> cursor {} / {}", model->cursor(), model->row_count());
        } else if (l_kat) {
            audio.play_sound("kat", VolumePreset::SOUND);
            model->move_cursor(-1);
            last_moved_ = now;
            navigated = true;
            log_cursor("left");
        }
        if (ray::IsKeyPressed(ray::KEY_RIGHT_CONTROL) || (r_kat && now <= last_moved_ + kSkipWindowMs)) {
            audio.play_sound("skip", VolumePreset::SOUND);
            model->skip_cursor(+1);
            last_moved_ = now;
            navigated = true;
            spdlog::info("[pro] skip +10 -> cursor {} / {}", model->cursor(), model->row_count());
        } else if (r_kat) {
            audio.play_sound("kat", VolumePreset::SOUND);
            model->move_cursor(+1);
            last_moved_ = now;
            navigated = true;
            log_cursor("right");
        }
        if (!navigated && don) activate_row();
    }

    void log_cursor(const char* dir) {
        if (!model || model->row_count() == 0) return;
        const ProListModel::Row& r = model->cursor_row();
        spdlog::info("[pro] {} -> cursor {} / {} ({})", dir, model->cursor(), model->row_count(),
                     r.kind == ProListModel::RowKind::CHART ? r.title : r.name);
    }

    void activate_row() {
        if (!model || model->row_count() == 0) return;
        const ProListModel::Row& r = model->cursor_row();
        switch (r.kind) {
            case ProListModel::RowKind::BACK:
                audio.play_sound("don", VolumePreset::SOUND);
                spdlog::info("[pro] BACK row -> root");
                model->back_to_root();
                break;
            case ProListModel::RowKind::FILTER:
                audio.play_sound("don", VolumePreset::SOUND);
                model->open_filter();
                state_ = UiState::FILTER;
                spdlog::info("[pro] filter dialog opened (row)");
                break;
            case ProListModel::RowKind::SEARCH:
                audio.play_sound("don", VolumePreset::SOUND);
                start_search();
                break;
            case ProListModel::RowKind::CHART:
                audio.play_sound("don", VolumePreset::SOUND);
                spdlog::info("[pro] chart selected: {} [{}/{}]", r.title, r.course, r.level);
                model->enter_selected();
                state_ = UiState::SELECTED;
                // Keyed on the song: a repeat selection of this song is a no-op
                // inside ProPreview::select, so the audio keeps playing.
                {
                    const ProListModel::SongAudio au = model->song_audio(r.song_path);
                    preview_.select(r.song_path.string(),
                                    ProPreview::Clip{au.wave, au.demostart, au.ok});
                }
                break;
            default:
                audio.play_sound("don", VolumePreset::SOUND);
                spdlog::info("[pro] open folder '{}' ({} charts)", r.name, r.chart_count);
                model->open_folder_row(model->cursor());
                break;
        }
    }

    void activate_control() {
        switch (model->control_index()) {
            case 0:
                audio.play_sound("don", VolumePreset::SOUND);
                spdlog::info("[pro] control BACK");
                model->leave_selected();
                state_ = UiState::BROWSING;
                break;
            case 1:
                audio.play_sound("don", VolumePreset::SOUND);
                open_options();
                break;
            case 2:
                start_game();
                break;
            case 3:
                audio.play_sound("don", VolumePreset::SOUND);
                start_practice();
                break;
            case 4:
                audio.play_sound("don", VolumePreset::SOUND);
                spdlog::info("[pro] control CHANGE DIFF");
                model->open_picker();
                state_ = UiState::PICKER;
                break;
            default:
                break;
        }
    }

    // ---- preview scope --------------------------------------------------
    // The preview is keyed to the SONG: ProPreview::select ignores a repeat of
    // the same song, which covers the CHANGE DIFF picker on its own. An adjacent
    // Oni/Ura row is reached by backing out of the selection and moving the
    // cursor, so the preview is kept alive while the cursor still rests on a row
    // of the previewed song, and the slot goes back to the bgm the moment the
    // cursor leaves that song (or a chart turns out to have no playable audio).
    void sync_preview_scope() {
        if (preview_.song().empty()) return;   // we hold no preview slot
        const bool live = preview_.playing() || preview_.pending();
        bool same_song = false;
        if (model && model->row_count() > 0) {
            const ProListModel::Row& r = model->cursor_row();
            same_song = r.kind == ProListModel::RowKind::CHART &&
                        r.song_path.string() == preview_.song();
        }
        if (live && same_song) return;         // the preview keeps the slot
        preview_.release();
        resume_bgm();
    }

    static void resume_bgm() {
        if (!audio.is_sound_playing("bgm")) audio.play_sound("bgm", VolumePreset::MUSIC);
    }

    // ---- play / practice commits ---------------------------------------
    void start_game() { commit_and_play(Screens::GAME); }
    void start_practice() { commit_and_play(Screens::GAME_PRACTICE); }

    void commit_and_play(Screens target) {
        if (!model || model->row_count() == 0) return;
        const ProListModel::Row& row = model->cursor_row();
        if (row.kind != ProListModel::RowKind::CHART) return;
        const int course = model->active_course();
        if (course < 0 || course > 4) return;

        SessionData& sd = global_data.session_data[(int)global_data.player_num];
        sd.selected_song = row.song_path;
        sd.selected_difficulty = course;
        std::array<std::string, 5> hashes = scores_manager.get_hashes(row.song_path);
        sd.song_hash = hashes[course];
        sd.song_title = row.title;
        sd.song_subtitle = row.subtitle;
        int genre_index = 0;
        std::string genre_label;
        if (model->song_genre(row.song_path, genre_index, genre_label)) {
            sd.genre_index = genre_index - 1;   // same offset the wheel stores
            sd.genre_label = genre_label;
        }
        sd.origin_screen = (int)play_origin();   // returns-after-song
        global_data.last_difficulty[(int)global_data.player_num] = course;

        spdlog::info("[pro] {} {} [L{}] course {} hash {}",
                     target == Screens::GAME_PRACTICE ? "PRACTICE" : "PLAY",
                     row.title, row.level, course, sd.song_hash.substr(0, 8));

        audio.play_sound("don", VolumePreset::SOUND);
        model->leave_selected();
        state_ = UiState::BROWSING;
        // The game takes over the audio: stop the preview now, so it never plays
        // under the loading transition (or over the song itself).
        preview_.release();
        game_target_ = target;
        game_transition.emplace(row.title, row.subtitle, false);
        if (fs::exists(row.song_path.parent_path() / "Loading.png")) {
            game_transition->add_loading_graphic((row.song_path.parent_path() / "Loading.png").string());
        }
        game_transition->start();
    }

    // ---- Play Options (the skin's modifier panel) -----------------------
    void open_options() {
        if (!player) return;
        player->modifier_selector.emplace(global_data.player_num, &player->player_data);
        state_ = UiState::OPTIONS;
        spdlog::info("[pro] play options opened");
    }

    void close_options() {
        if (player && player->modifier_selector.has_value()) {
            scores_manager.save_player_data(player->player_data);
            player->modifier_selector.reset();
        }
        state_ = UiState::SELECTED;
        spdlog::info("[pro] play options closed");
    }

    void update_options(double now) {
        if (!player || !player->modifier_selector.has_value()) {
            state_ = UiState::SELECTED;
            return;
        }
        player->modifier_selector->update(now);
        if (player->modifier_selector->is_finished) close_options();
    }

    // ---- search ---------------------------------------------------------
    void start_search() {
        search_box.emplace();
        while (ray::GetCharPressed() > 0) {}
        set_keyboard_visible(true);
        search_guard_until_ = get_current_ms() + 250.0;
        state_ = UiState::SEARCH;
        spdlog::info("[pro] search opened");
    }

    void close_search() {
        if (search_box.has_value()) {
            set_keyboard_visible(false);
            search_box.reset();
        }
        state_ = UiState::BROWSING;
    }

    void update_search(double now) {
        if (!search_box.has_value()) {
            state_ = UiState::BROWSING;
            return;
        }
        search_box->update(now);
        const TextEditAction act = poll_text_edit(search_box->current_search);
        if (act == TextEditAction::Cancel) {
            close_search();
            return;
        }
        bool confirm = (act == TextEditAction::Confirm);
        if (!confirm && now >= search_guard_until_ &&
            (is_l_don_pressed(global_data.player_num) || is_r_don_pressed(global_data.player_num))) {
            confirm = true;   // the pad's don doubles as the confirm
        }
        if (confirm) {
            const std::string query = search_box->current_search;
            close_search();
            const bool blank = query.find_first_not_of(" \t\r\n") == std::string::npos;
            if (!blank) {
                model->open_search(query);
                spdlog::info("[pro] search -> '{}'", query);
            }
        }
    }

    void draw_fallback() {
        ray::DrawRectangle(0, 0, tex.screen_width, tex.screen_height, ray::Color{16, 18, 26, 255});
        const char* title = "PRO SELECT";
        const int size = 64;
        ray::DrawText(title, tex.screen_width / 2 - ray::MeasureText(title, size) / 2,
                      tex.screen_height / 2 - 120, size, ray::Color{238, 238, 244, 255});
        if (!library_ready_) {
            const char* sub = "waiting for the library scan...";
            ray::DrawText(sub, 80, 140, 28, ray::Color{200, 200, 210, 255});
            return;
        }
        if (model->building()) {
            char buf[128];
            snprintf(buf, sizeof(buf), "building list... %.0f%%", model->build_progress() * 100.0f);
            ray::DrawText(buf, 80, 140, 28, ray::Color{200, 200, 210, 255});
            return;
        }
        static const char* ctx_names[] = {"root", "folder", "recent", "favorite", "all", "search"};
        static const char* kind_names[] = {"FOLDER", "SEARCH", "RECENT", "FAVORITE", "ALL",
                                           "CHART", "BACK", "FILTER"};
        char buf[256];
        snprintf(buf, sizeof(buf), "context: %s   cursor: %d / %d   charts: %d",
                 ctx_names[(int)model->context()], model->cursor(), model->row_count(),
                 model->chart_row_count());
        ray::DrawText(buf, 80, 140, 26, ray::Color{200, 200, 210, 255});
        snprintf(buf, sizeof(buf), "sort: %s   filter: star %d tier '%s'", model->sort_name(),
                 model->filter().star, model->filter().tier.c_str());
        ray::DrawText(buf, 80, 176, 26, ray::Color{200, 200, 210, 255});
        if (model->row_count() > 0) {
            const ProListModel::Row& r = model->cursor_row();
            snprintf(buf, sizeof(buf), "row[%d] kind=%s name='%s' title='%s' course=%d level=%d",
                     model->cursor(), kind_names[(int)r.kind], r.name.c_str(), r.title.c_str(),
                     r.course, r.level);
            ray::DrawText(buf, 80, 220, 26, ray::Color{255, 210, 74, 255});
        }
        const int window = 14;
        const int n = model->row_count();
        for (int i = 0; i < window && i < n; ++i) {
            int idx = model->cursor() + (i - window / 2);
            idx = ((idx % n) + n) % n;
            const ProListModel::Row& r = model->row_at(idx);
            char row_buf[256];
            if (r.kind == ProListModel::RowKind::CHART) {
                snprintf(row_buf, sizeof(row_buf), "%c %-8s %-40s c%d L%d",
                         idx == model->cursor() ? '>' : ' ', kind_names[(int)r.kind],
                         r.title.c_str(), r.course, r.level);
            } else {
                snprintf(row_buf, sizeof(row_buf), "%c %-8s %s",
                         idx == model->cursor() ? '>' : ' ', kind_names[(int)r.kind],
                         r.name.c_str());
            }
            ray::DrawText(row_buf, 80, 280 + i * 30, 24,
                          idx == model->cursor() ? ray::Color{255, 210, 74, 255}
                                                 : ray::Color{210, 210, 220, 255});
        }
    }
};
