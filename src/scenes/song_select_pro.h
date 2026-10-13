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
        // The restore cursor has not been applied yet this visit; the snapshot it
        // reads from (`restore_`) deliberately SURVIVES, because this is the field
        // that carries the browse position across a visit.
        wait_restore_ = false;
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
            begin_initial_context();
            spdlog::info("[pro] library ready at entry; initial rows: {}", model->row_count());
        }
        spdlog::info("pro_select: entered (v2) - transient state reset (transition cleared, state BROWSING)");
    }

    Screens on_screen_end(Screens next_screen) override {
        if (search_box.has_value()) set_keyboard_visible(false);
        // Record where the user is leaving from, so the next entry can return to
        // it. Written for EVERY exit — play, practice, ESC out, the result screen,
        // even the main menu — because this is just the list's context + cursor,
        // never anything that drives the transition.
        remember_position();
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

        // The dan handoff (pro -> dojo), exactly the wheel's shape: the flash
        // plays once, then the screen leaves for DAN_SELECT. Nothing else runs
        // while it does.
        if (state_ == UiState::DAN_SELECTING && !dan_transition.has_value())
            return on_screen_end(Screens::DAN_SELECT);
        if (dan_transition.has_value()) {
            dan_transition->update(now);
            if (dan_transition->is_finished()) return on_screen_end(Screens::DAN_SELECT);
            return std::nullopt;
        }

        if (!library_ready_ && navigator.song_files_ready.load()) {
            library_ready_ = true;
            begin_initial_context();
            spdlog::info("[pro] library ready; initial rows: {}", model->row_count());
        }
        if (library_ready_) model->update();
        // The target context's rows are built in chunks, so the saved cursor can
        // only be re-established once the build has settled — and only then can it
        // be clamped to a list that may have changed size.
        if (wait_restore_ && library_ready_ && model->ready()) {
            apply_restore_cursor();
            wait_restore_ = false;
        }

        // TAB toggles the Filter/Sort dialog (the filter card row does the same).
        if (!game_transition.has_value() && check_key_pressed(ray::KEY_TAB)) {
            if (state_ == UiState::FILTER) {
                model->close_filter();
                state_ = UiState::BROWSING;
                spdlog::info("[pro] filter dialog closed (tab)");
            } else if (state_ == UiState::BROWSING && library_ready_ && !model->building()
                       && !model->dan_context()) {
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

        // The preview follows the song under the cursor (see sync_preview), so
        // hovering a chart row already started it; there is nothing to start
        // (or restart) on the don itself.
        preview_.update();          // lands an off-thread .nus3bank decode
        sync_preview();

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
        if (dan_transition.has_value()) dan_transition->draw();
    }

private:
    enum class UiState { BROWSING, SELECTED, PICKER, OPTIONS, FILTER, SEARCH, DAN_SELECTING };

    std::unique_ptr<ProListModel> model;
    ProPreview preview_;              // owns the "preview" stream, keyed on the song
    Screens game_target_ = Screens::GAME;
    bool library_ready_ = false;
    UiState state_ = UiState::BROWSING;
    double last_moved_ = -1e9;
    double search_guard_until_ = 0.0;

    // ---- return-to-position --------------------------------------------
    // The screen object outlives a visit (the screens table is built once), but
    // the list model does NOT — it is rebuilt in on_screen_start. This snapshot
    // is the browse position carried across a visit: WRITTEN in on_screen_end
    // (every exit), READ in on_screen_start through begin_initial_context().
    // It holds only list state (context, folder, filter/sort, the cursor's row
    // identity) and is never consulted by the transition code, so it cannot
    // re-arm the stale game_transition the entry reset clears.
    struct BrowseRestore {
        ProListModel::Context context = ProListModel::Context::ROOT;
        std::filesystem::path folder_path;              // FOLDER only
        std::string label;                              // context_label_ as shown
        int genre_index = 0;                            // context_genre_index_ as shown
        ProListModel::Filter filter;                    // filter in force
        ProListModel::Sort sort = ProListModel::Sort::ALPHA;   // sort in force
        std::string search_query;                       // SEARCH only
        // The cursor's row, by identity so it survives a rebuild that reorders or
        // resizes the list: a CHART row is (song_path, course); any other row is
        // (kind, name).
        bool anchor_chart = false;
        std::filesystem::path song_path;
        int course = -1;
        ProListModel::RowKind row_kind = ProListModel::RowKind::CHART;
        std::string row_name;
        int cursor = 0;                                 // last-resort fallback index
    };
    std::optional<BrowseRestore> restore_;   // SURVIVES a visit — never reset on entry
    bool wait_restore_ = false;              // per-visit: cursor not applied yet

    void remember_position() {
        if (!model) return;
        BrowseRestore r;
        r.context = model->context();
        r.folder_path = model->folder_path();
        r.label = model->context_label();
        r.genre_index = model->context_genre_index();
        r.filter = model->filter();
        r.sort = model->sort();
        r.search_query = model->search_query();
        r.cursor = model->cursor();
        if (model->row_count() > 0) {
            const ProListModel::Row& row = model->cursor_row();
            r.row_kind = row.kind;
            r.row_name = row.name;
            if (row.kind == ProListModel::RowKind::CHART) {
                r.anchor_chart = true;
                r.song_path = row.song_path;
                r.course = row.course;
            }
        }
        restore_ = std::move(r);
        spdlog::info("[pro] position remembered: ctx {} cursor {} anchor {}",
                     (int)restore_->context, restore_->cursor,
                     restore_->anchor_chart ? "chart" : "row");
    }

    // First context for a visit: the remembered one when there is one, else the
    // root. reset_root() keeps its meaning — it is what this falls back to, and
    // what a true reset still calls.
    void begin_initial_context() {
        if (!restore_.has_value() || restore_->context == ProListModel::Context::ROOT) {
            model->reset_root();
            spdlog::info("[pro] no position to restore; root");
            return;
        }
        const BrowseRestore& r = *restore_;
        // Apply filter/sort FIRST: while the context is still ROOT set_sort() does
        // not rebuild, so the target context opens with exactly one build.
        model->filter() = r.filter;
        model->set_sort(r.sort);
        if (r.context == ProListModel::Context::SEARCH) {
            if (r.search_query.empty()) { model->reset_root(); return; }
            model->open_search(r.search_query);
            wait_restore_ = true;
            spdlog::info("[pro] restore SEARCH '{}' (cursor pending build)", r.search_query);
            return;
        }
        if (!model->open_saved_context(r.context, r.folder_path, r.label, r.genre_index)) {
            // The remembered folder is gone (renamed / moved / deleted): land on
            // the root rather than a phantom context.
            spdlog::warn("[pro] saved folder '{}' is gone; falling back to root",
                         r.folder_path.string());
            model->reset_root();
            return;
        }
        wait_restore_ = true;
        spdlog::info("[pro] restore ctx {} '{}' (cursor pending build)", (int)r.context, r.label);
    }

    void apply_restore_cursor() {
        if (!restore_ || !model) return;
        const BrowseRestore& r = *restore_;
        const int n = model->row_count();
        if (n <= 0) return;
        int target = -1;
        for (int i = 0; i < n; ++i) {
            const ProListModel::Row& row = model->row_at(i);
            if (r.anchor_chart) {
                if (row.kind == ProListModel::RowKind::CHART && row.course == r.course &&
                    row.song_path == r.song_path) { target = i; break; }
            } else if (row.kind == r.row_kind && row.name == r.row_name) {
                target = i; break;
            }
        }
        if (target < 0) {
            // The exact row is gone (filter changed the row count, the chart left
            // the collection): clamp the remembered index into the rebuilt list.
            target = std::clamp(r.cursor, 0, n - 1);
            spdlog::info("[pro] restore anchor missing; clamped {} -> {} / {}", r.cursor, target, n);
        } else {
            spdlog::info("[pro] restore cursor -> {} / {} ({})", target, n,
                         r.anchor_chart ? "chart" : "row");
        }
        model->set_cursor(target);
    }

    static constexpr double kSkipWindowMs = 50.0;   // two kats inside 50 ms = ±10 skip

    // ---- view settings persistence ([pro] in config.toml) ---------------
    void load_pro_config() {
        const ProViewConfig& pc = global_data.config->pro;
        ProListModel::Filter f;
        f.diff = { pc.diff_easy, pc.diff_normal, pc.diff_hard, pc.diff_oni, pc.diff_ura };
        f.star = pc.star;
        f.tier = pc.tier;
        model->filter() = f;
        // A tier saved while STAR=ALL (or outside the level's own set) is not
        // offered any more: clamp it to ALL before the list builds.
        model->reconcile_tier();
        if (pc.sort == "SCORE")      model->set_sort(ProListModel::Sort::SCORE);
        else if (pc.sort == "CROWN") model->set_sort(ProListModel::Sort::CROWN);
        else if (pc.sort == "LEVEL") model->set_sort(ProListModel::Sort::LEVEL);
        else if (pc.sort == "TIER")  model->set_sort(ProListModel::Sort::LEVEL);  // removed sort -> LEVEL
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
            case ProListModel::Sort::LEVEL: pc.sort = "LEVEL"; break;
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
                const bool confirm = model->advance_filter_value();
                save_pro_config();
                spdlog::info("[pro] filter focus {} entry {} value {} (star {}, tier '{}', sort {})",
                             model->filter_focus_name(), model->filter_cursor(),
                             model->filter_value_cursor(), model->filter().star,
                             model->filter().tier, model->sort_name());
                if (confirm) {
                    model->close_filter();
                    state_ = UiState::BROWSING;
                    spdlog::info("[pro] filter dialog confirmed -> browsing");
                }
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
            case ProListModel::RowKind::DAN: {
                // A dan course: hand off to the dojo, focused on this course,
                // and have the dojo's own exit land back here.
                audio.play_sound("don", VolumePreset::SOUND);
                SessionData& sd = global_data.session_data[(int)global_data.player_num];
                sd.origin_screen  = (int)Screens::PRO_SELECT;
                sd.dan_focus_path = r.dan ? r.dan->json_path.string() : "";
                spdlog::info("[pro] dan selected: '{}' -> dojo (focused)", r.name);
                state_ = UiState::DAN_SELECTING;
                // The curtain is the wheel's own rig (ROUND 17: anim 38 drives
                // it; dan_doors.lua draws the shutters while the engine's quad
                // stays invisible in this skin). Run it only when the skin
                // ships one — the dojo's guard pattern — and never let a
                // partial animation set take the screen down.
                if (tex.has_texture("dan_transition/background")) {
                    dan_transition.emplace();
                    try {
                        dan_transition->start();
                    } catch (const std::exception& e) {
                        spdlog::warn("[pro] dan curtain skipped: {}", e.what());
                        dan_transition.reset();
                    }
                }
                break;
            }
            case ProListModel::RowKind::CHART:
                audio.play_sound("don", VolumePreset::SOUND);
                spdlog::info("[pro] chart selected: {} [{}/{}]", r.title, r.course, r.level);
                model->enter_selected();
                state_ = UiState::SELECTED;
                // The preview is already running: the cursor has rested on this
                // chart row since it arrived, so sync_preview started it. A
                // repeat don on the same row (or the CHANGE DIFF picker) leaves
                // the song key unchanged, so nothing restarts.
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
    // The trigger is the CURSOR, not a selection: whatever chart row the cursor
    // rests on is the song being previewed, exactly as the wheel previews its
    // focused box. That covers every state that leaves the cursor on a chart
    // row -- BROWSING, the submenu (SELECTED), the CHANGE DIFF picker, and the
    // Play Options panel -- with no per-state plumbing.
    //
    // ProPreview::hover is keyed on the song, so crossing to another course of
    // the SAME song (an adjacent Oni/Ura row, or the picker) is a no-op and the
    // audio keeps playing uninterrupted. Leaving that song for a different
    // chart, or moving to a NON-chart row (BACK / FILTER / SEARCH / a folder /
    // a collection), gives the slot back to the bgm: a preview must never
    // outlive the cursor, play over the game or over another screen.
    void sync_preview() {
        // A PLAY / PRACTICE commit already released the slot and handed audio to
        // the game (see commit_and_play). Do not re-start a preview under the
        // loading transition or the song itself -- the cursor is still sitting
        // on that chart row, so without this guard hover would immediately take
        // the slot back.
        if (game_transition.has_value()) return;

        const bool have_row = library_ready_ && model && !model->building() && model->row_count() > 0;
        const ProListModel::Row* row = have_row ? &model->cursor_row() : nullptr;

        if (!row || row->kind != ProListModel::RowKind::CHART) {
            if (!preview_.song().empty()) {
                preview_.release();
                resume_bgm();
            }
            return;
        }

        if (preview_.song() != row->song_path.string()) {
            const ProListModel::SongAudio au = model->song_audio(row->song_path);
            preview_.hover(row->song_path.string(),
                           ProPreview::Clip{au.wave, au.demostart, au.ok});
        }
        // A chart with no playable audio (or a failed bank decode) leaves the
        // slot idle: hand the bgm back rather than holding a silent stream.
        if (!preview_.playing() && !preview_.pending()) resume_bgm();
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
