#pragma once

#include "../libs/screen.h"
#include "../libs/scores.h"
#include "../libs/network.h"
#include "../objects/entry/box_manager.h"
#include "../objects/entry/entry_script.h"
#include "../objects/entry/player.h"
#include "../objects/global/nameplate.h"
#include "../objects/global/coin_overlay.h"
#include "../objects/global/allnet_indicator.h"
#include "../objects/global/entry_overlay.h"
#include "../objects/global/timer.h"
#include "../objects/global/chara_3d.h"

#ifdef CARD_READER_ENABLED
#include "../libs/optional/card_reader.h"
#endif

#include <functional>
#include <future>


enum class EntryState {
    SELECT_SIDE = 0,
    SELECT_MODE = 1,
    SELECT_COSTUME = 2,
    WAITING = 3  // online, auto_login off: side select hidden until Enter (online) or Don (local)
};

// The network half of an online login, done on a worker thread so the entry screen keeps running:
// registration, the server's profile, and the scores to sync. It touches neither the config nor
// scores.db; EntryScreen::finish_login applies it on the main thread.
struct RemoteLogin {
    bool ok = false;           // false: a card could not be registered
    std::string code;          // access code to log in with
    std::string replaced_code; // set when a placeholder code was registered and replaced by `code`
    bool card = false;         // `code` was registered for a scanned card
    std::optional<RemoteUser> user;
    std::vector<RemoteScore> scores;
};

class EntryScreen : public Screen {
private:
    int side = 1;
    bool is_2p = false;
    std::unique_ptr<BoxManager> box_manager;
    EntryState state = EntryState::SELECT_SIDE;

    std::unique_ptr<EntryScript> lua_entry;
    Nameplate nameplate;
    CoinOverlay coin_overlay;
    AllNetIcon allnet_indicator;
    EntryOverlay entry_overlay;
    std::unique_ptr<Timer> timer;

    std::unique_ptr<Chara3D> chara;
    bool announce_played = false;
    std::vector<std::unique_ptr<EntryPlayer>> players;

#ifdef CARD_READER_ENABLED
    std::unique_ptr<card_reader::CardReader> card_reader_;
    double last_card_poll_ms_ = 0;
    bool card_reader_initialized_ = false;
#endif
    std::vector<std::string> used_cards_;  // held card must not log in twice
    std::string pending_card_hex_;         // scanned on title, consumed on first update
    bool local_login_ = false;   // Don pressed: scores.db only, never register
    bool login_ready_ = false;   // next join_player already logged in (Enter / auto login)

    void reload_preview_chara(PlayerNum player_num);
    bool arcade_credit() const;
    bool seat_joined(PlayerNum player_num) const;
    void join_player(PlayerNum player_num, bool do_login = true);
    void start_second_player_join();
    void draw_background();
    void draw_side_select(float fade);
    void draw_player_drum();
    void draw_mode_select();
    bool mode_select_ready();
    std::optional<Screens> handle_input();

    std::future<RemoteLogin> pending_login_;
    int pending_login_slot_ = 0;
    std::function<void()> after_login_;

    bool login_slot(const std::string& card_hex, std::function<void()> then);
    void finish_login(RemoteLogin remote);
    void finish_slot(int slot, PlayerData pd);
    bool login_pending() const { return pending_login_.valid(); }
    void join_with_card(const std::string& card_hex);
    void on_card_login();

public:
    EntryScreen() : Screen("entry") {
    }
    void on_screen_start() override;
    Screens on_screen_end(Screens next_screen) override;
    std::optional<Screens> update() override;
    void draw() override;
};
