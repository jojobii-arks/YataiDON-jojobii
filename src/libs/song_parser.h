#pragma once

#include <variant>
#ifdef SUPPORT_FUMEN
#include "optional/fumen.h"
#endif
#include "drill.h"
#include "global_data.h"
#include "parsers/osu.h"
#include "parsers/tja.h"

// Exposes the same public interface as TJAParser so existing code
// can be updated by substituting SongParser for TJAParser.
class SongParser {
public:
    TJAMetadata metadata;
    TJAEXData   ex_data;
    fs::path    file_path;

    SongParser() = default;
    SongParser(const fs::path& path, int start_delay = 0, PlayerNum player_num = PlayerNum::ALL);
    explicit SongParser(SyntheticParser synth);
    void get_metadata() {}
    std::string get_difficulty_name();
    // Replaces the note list of a generated-mode parser. No effect otherwise.
    void set_synthetic_notes(NoteList notes);

    std::tuple<NoteList, std::deque<NoteList>, std::deque<NoteList>, std::deque<NoteList>>
    notes_to_position(int diff);

    std::string get_song_hash();
    std::string get_diff_hash(int difficulty);

#ifdef SUPPORT_FUMEN
    std::variant<TJAParser, OsuParser, FumenParser, SyntheticParser> impl;
#else
    std::variant<TJAParser, OsuParser, SyntheticParser> impl;
#endif

private:
    void sync();
};
