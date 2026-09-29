#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

// Community clear-tier list from taiko.wiki (★6..★10), loaded once from a
// committed JSON artifact. Never hits the network at runtime.
//
// A tier is per (song, difficulty, star-level): taiko.wiki ranks each star level
// with its own tier-letter scale (L6: S/A/C/E/F ... L10: SSS/SS/S+/S/A/B/C/D/E/F/X),
// so the same song's Oni at ★7 vs ★10 carries different tiers.
struct TierEntry {
    std::string romaji;
    std::string difficulty;  // "oni" | "ura"
    std::string tier;        // "SSS", "SS", "S+", ...
    int order = 0;
    int level = 0;           // 6..10
};

// Tier for one (song, difficulty) at one star level.
struct TierLookup {
    std::string oni;
    std::string ura;
    int oni_order = 0;
    int ura_order = 0;
};

class TierListManager {
public:
    // Fail-soft: missing / invalid file leaves the manager empty.
    void load(const fs::path& path = "data/tier_data.json");

    bool loaded() const { return loaded_; }
    const std::vector<int>& levels() const { return levels_; }
    const std::vector<std::string>& tiers() const { return tiers_; }
    const std::vector<TierEntry>& entries() const { return entries_; }

    // Tier letters available for a given star level (empty if none).
    std::vector<std::string> tiers_for_level(int level) const;

    // Lookup by ESE folder name / romaji key (case-insensitive) at a star level.
    TierLookup lookup(const std::string& romaji_or_folder, int level) const;

    // Try candidates in order; first non-empty hit wins (use TITLE keys before folder).
    TierLookup lookup_first(const std::vector<std::string>& keys, int level) const;

    // Per-course tier string for Difficulty::ONI / URA at a star level (others empty).
    std::string tier_for(const std::string& romaji_or_folder, int course, int level) const;

    std::string tier_for_first(const std::vector<std::string>& keys, int course, int level) const;

    // Local song count per tier letter at a star level (folder names present in `folder_names`).
    std::unordered_map<std::string, int> count_local(
        const std::vector<std::string>& folder_names, int level) const;

    static std::string normalize_key(std::string s);
    static std::string folder_key_from_path(const fs::path& song_path);

private:
    bool loaded_ = false;
    std::vector<int> levels_;
    std::vector<std::string> tiers_;
    std::vector<TierEntry> entries_;
    // normalized key -> (level -> lookup)
    std::unordered_map<std::string, std::unordered_map<int, TierLookup>> by_key_;
};

extern TierListManager tier_list;

void init_tier_list();
