#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

// Community clear-tier list from taiko.wiki (level 10), loaded once from a
// committed JSON artifact. Never hits the network at runtime.
struct TierEntry {
    std::string romaji;
    std::string difficulty;  // "oni" | "ura"
    std::string tier;        // "SSS", "SS", "S+", ...
    int order = 0;
};

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
    const std::vector<std::string>& tiers() const { return tiers_; }
    const std::vector<TierEntry>& entries() const { return entries_; }

    // Lookup by ESE folder name / romaji key (case-insensitive).
    TierLookup lookup(const std::string& romaji_or_folder) const;

    // Per-course tier string for Difficulty::ONI / URA (others empty).
    std::string tier_for(const std::string& romaji_or_folder, int course) const;

    // Local song count per tier letter (folder names present in `folder_names`).
    std::unordered_map<std::string, int> count_local(
        const std::vector<std::string>& folder_names) const;

    static std::string normalize_key(std::string s);
    static std::string folder_key_from_path(const fs::path& song_path);

private:
    bool loaded_ = false;
    std::vector<std::string> tiers_;
    std::vector<TierEntry> entries_;
    // normalized key -> lookup
    std::unordered_map<std::string, TierLookup> by_key_;
};

extern TierListManager tier_list;

void init_tier_list();
