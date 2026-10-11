#!/usr/bin/env python3
"""Build a year's Dan-i Dojo course set as `dan.json` folders for YataiDON.

Data source: the Taiko no Tatsujin Wiki (https://taiko.wiki/dani/<year>) — its
Dan-i Dojo pages are server-rendered, so the course/song/condition tables can be
scraped straight out of the HTML.

Output: one folder per course, each holding a `dan.json` in the format the game's
`DanNavigator::load_dan_box_data` (src/scenes/dan_select.cpp) reads:

    {
      "title":  "初段",                 // shown on the dan chip
      "color":  2,                     // box/folder frame index
      "rank_art": 5,                   // rank plate index in the skin
      "dan_index": 11,                 // 0..24 (Shokyuu..Tatsujin)
      "exams": [ { "type": "gauge", "range": "more", "value": [97, 100] }, ... ],
      "charts": [ { "hash": "", "title": "<en TITLE>", "subtitle": "<en SUBTITLE>",
                    "difficulty": 3 }, ... ]
    }

Song identity: the game indexes songs by their **English/romaji** `TITLE:` and
`SUBTITLE:` (navigator.cpp builds `song_files` from `metadata.title["en"]`), while
the wiki lists Japanese titles. So songs are matched on `TITLEJA:` and the local
file's `TITLE:`/`SUBTITLE:` are what get written into the dan.json.

Exam type mapping (game_dan.cpp):
    Soul Guage -> gauge      Hits -> hit        Goods -> judgeperfect
    OKs        -> judgegood  Bads -> judgedbad  Drumrolls -> renda
    Combos     -> combo      Score -> score
A condition listing one value per song on the wiki (e.g. "58, 75, 29") becomes a
per-song `value: [[red, gold], ...]`; a single value is course-wide.

Usage:
    python tools/gen_dan.py --year 26 --out "../Songs/11 Dan Dojo/05 Nijiiro 2026 Version" --lib "../Songs"
    python tools/gen_dan.py --year 26 --html /tmp/dani26.html --dry-run
"""

from __future__ import annotations

import argparse
import html as html_mod
import json
import os
import re
import sys
import unicodedata
import urllib.request

WIKI = "https://taiko.wiki/dani/{year}?lang=en"
USER_AGENT = "YataiDON-dan-gen/1.0"

# wiki course id -> (dan_index, folder name, JP title, color, rank_art)
# Mirrors the rank plates/colors of the upstream Songs/11 Dan Dojo set.
RANKS = {
    "shokyuu":   (0,  "Shokyuu",   "初級", 0, 21),
    "10kyu":     (1,  "10th Kyuu", "十級", 0, 22),
    "9kyu":      (2,  "9th Kyuu",  "九級", 0, 23),
    "8kyu":      (3,  "8th Kyuu",  "八級", 0, 24),
    "7kyu":      (4,  "7th Kyuu",  "七級", 0, 25),
    "6kyu":      (5,  "6th Kyuu",  "六級", 0, 26),
    "5kyu":      (6,  "5th Kyuu",  "五級", 1, 0),
    "4kyu":      (7,  "4th Kyuu",  "四級", 1, 1),
    "3kyu":      (8,  "3rd Kyuu",  "三級", 1, 2),
    "2kyu":      (9,  "2nd Kyuu",  "二級", 1, 3),
    "1kyu":      (10, "1st Kyuu",  "一級", 1, 4),
    "1dan":      (11, "Shodan",    "初段", 2, 5),
    "2dan":      (12, "2nd Dan",   "二段", 2, 6),
    "3dan":      (13, "3rd Dan",   "三段", 2, 7),
    "4dan":      (14, "4th Dan",   "四段", 2, 8),
    "5dan":      (15, "5th Dan",   "五段", 2, 9),
    "6dan":      (16, "6th Dan",   "六段", 3, 10),
    "7dan":      (17, "7th Dan",   "七段", 3, 11),
    "8dan":      (18, "8th Dan",   "八段", 3, 12),
    "9dan":      (19, "9th Dan",   "九段", 3, 13),
    "10dan":     (20, "10th Dan",  "十段", 3, 14),
    "kuroto":    (21, "Kurouto",   "玄人", 4, 15),
    "meijin":    (22, "Meijin",    "名人", 4, 16),
    "chojin":    (23, "Choujin",   "超人", 4, 17),
    "tatsujin":  (24, "Tatsujin",  "達人", 5, 18),
}

# wiki difficulty query value -> YataiDON course index (script.cpp: 0..4 = Easy..Ura)
DIFFICULTY = {"easy": 0, "normal": 1, "hard": 2, "oni": 3, "ura": 4}
TJA_COURSE = {"Easy": 0, "Normal": 1, "Hard": 2, "Oni": 3, "Edit": 4}

EXAM_TYPES = {
    "Soul Guage": "gauge",
    "Soul Gauge": "gauge",
    "Hits": "hit",
    "Combos": "combo",
    "Goods": "judgeperfect",
    "OKs": "judgegood",
    "Bads": "judgebad",
    "Drumrolls": "renda",
    "Score": "score",
}


def norm(s: str) -> str:
    """Fold full/half width, case and spaces so ＊/！/～ variants compare equal."""
    s = unicodedata.normalize("NFKC", s or "")
    return re.sub(r"\s+", "", s).lower()


# ---------------------------------------------------------------- wiki scraping

def fetch(page_year: str, html_path: str | None) -> str:
    if html_path and os.path.exists(html_path):
        with open(html_path, encoding="utf-8") as f:
            return f.read()
    url = WIKI.format(year=page_year)
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=60) as resp:
        return resp.read().decode("utf-8", "replace")


def parse_conditions(block: str) -> list[dict]:
    """Condition containers: type div, then one 'item' div per red/gold threshold."""
    exams: list[dict] = []
    cond_area = block.split('<div class="container svelte-lzwsxn"', 1)[-1]
    for chunk in cond_area.split('<div class="container svelte-5ml3li"')[1:]:
        tm = re.search(r'<div class="type[^"]*"><span[^>]*>([^<]+)</span>', chunk)
        if not tm:
            continue
        raw_type = html_mod.unescape(tm.group(1)).strip()
        etype = EXAM_TYPES.get(raw_type)
        if etype is None:
            print(f"    ! unknown condition type {raw_type!r} — skipped")
            continue
        red = re.search(r'class="red[^"]*"><span[^>]*>([^<]+)</span>', chunk)
        gold = re.search(r'class="gold[^"]*"><span[^>]*>([^<]+)</span>', chunk)
        if not red:
            continue
        red_nums = [int(x) for x in re.findall(r"\d+", red.group(1))]
        gold_nums = [int(x) for x in re.findall(r"\d+", gold.group(1))] if gold else []
        if not red_nums:
            continue
        is_less = "under" in red.group(1).lower()
        if max(len(red_nums), len(gold_nums)) > 1:
            # one [red, gold] pair per song
            value = [[r, gold_nums[i]] if i < len(gold_nums) else [r]
                     for i, r in enumerate(red_nums)]
        else:
            value = [red_nums[0], gold_nums[0]] if gold_nums else [red_nums[0]]
        exams.append({"type": etype, "range": "less" if is_less else "more", "value": value})
    return exams


def parse_courses(page: str) -> list[dict]:
    blocks = re.split(r'(?=<div class="container svelte-1namkk9")', page)
    out: list[dict] = []
    for block in blocks[1:]:
        idm = re.search(r'id="dani-([a-z0-9]+)"', block)
        if not idm or idm.group(1) not in RANKS:
            continue
        key = idm.group(1)
        songs = [
            {"id": s[0], "difficulty": s[1], "title": html_mod.unescape(s[2]).strip()}
            for s in re.findall(
                r'href="/song/(\d+)\?diff=(\w+)"[^>]*>.*?<div class="title[^"]*">([^<]+)</div>',
                block, re.S)
        ]
        if not songs:
            continue
        stars = [int(x) for x in re.findall(r'<div difficulty="\w+"[^>]*>.*?★(\d+)', block)]
        for i, s in enumerate(songs):
            s["star"] = stars[i] if i < len(stars) else None
        notes = [int(x) for x in re.findall(
            r'<div class="combo[^"]*"><span[^>]*>(\d+)</span>',
            block.split('<div class="container svelte-lzwsxn"')[0])]
        for i, s in enumerate(songs):
            s["notes"] = notes[i] if i < len(notes) else None
        index, folder, jp, color, rank_art = RANKS[key]
        out.append({"key": key, "index": index, "folder": folder, "title": jp,
                    "color": color, "rank_art": rank_art,
                    "songs": songs, "exams": parse_conditions(block)})
    out.sort(key=lambda c: c["index"])
    return out


# ------------------------------------------------------------- local song index

def scan_library(roots: list[str]) -> list[dict]:
    idx: list[dict] = []
    for root in roots:
        for dp, _dn, fn in os.walk(root):
            for name in fn:
                if not name.lower().endswith(".tja"):
                    continue
                path = os.path.join(dp, name)
                try:
                    raw = open(path, "rb").read()
                except OSError:
                    continue
                text = None
                for enc in ("utf-8-sig", "cp932"):
                    try:
                        text = raw.decode(enc)
                        break
                    except UnicodeDecodeError:
                        continue
                if text is None:
                    continue
                meta = {"TITLE": "", "TITLEJA": "", "SUBTITLE": "", "SUBTITLEJA": ""}
                courses: dict[int, int] = {}
                current = None
                for line in text.splitlines():
                    m = re.match(r"^\s*(TITLE|TITLEJA|SUBTITLE|SUBTITLEJA|COURSE|LEVEL)\s*:(.*)$", line)
                    if not m:
                        continue
                    key, val = m.group(1), m.group(2).strip()
                    if key in meta:
                        # tja prefixes the subtitle with ++ / "/ " / -- to pick how it
                        # is displayed; the value itself is what follows (parser: tja.cpp).
                        if key.startswith("SUBTITLE"):
                            val = re.sub(r"^(\+\+|/\s|--)", "", val).strip()
                        if not meta[key]:
                            meta[key] = val
                    elif key == "COURSE":
                        current = TJA_COURSE.get(val)
                    elif key == "LEVEL" and current is not None:
                        try:
                            courses[current] = int(val)
                        except ValueError:
                            pass
                        current = None
                if not courses:
                    continue
                meta["courses"] = courses
                meta["path"] = path
                meta["notes"] = count_notes(text)
                idx.append(meta)
    return idx


NOTE_RE = re.compile(r"^\s*[0-9]{0,4}[,\s]*[0-9]+[,\s]*$")


def count_notes(text: str) -> dict[int, int]:
    """Per-course count of hittable notes (don/kat, big included), rolls excluded.

    Mirrors the game's `total_notes_for`: the section before #BRANCHSTART plus the
    Master branch (#M), which is what the parser reports as the main note list.
    """
    counts: dict[int, int] = {}
    current = None
    variant = None  # None = main section; "N"/"E"/"M" inside a branch group
    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith("//"):
            continue
        m = re.match(r"^COURSE\s*:(.*)$", s)
        if m:
            current = TJA_COURSE.get(m.group(1).strip())
            variant = None
            continue
        if s.startswith("#"):
            if s.startswith("#BRANCHSTART"):
                variant = "N"  # notes before the first variant belong to the main list
            elif s.startswith("#BRANCHEND"):
                variant = None
            elif s.startswith("#NEXTSONG") or s.startswith("#END"):
                current = None
                variant = None
            elif s in ("#N", "#E", "#M"):
                variant = s[1]
            continue
        if current is None or (variant is not None and variant != "M"):
            continue
        body = re.sub(r"//.*$", "", s)
        counts[current] = counts.get(current, 0) + sum(body.count(c) for c in "1234")
    return counts


def match_song(song: dict, library: list[dict]) -> tuple[dict | None, int, list[str]]:
    """Pick the local file + difficulty for a wiki song entry.

    Preference: an exact TITLEJA/TITLE hit over a substring hit, a chart whose
    level matches the wiki's ★, the course the wiki's `diff` names, and finally
    the note count closest to (and not above) the wiki's.
    """
    warnings: list[str] = []
    want_title = song["title"]
    want_diff = DIFFICULTY.get(song["difficulty"], 3)
    want_star = song["star"]
    want_notes = song.get("notes")
    nt = norm(want_title)
    if not nt:
        return None, want_diff, ["wiki entry has an empty title"]

    exact = [e for e in library if nt == norm(e["TITLEJA"]) or nt == norm(e["TITLE"])]
    loose = []
    if not exact:
        for e in library:
            nja, nen = norm(e["TITLEJA"]), norm(e["TITLE"])
            if (nja and nt in nja) or (nen and nt in nen) or (nja and nja in nt) or (nen and nen in nt):
                loose.append(e)
    pool = exact or loose
    if not pool:
        return None, want_diff, [f"no local chart titled {want_title!r}"]

    def choose(entry: dict) -> int | None:
        if want_diff in entry["courses"]:
            return want_diff
        if want_star is not None:
            same = [c for c, lv in entry["courses"].items() if lv == want_star]
            if same:
                return sorted(same)[0]
        return None

    def score(entry: dict) -> tuple:
        difficulty = choose(entry)
        notes = entry["notes"].get(difficulty) if difficulty is not None else None
        # Our count is a rough parse (branches included), so only a *large* gap
        # counts as a different chart; a couple of notes either way is noise.
        mismatch = 0
        if want_notes and notes:
            delta = notes - want_notes
            mismatch = 0 if abs(delta) <= max(6, 0.03 * want_notes) else abs(delta)
        return (
            difficulty is None,
            want_star is not None and want_star not in entry["courses"].values(),
            mismatch,
        )

    entry = sorted(pool, key=score)[0]
    difficulty = choose(entry)
    if entry not in exact:
        warnings.append(f"fuzzy title match: {want_title!r} -> {entry['TITLEJA'] or entry['TITLE']!r}")
    if len(pool) > 1:
        others = ", ".join(f"{e['TITLE']} {e['courses']}" for e in pool if e is not entry)
        warnings.append(f"{want_title!r}: {len(pool)} candidates, chose {entry['TITLE']!r}; also saw {others}")

    if difficulty is None:
        warnings.append(f"{entry['TITLE']!r}: no course {want_diff} and no ★{want_star} chart; "
                        f"courses {entry['courses']}")
        difficulty = want_diff
    elif difficulty != want_diff:
        warnings.append(f"{entry['TITLE']!r}: no course {want_diff}, using course {difficulty} (★{want_star})")
    elif want_star is not None and entry["courses"][difficulty] != want_star:
        warnings.append(f"{entry['TITLE']!r}: course {difficulty} is ★{entry['courses'][difficulty]}, "
                        f"wiki says ★{want_star}")

    notes = entry["notes"].get(difficulty)
    if want_notes and notes and abs(notes - want_notes) > max(6, 0.03 * want_notes):
        warnings.append(f"{entry['TITLE']!r}: local chart has {notes} notes, wiki says {want_notes} "
                        f"({notes - want_notes:+d}) — check this is the right chart")
    return entry, difficulty, warnings


# ---------------------------------------------------------------------- emitting

def build_course(course: dict, library: list[dict], verbose: bool) -> dict | None:
    charts = []
    warnings: list[str] = []
    local_notes = 0
    for song in course["songs"]:
        entry, difficulty, warn = match_song(song, library)
        warnings += [f"{song['title']}: {w}" for w in warn]
        if entry is None:
            continue
        local_notes += entry["notes"].get(difficulty, 0)
        charts.append({
            "hash": "",
            "title": entry["TITLE"],
            "subtitle": entry["SUBTITLE"],
            "difficulty": difficulty,
        })
        if verbose:
            lv = entry["courses"].get(difficulty)
            print(f"      {song['title']:<26} ★{song['star']} {song['difficulty']:<6} -> "
                  f"{entry['TITLE'][:34]:<34} diff={difficulty} ★{lv} "
                  f"notes {entry['notes'].get(difficulty)}/{song.get('notes')}")
    # Course-wide thresholds are absolute numbers taken from the arcade charts, so a
    # local chart with a different note count can make an exam unreachable.
    # 'hit' is deliberately not checked: it counts drumroll hits too, so it can
    # legitimately exceed the chart's note count.
    for exam in course["exams"]:
        if exam["type"] not in ("judgeperfect", "judgegood"):
            continue
        if exam["range"] != "more" or not isinstance(exam["value"][0], int):
            continue
        if exam["value"][0] > local_notes:
            warnings.append(f"{exam['type']} threshold {exam['value'][0]} exceeds the "
                            f"{local_notes} notes the matched charts contain")
    if not charts:
        print(f"    !! {course['title']}: no songs matched, skipped")
        return None
    if len(charts) != len(course["songs"]):
        print(f"    !! {course['title']}: {len(charts)}/{len(course['songs'])} songs matched")
    for w in warnings:
        print(f"    ~ {w}")
    return {
        "title": course["title"],
        "color": course["color"],
        "rank_art": course["rank_art"],
        "dan_index": course["index"],
        "exams": [{"type": e["type"], "range": e["range"], "value": e["value"]}
                  for e in course["exams"]],
        "charts": charts,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--year", default="26", help="wiki dan page year token, e.g. 26")
    ap.add_argument("--out", required=True, help="output folder for the version set")
    ap.add_argument("--lib", nargs="+", default=["./Songs"], help="song library root(s)")
    ap.add_argument("--name", default=None, help="version prefix for course folder names")
    ap.add_argument("--html", default=None, help="cached wiki HTML instead of fetching")
    ap.add_argument("--dump-html", default=None, help="save the fetched HTML here")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    page = fetch(args.year, args.html)
    if args.dump_html:
        with open(args.dump_html, "w", encoding="utf-8") as f:
            f.write(page)

    courses = parse_courses(page)
    if not courses:
        print("no courses parsed — the wiki markup probably changed")
        return 1

    label = args.name or f"Nijiiro 20{args.year}"
    libraries = [os.path.abspath(p) for p in args.lib]
    print(f"scanning library: {', '.join(libraries)}")
    library = scan_library(libraries)
    print(f"  {len(library)} playable charts indexed\n")
    if not library:
        print("library scan found no charts — check --lib")
        return 1

    print(f"{label}: {len(courses)} courses "
          f"({courses[0]['folder']} .. {courses[-1]['folder']})\n")
    written = 0
    for course in courses:
        print(f"  [{course['index']:>2}] {course['title']} ({course['folder']})")
        data = build_course(course, library, verbose=True)
        if data is None:
            continue
        if args.dry_run:
            continue
        folder = os.path.join(args.out, f"{course['index']:02d} {label} {course['folder']}")
        os.makedirs(folder, exist_ok=True)
        with open(os.path.join(folder, "dan.json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(data, f, ensure_ascii=False, indent=4)
            f.write("\n")
        written += 1
    if args.dry_run:
        print("\ndry run — nothing written")
    else:
        print(f"\nwrote {written} dan.json files under {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())