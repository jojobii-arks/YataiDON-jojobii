#include "tier_list.h"
#include "filesystem.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>

TierListManager tier_list;

std::string TierListManager::normalize_key(std::string s) {
    // Trim + ASCII lowercase. Folder names and wiki romaji are compared this way.
    while (!s.empty() && (unsigned char)s.front() <= ' ') s.erase(s.begin());
    while (!s.empty() && (unsigned char)s.back() <= ' ') s.pop_back();
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string TierListManager::folder_key_from_path(const fs::path& song_path) {
    // ESE layout: Genre/<Romaji Folder>/<chart>.tja — match on the parent folder.
    fs::path parent = song_path.parent_path();
    if (parent.empty()) return normalize_key(song_path.stem().string());
    return normalize_key(parent.filename().string());
}

void TierListManager::load(const fs::path& path) {
    loaded_ = false;
    levels_.clear();
    tiers_.clear();
    entries_.clear();
    by_key_.clear();

    std::error_code ec;
    if (!fs::exists(path, ec)) {
        spdlog::warn("tier_list: {} not found (tiers disabled)", path.string());
        return;
    }

    rapidjson::Document doc;
    try {
        doc = read_json_file(path);
    } catch (const std::exception& e) {
        spdlog::warn("tier_list: failed to read {}: {}", path.string(), e.what());
        return;
    } catch (...) {
        spdlog::warn("tier_list: failed to read {}", path.string());
        return;
    }

    if (!doc.IsObject()) {
        spdlog::warn("tier_list: {} is not a JSON object", path.string());
        return;
    }

    if (doc.HasMember("levels") && doc["levels"].IsArray()) {
        for (const auto& l : doc["levels"].GetArray())
            if (l.IsInt()) levels_.push_back(l.GetInt());
    }
    if (doc.HasMember("tiers") && doc["tiers"].IsArray()) {
        for (const auto& t : doc["tiers"].GetArray()) {
            if (t.IsString()) tiers_.push_back(t.GetString());
        }
    }

    if (!doc.HasMember("entries") || !doc["entries"].IsArray()) {
        spdlog::warn("tier_list: {} has no entries array", path.string());
        loaded_ = !tiers_.empty();
        return;
    }

    auto apply_key = [&](const std::string& raw_key, const TierEntry& entry) {
        std::string key = normalize_key(raw_key);
        if (key.empty()) return;
        TierLookup& slot = by_key_[key][entry.level];
        if (entry.difficulty == "ura") {
            slot.ura = entry.tier;
            slot.ura_order = entry.order;
        } else {
            slot.oni = entry.tier;
            slot.oni_order = entry.order;
        }
    };

    for (const auto& e : doc["entries"].GetArray()) {
        if (!e.IsObject()) continue;
        TierEntry entry;
        if (e.HasMember("romaji") && e["romaji"].IsString())
            entry.romaji = e["romaji"].GetString();
        if (e.HasMember("difficulty") && e["difficulty"].IsString())
            entry.difficulty = e["difficulty"].GetString();
        else
            entry.difficulty = "oni";
        if (e.HasMember("tier") && e["tier"].IsString())
            entry.tier = e["tier"].GetString();
        if (e.HasMember("order") && e["order"].IsInt())
            entry.order = e["order"].GetInt();
        if (e.HasMember("level") && e["level"].IsInt())
            entry.level = e["level"].GetInt();
        if (entry.tier.empty()) continue;

        entries_.push_back(entry);
        apply_key(entry.romaji, entry);
        if (e.HasMember("keys") && e["keys"].IsArray()) {
            for (const auto& k : e["keys"].GetArray()) {
                if (k.IsString()) apply_key(k.GetString(), entry);
            }
        }
    }

    if (tiers_.empty()) {
        // Derive tier order from first-seen entry order if the file omitted `tiers`.
        for (const auto& entry : entries_) {
            if (std::find(tiers_.begin(), tiers_.end(), entry.tier) == tiers_.end())
                tiers_.push_back(entry.tier);
        }
    }

    loaded_ = true;
    spdlog::info("tier_list: loaded {} entries ({} keys) from {}",
                 entries_.size(), by_key_.size(), path.string());
}

std::vector<std::string> TierListManager::tiers_for_level(int level) const {
    std::vector<std::string> out;
    for (const auto& entry : entries_) {
        if (entry.level != level) continue;
        if (std::find(out.begin(), out.end(), entry.tier) == out.end())
            out.push_back(entry.tier);
    }
    return out;
}

TierLookup TierListManager::lookup(const std::string& romaji_or_folder, int level) const {
    auto it = by_key_.find(normalize_key(romaji_or_folder));
    if (it == by_key_.end()) return {};
    auto lit = it->second.find(level);
    if (lit == it->second.end()) return {};
    return lit->second;
}

std::string TierListManager::tier_for(const std::string& romaji_or_folder, int course, int level) const {
    TierLookup l = lookup(romaji_or_folder, level);
    if (course == 4) return l.ura;
    if (course == 3) return l.oni;
    return {};
}

std::unordered_map<std::string, int> TierListManager::count_local(
    const std::vector<std::string>& folder_names, int level) const {
    std::unordered_map<std::string, int> counts;
    for (const auto& t : tiers_for_level(level)) counts[t] = 0;
    std::unordered_map<std::string, bool> seen;  // folder|diff to avoid double-count
    for (const auto& folder : folder_names) {
        TierLookup l = lookup(folder, level);
        if (!l.oni.empty()) {
            std::string k = normalize_key(folder) + "|oni";
            if (!seen[k]) { seen[k] = true; counts[l.oni]++; }
        }
        if (!l.ura.empty()) {
            std::string k = normalize_key(folder) + "|ura";
            if (!seen[k]) { seen[k] = true; counts[l.ura]++; }
        }
    }
    return counts;
}

void init_tier_list() {
    tier_list.load("data/tier_data.json");
}
