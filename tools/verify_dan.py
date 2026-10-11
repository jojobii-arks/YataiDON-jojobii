#!/usr/bin/env python3
"""Dry-load a Dan-i Dojo set the way the game does, to prove it will show up.

Simulates `DanNavigator::load_dan_box_data` / `parse_exam`
(src/scenes/dan_select.cpp) and `Navigator::find_song_by_title`
(src/objects/song_select/file_navigator/navigator.cpp) over a folder of
`dan.json` courses:

  * required keys (title string, color int) and the dan_index derived from the
    folder's leading number when the json omits it
  * every chart resolving to a song the library indexes by its English
    TITLE/SUBTITLE (the game normalizes both sides: ASCII punctuation, spaces,
    -New/-Old Audio- tags and case are dropped)
  * every exam parsing into a concrete red/gold border, course-wide or per song
  * the note total the dan box would display

Usage:
    python tools/verify_dan.py "../Songs/11 Dan Dojo/05 Nijiiro 2026 Version" --lib "../Songs"
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from gen_dan import DIFFICULTY, scan_library  # noqa: E402

DIFF_NAME = {0: "Easy", 1: "Normal", 2: "Hard", 3: "Oni", 4: "Ura"}
GOLD_FULL = -1
VALID_TYPES = {"gauge", "combo", "judgebad", "judgegood", "judgeperfect", "hit", "score", "renda"}


def game_norm(s: str) -> str:
    """Port of the C++ normalize_title()."""
    for tag in ("-New Audio-", "-新曲-", "-Old Audio-", "-旧曲-"):
        s = s.replace(tag, "")
    s = "".join(c for c in s if not (ord(c) < 0x80 and not c.isalnum()))
    return s.lower()


def parse_exam(e: dict) -> dict:
    """Port of DanNavigator::parse_exam (incl. the gold-side sanity check)."""
    out = {"type": e.get("type", ""), "range": e.get("range", ""),
           "red": 0, "gold": GOLD_FULL, "song_red": [], "song_gold": []}
    value = e.get("value", [])
    if value and isinstance(value[0], list):
        for pair in value:
            if not pair or not isinstance(pair[0], int):
                continue
            out["song_red"].append(pair[0])
            out["song_gold"].append(pair[1] if len(pair) > 1 and isinstance(pair[1], int) else GOLD_FULL)
        if out["song_red"]:
            out["red"], out["gold"] = out["song_red"][0], out["song_gold"][0]
    elif value and isinstance(value[0], int):
        out["red"] = value[0]
        out["gold"] = value[1] if len(value) > 1 and isinstance(value[1], int) else GOLD_FULL
    for i, gold in enumerate(out["song_gold"]):
        if gold != GOLD_FULL and ((out["range"] == "less") == (gold > out["song_red"][i])):
            out["song_gold"][i] = GOLD_FULL
    if out["gold"] != GOLD_FULL and ((out["range"] == "less") == (out["gold"] > out["red"])):
        out["gold"] = GOLD_FULL
    out["per_song"] = bool(out["song_red"])
    return out


def verify_folder(folder: str, library: list[dict]) -> int:
    index: dict[tuple[str, str], dict] = {}
    for e in library:
        index[(game_norm(e["TITLE"]), game_norm(e["SUBTITLE"]))] = e
    by_title: dict[str, dict] = {}
    for e in library:
        by_title.setdefault(game_norm(e["TITLE"]), e)

    json_files = []
    for dp, _dn, fn in os.walk(folder):
        if "dan.json" in fn:
            json_files.append(os.path.join(dp, "dan.json"))
    json_files.sort(key=lambda p: os.path.basename(os.path.dirname(p)))
    if not json_files:
        print(f"no dan.json under {folder}")
        return 1

    problems = 0
    print(f"{len(json_files)} course(s) under {folder}\n")
    for path in json_files:
        rel = os.path.relpath(os.path.dirname(path), folder)
        try:
            with open(path, encoding="utf-8-sig") as f:
                d = json.load(f)
        except json.JSONDecodeError as ex:
            print(f"[{rel}] FAIL json: {ex}")
            problems += 1
            continue
        if not isinstance(d.get("title"), str) or not isinstance(d.get("color"), int):
            print(f"[{rel}] FAIL missing required title/color — the loader would skip this course")
            problems += 1
            continue

        dan_index = d.get("dan_index")
        if not isinstance(dan_index, int):
            m = re.match(r"\d+", os.path.basename(os.path.dirname(path)))
            dan_index = int(m.group()) if m else -1
        if not 0 <= dan_index <= 24:
            print(f"[{rel}] WARN dan_index {dan_index} out of range → the loader drops it to -1")

        charts, total_notes, notes = [], 0, []
        for chart in d.get("charts", []):
            key = (game_norm(chart.get("title", "")), game_norm(chart.get("subtitle", "")))
            entry = index.get(key) or by_title.get(key[0])
            diff = chart.get("difficulty")
            if entry is None:
                print(f"[{rel}] FAIL song not found by title/subtitle: {chart.get('title')!r}")
                problems += 1
                continue
            if diff not in entry["courses"]:
                print(f"[{rel}] WARN {entry['TITLE']!r} has no course {diff} "
                      f"({DIFF_NAME.get(diff, '?')}) — the chart would come back empty")
                problems += 1
            notes.append(entry["notes"].get(diff, 0))
            total_notes += entry["notes"].get(diff, 0)
            charts.append(f"{entry['TITLE']} [{DIFF_NAME.get(diff, '?')}]")

        exams = []
        for raw in d.get("exams", []):
            ex = parse_exam(raw)
            if ex["type"] not in VALID_TYPES:
                print(f"[{rel}] WARN unknown exam type {ex['type']!r}")
                problems += 1
            elif ex["red"] == 0 and not ex["song_red"]:
                print(f"[{rel}] WARN exam {ex['type']} has no threshold")
                problems += 1
            fmt = ("[" + ", ".join(f"{r}/{g if g != GOLD_FULL else '-'}"
                                   for r, g in zip(ex["song_red"], ex["song_gold"])) + "]"
                   if ex["per_song"] else
                   f"{ex['red']}/{ex['gold'] if ex['gold'] != GOLD_FULL else '-'}")
            exams.append(f"{ex['type']} {ex['range']} {fmt}{' per-song' if ex['per_song'] else ''}")

        print(f"[{rel}]")
        print(f"  title {d['title']!r}  color {d['color']}  rank_art {d.get('rank_art')}  dan_index {dan_index}")
        for c in charts:
            print(f"  song  {c}")
        for e in exams:
            print(f"  exam  {e}")
        print(f"  total notes (game's counter would show) {total_notes}"
              f"   charts {len(charts)}/{len(d.get('charts', []))}\n")

    print("OK — every course loads" if not problems else f"{problems} problem(s) above")
    return 1 if problems else 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("folder", help="version-set folder holding the course subfolders")
    ap.add_argument("--lib", nargs="+", default=["./Songs"])
    args = ap.parse_args()
    library = scan_library([os.path.abspath(p) for p in args.lib])
    print(f"{len(library)} charts indexed from {', '.join(args.lib)}\n")
    return verify_folder(args.folder, library)


if __name__ == "__main__":
    sys.exit(main())