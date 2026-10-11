#include "drill.h"
#include "audio.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <spdlog/spdlog.h>
#include <tuple>

namespace {

void write_u16(std::ostream& out, uint16_t v) {
    uint8_t b[2] = {uint8_t(v & 0xff), uint8_t((v >> 8) & 0xff)};
    out.write(reinterpret_cast<char*>(b), 2);
}

void write_u32(std::ostream& out, uint32_t v) {
    uint8_t b[4] = {
        uint8_t(v & 0xff), uint8_t((v >> 8) & 0xff),
        uint8_t((v >> 16) & 0xff), uint8_t((v >> 24) & 0xff)};
    out.write(reinterpret_cast<char*>(b), 4);
}

bool allowed_pattern_char(char c) {
    switch (c) {
        case 'd': case 'k': case 'D': case 'K':
        case '1': case '2': case '3': case '4':
        case '0': case '.': case '-': case '_': case ' ':
            return true;
        default:
            return false;
    }
}

double tenth(double v) {
    return std::round(v * 10.0) / 10.0;
}

bool near(double a, double b) {
    return std::abs(a - b) < 0.001;
}

DrillSlot slot_from_char(char c) {
    DrillSlot slot;
    switch (c) {
        case 'd': case '1': slot.rest = false; slot.type = NoteType::DON; break;
        case 'k': case '2': slot.rest = false; slot.type = NoteType::KAT; break;
        case 'D': case '3': slot.rest = false; slot.type = NoteType::DON_L; break;
        case 'K': case '4': slot.rest = false; slot.type = NoteType::KAT_L; break;
        default: slot.rest = true; break;
    }
    return slot;
}

std::vector<DrillSlot> slots_from_pattern(const std::string& pattern) {
    std::vector<DrillSlot> slots;
    slots.reserve(pattern.size());
    for (char c : pattern) slots.push_back(slot_from_char(c));
    if (slots.empty()) slots.push_back(slot_from_char('d'));
    return slots;
}

std::string clean_phrase(const std::string& raw) {
    std::string cleaned;
    cleaned.reserve(raw.size());
    for (unsigned char uc : raw) {
        char c = (char)uc;
        if (c == '|' || c == ',') break;
        cleaned.push_back(allowed_pattern_char(c) ? c : '.');
        if ((int)cleaned.size() >= DRILL_PATTERN_MAX) break;
    }
    return cleaned;
}

std::vector<std::string> split_phrases(const std::string& seq) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        std::string cleaned = clean_phrase(cur);
        if (!cleaned.empty()) out.push_back(std::move(cleaned));
        cur.clear();
    };
    for (char c : seq) {
        if (c == '|' || c == ',') flush();
        else cur.push_back(c);
    }
    flush();
    if ((int)out.size() > WORKOUT_PHRASE_MAX) out.resize(WORKOUT_PHRASE_MAX);
    return out;
}

std::string join_phrases(const std::vector<std::string>& phrases) {
    std::string out;
    for (const std::string& phrase : phrases) {
        if (!out.empty()) out.push_back('|');
        out += phrase;
    }
    return out;
}

std::string apply_invert(std::string pattern) {
    for (char& c : pattern) {
        switch (c) {
            case 'd': case '1': c = 'k'; break;
            case 'k': case '2': c = 'd'; break;
            case 'D': case '3': c = 'K'; break;
            case 'K': case '4': c = 'D'; break;
            default: break;
        }
    }
    return pattern;
}

struct Placed {
    double t = 0;
    int ord = 1;
    bool bar = false;
    NoteType type = NoteType::DON;
    int hand = 0;
};

void emit_placed(DrillChart& chart, std::vector<Placed>& evs) {
    std::sort(evs.begin(), evs.end(), [](const Placed& a, const Placed& b) {
        if (a.t != b.t) return a.t < b.t;
        return a.ord < b.ord;
    });
    for (const Placed& e : evs) {
        Note note;
        note.type = e.bar ? NoteType::BARLINE : e.type;
        note.hit_ms = e.t;
        note.bpm = chart.bpm;
        note.scroll_x = chart.spec.scroll;
        note.scroll_y = 0.0;
        note.display = true;
        note.index = chart.next_index++;
        note.workout_hand = e.bar ? 0 : e.hand;
        chart.notes.notes.push_back(note);
        if (!e.bar) chart.steps.push_back(e.t);
    }
    evs.clear();
}

void collect_bars(DrillChart& chart, double up_to, std::vector<Placed>& evs) {
    const double measure = chart.beat_ms * 4.0;
    if (measure <= 0.0) return;
    while (chart.next_bar_ms <= up_to + 1e-4) {
        Placed bar;
        bar.t = chart.next_bar_ms;
        bar.ord = 0;
        bar.bar = true;
        bar.type = NoteType::BARLINE;
        evs.push_back(bar);
        chart.next_bar_ms += measure;
    }
}

std::string pick_phrase(DrillChart& chart, int index, bool randomize) {
    if (chart.phrases.empty()) return "dk";
    if (randomize) {
        std::uniform_int_distribution<int> dist(0, (int)chart.phrases.size() - 1);
        return chart.phrases[dist(chart.rng)];
    }
    return chart.phrases[index % (int)chart.phrases.size()];
}

void place_note(DrillChart& chart, std::vector<Placed>& evs, double t, NoteType type) {
    Placed note;
    note.t = t;
    note.ord = 1;
    note.type = type;
    note.hand = chart.spec.alternate ? chart.next_hand : 0;
    evs.push_back(note);
    if (chart.spec.alternate)
        chart.next_hand = chart.next_hand == 1 ? 2 : 1;
}

void append_pattern_reps(DrillChart& chart, int extra_reps) {
    if (extra_reps < 1) return;
    std::vector<Placed> evs;
    const double note_step = chart.beat_ms * note_div_beats(chart.spec.note_div);
    const double pattern_span = chart.spec.pattern_every * chart.beat_ms;
    const double sequence_gap = chart.spec.sequence_interval * chart.beat_ms;
    double latest = chart.place_t;
    auto place_phrase = [&](const std::string& raw) {
        std::string pat = chart.spec.invert ? apply_invert(raw) : raw;
        const double start = chart.place_t;
        double t = start;
        for (const DrillSlot& slot : slots_from_pattern(pat)) {
            if (!slot.rest) {
                place_note(chart, evs, t, slot.type);
                if (t > latest) latest = t;
            }
            t += note_step;
        }
        chart.place_t = start + pattern_span;
        if (chart.place_t > latest) latest = chart.place_t;
    };
    const int target = chart.reps_generated + extra_reps;
    for (int rep = chart.reps_generated; rep < target; ++rep) {
        if (chart.spec.random_pool) {
            place_phrase(pick_phrase(chart, 0, true));
            continue;
        }
        const int phrase_count = std::max(1, (int)chart.phrases.size());
        for (int i = 0; i < phrase_count; ++i)
            place_phrase(pick_phrase(chart, i, false));
        const bool more = chart.endless || (rep + 1 < chart.spec.repetitions);
        if (more) {
            chart.place_t += sequence_gap;
            if (chart.place_t > latest) latest = chart.place_t;
        }
    }
    collect_bars(chart, latest, evs);
    emit_placed(chart, evs);
    chart.reps_generated = target;
    chart.generated_until_ms = latest;
}

void append_drill_notes(DrillChart& chart, int extra_notes) {
    if (extra_notes < 1) return;
    std::vector<Placed> evs;
    // Rhythra draws drill notes (index - cursor) * spacing units across a 74-unit approach.
    const double scroll = std::max(WORKOUT_SCROLL_MIN, chart.spec.scroll);
    const double travel_ms = (chart.beat_ms * 4.0) / scroll;
    const double step = travel_ms * ((double)chart.spec.note_spacing / 74.0);
    int placed = 0;
    int guard = 0;
    const int guard_max = extra_notes * 8 + 16;
    while (placed < extra_notes && guard++ < guard_max) {
        std::string pat = pick_phrase(chart, placed, true);
        int before = placed;
        for (const DrillSlot& slot : slots_from_pattern(pat)) {
            if (slot.rest || placed >= extra_notes) continue;
            place_note(chart, evs, chart.place_t, slot.type);
            chart.place_t += step;
            placed++;
        }
        if (placed == before) {
            place_note(chart, evs, chart.place_t, NoteType::DON);
            chart.place_t += step;
            placed++;
        }
    }
    collect_bars(chart, chart.place_t, evs);
    emit_placed(chart, evs);
    chart.generated_until_ms = chart.place_t;
}

}  // namespace

const std::vector<std::string>& workout_catalog() {
    static const std::vector<std::string> catalog = [] {
        std::vector<std::string> out;
        for (int len = 1; len <= 6; ++len) {
            const int combos = 1 << len;
            for (int bits = 0; bits < combos; ++bits) {
                std::string notes(len, 'd');
                for (int i = 0; i < len; ++i)
                    if ((bits >> i) & 1) notes[i] = 'k';
                out.push_back(std::move(notes));
            }
        }
        return out;
    }();
    return catalog;
}

std::string workout_pattern_name(const std::string& notes) {
    std::string words;
    int count = 0;
    for (char c : notes) {
        const char* word = nullptr;
        switch (c) {
            case 'd': case '1': word = "Don"; break;
            case 'k': case '2': word = "Ka"; break;
            case 'D': case '3': word = "Big Don"; break;
            case 'K': case '4': word = "Big Ka"; break;
            default: break;
        }
        if (!word) continue;
        if (count) words.push_back(' ');
        words += word;
        count++;
    }
    if (count == 0) return "rest";
    return std::to_string(count) + " Hit: " + words;
}

DrillConfig normalize_drill_config(DrillConfig in) {
    in.kind = in.kind == 1 ? 1 : 0;
    in.bpm = std::clamp(in.bpm, DRILL_BPM_MIN, DRILL_BPM_MAX);
    in.scroll = std::clamp(tenth(in.scroll), WORKOUT_SCROLL_MIN, WORKOUT_SCROLL_MAX);
    switch (in.subdivision) {
        case 1: case 2: case 3: case 4: case 6: case 8: break;
        default: in.subdivision = 4; break;
    }
    in.note_div = clamp_note_div(in.note_div);
    in.sequence = join_phrases(split_phrases(in.sequence));
    auto phrases = split_phrases(in.sequence);
    in.pattern = phrases.empty() ? "dk" : phrases.front();
    in.repetitions = std::clamp(in.repetitions, 1, WORKOUT_REPS_MAX);
    in.pattern_interval = std::clamp(tenth(in.pattern_interval), 0.0, 8.0);
    in.pattern_every = std::clamp(in.pattern_every, WORKOUT_PATTERN_EVERY_MIN, WORKOUT_PATTERN_EVERY_MAX);
    in.sequence_interval = std::clamp(tenth(in.sequence_interval), 0.0, 8.0);
    in.max_misses = std::clamp(in.max_misses, 1, 10);
    in.timer_sec = std::clamp(in.timer_sec, 10, 3600);
    in.total_notes = std::clamp(in.total_notes, WORKOUT_NOTES_MIN, WORKOUT_NOTES_MAX);
    in.note_spacing = std::clamp(in.note_spacing, WORKOUT_SPACE_MIN, WORKOUT_SPACE_MAX);
    in.size = in.endless ? 0 : std::min(in.repetitions, DRILL_SIZE_MAX);
    return in;
}

bool same_drill_config(const DrillConfig& a, const DrillConfig& b) {
    return a.kind == b.kind
        && a.bpm == b.bpm
        && near(a.scroll, b.scroll)
        && a.subdivision == b.subdivision
        && a.note_div == b.note_div
        && a.pattern == b.pattern
        && a.sequence == b.sequence
        && a.repetitions == b.repetitions
        && near(a.pattern_interval, b.pattern_interval)
        && a.pattern_every == b.pattern_every
        && near(a.sequence_interval, b.sequence_interval)
        && a.endless == b.endless
        && a.invert == b.invert
        && a.random_pool == b.random_pool
        && a.alternate == b.alternate
        && a.lead_left == b.lead_left
        && a.no_miss == b.no_miss
        && a.miss_restart == b.miss_restart
        && a.max_misses == b.max_misses
        && a.timer_on == b.timer_on
        && a.timer_sec == b.timer_sec
        && a.metronome == b.metronome
        && a.total_notes == b.total_notes
        && a.note_spacing == b.note_spacing;
}

DrillChart build_drill_chart(const DrillConfig& raw) {
    DrillChart chart;
    chart.spec = normalize_drill_config(raw);
    chart.hold = chart.spec.kind == 1;
    chart.phrases = chart.hold ? workout_catalog() : split_phrases(chart.spec.sequence);
    if (!chart.phrases.empty()) chart.slots = slots_from_pattern(chart.phrases.front());
    chart.bpm = chart.spec.bpm;
    chart.beat_ms = 60000.0 / chart.bpm;
    chart.count_in_ms = chart.beat_ms * 4.0;
    chart.place_t = chart.count_in_ms;
    chart.next_bar_ms = 0.0;
    chart.next_hand = chart.spec.lead_left ? 1 : 2;
    chart.endless = chart.spec.endless;
    chart.cycle_ms = chart.beat_ms * 2.0;
    chart.rng.seed((uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());

    if (chart.hold) {
        append_drill_notes(chart, chart.endless ? 100 : chart.spec.total_notes);
    } else if (!chart.phrases.empty()) {
        int reps = chart.spec.repetitions;
        if (chart.endless) reps = chart.spec.random_pool ? 12 : DRILL_ENDLESS_CHUNK;
        if (reps < 1) reps = 1;
        append_pattern_reps(chart, reps);
    }

    spdlog::info("Workout chart: {} {} BPM scroll {:.1f} '{}'",
                 chart.hold ? "drill" : "pattern", chart.spec.bpm, chart.spec.scroll,
                 chart.spec.sequence.empty() ? chart.spec.pattern : chart.spec.sequence);
    return chart;
}

NoteList extend_drill_chart(DrillChart& chart, int extra) {
    NoteList added;
    if (!chart.endless || extra < 1) return added;
    const size_t before = chart.notes.notes.size();
    if (chart.hold) append_drill_notes(chart, extra);
    else append_pattern_reps(chart, extra);
    if (chart.notes.notes.size() > before) {
        added.notes.insert(added.notes.end(),
                           chart.notes.notes.begin() + (std::ptrdiff_t)before,
                           chart.notes.notes.end());
    }
    return added;
}

SyntheticParser make_synthetic_parser(const DrillChart& chart) {
    SyntheticParser parser;
    parser.metadata.bpm = chart.bpm;
    parser.metadata.offset = 0.0;
    parser.metadata.title["en"] = "Workout";
    parser.metadata.subtitle["en"] = chart.hold ? "Drill" : "Pattern";
    CourseData course;
    course.level = 8;
    course.is_branching = false;
    parser.metadata.course_data[DRILL_DIFFICULTY] = course;
    parser.notes = chart.notes;
    return parser;
}

bool load_drill_click() {
    try {
        const fs::path path = fs::temp_directory_path() / "yataidon_drill_click.wav";
        const int rate = 44100;
        const int frames = rate / 66; // ~15 ms
        std::vector<int16_t> pcm(frames);
        for (int i = 0; i < frames; ++i) {
            double t = (double)i / (double)rate;
            double env = std::exp(-t * 140.0);
            double s = std::sin(2.0 * 3.141592653589793 * 1800.0 * t) * env;
            pcm[i] = (int16_t)std::clamp(s * 26000.0, -32767.0, 32767.0);
        }
        const uint32_t data_bytes = (uint32_t)pcm.size() * 2u;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            spdlog::error("Drill click: cannot write {}", path.string());
            return false;
        }
        out.write("RIFF", 4);
        write_u32(out, 36u + data_bytes);
        out.write("WAVE", 4);
        out.write("fmt ", 4);
        write_u32(out, 16);
        write_u16(out, 1);
        write_u16(out, 1);
        write_u32(out, (uint32_t)rate);
        write_u32(out, (uint32_t)rate * 2u);
        write_u16(out, 2);
        write_u16(out, 16);
        out.write("data", 4);
        write_u32(out, data_bytes);
        out.write(reinterpret_cast<const char*>(pcm.data()), (std::streamsize)data_bytes);
        out.close();
        if (!out) {
            spdlog::error("Drill click: write failed for {}", path.string());
            return false;
        }
        return !audio.load_sound(path, "drill_click").empty();
    } catch (const std::exception& e) {
        spdlog::error("Drill click: {}", e.what());
        return false;
    }
}
