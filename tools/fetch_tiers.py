#!/usr/bin/env python3
"""Fetch taiko.wiki community clear-tier data and write a static JSON artifact.

Usage:
    python tools/fetch_tiers.py [output_path]

Default output: data/tier_data.json (relative to the repo root).

The game loads this file at startup and never calls the network at runtime.
Re-run this script when you want to refresh the committed tier list.
"""

from __future__ import annotations

import json
import os
import sys
import time
import urllib.parse
import urllib.request

DIFFCHART_URL = "https://taiko.wiki/api/v1/diffchart?type=clear&level=10"
SONG_BATCH_URL = "https://taiko.wiki/api/v1/song/no"
USER_AGENT = "YataiDON-tier-fetch/1.0"
CHUNK = 40


def get_json(url: str):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=60) as resp:
        return json.load(resp)


def match_keys(song: dict) -> list[str]:
    """Folder-name candidates that ESE charts may use for this song."""
    keys: list[str] = []
    for field in ("romaji", "titleEn", "title", "aliasEn"):
        val = song.get(field)
        if isinstance(val, str):
            val = val.strip()
            if val and val not in keys:
                keys.append(val)
    return keys


def primary_romaji(song: dict) -> str:
    keys = match_keys(song)
    return keys[0] if keys else ""


def fetch_songs(song_nos: list[str]) -> dict[str, dict]:
    by_no: dict[str, dict] = {}
    for i in range(0, len(song_nos), CHUNK):
        chunk = song_nos[i : i + CHUNK]
        qs = urllib.parse.urlencode({"songno": json.dumps(chunk, separators=(",", ":"))})
        url = f"{SONG_BATCH_URL}?{qs}"
        print(f"  songs {i + 1}-{i + len(chunk)}...", flush=True)
        try:
            data = get_json(url)
        except Exception as exc:
            print(f"  batch failed ({exc}), falling back to singles", flush=True)
            data = []
            for sn in chunk:
                try:
                    data.append(get_json(f"{SONG_BATCH_URL}/{sn}"))
                    time.sleep(0.05)
                except Exception as exc2:
                    print(f"  skip {sn}: {exc2}", flush=True)
        if isinstance(data, dict):
            if isinstance(data.get("songs"), list):
                data = data["songs"]
            elif isinstance(data.get("data"), list):
                data = data["data"]
            else:
                data = [data]
        for song in data:
            if not isinstance(song, dict):
                continue
            sn = str(song.get("songNo", song.get("songno", "")))
            if sn:
                by_no[sn] = song
        time.sleep(0.15)
    return by_no


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    default_out = os.path.join(here, "..", "data", "tier_data.json")
    out_path = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else default_out)

    print("Fetching diffchart...", flush=True)
    chart = get_json(DIFFCHART_URL)
    sections = chart.get("data", {}).get("sections", [])
    if not sections:
        print("ERROR: no sections in diffchart response", file=sys.stderr)
        return 1

    entries_raw: list[tuple[str, str, str, int]] = []
    song_nos: list[str] = []
    seen: set[str] = set()
    for sec in sections:
        tier = sec.get("name", "")
        for i, song in enumerate(sec.get("songs", [])):
            sn = str(song.get("songNo", ""))
            diff = song.get("difficulty", "oni") or "oni"
            order = int(song.get("order", i))
            entries_raw.append((tier, sn, diff, order))
            if sn and sn not in seen:
                seen.add(sn)
                song_nos.append(sn)

    print(f"Tiers: {[s.get('name') for s in sections]}")
    print(f"Unique songs: {len(song_nos)}, chart entries: {len(entries_raw)}", flush=True)

    by_no = fetch_songs(song_nos)
    print(f"Fetched song records: {len(by_no)}/{len(song_nos)}", flush=True)

    tiers = [s.get("name") for s in sections]
    entries = []
    missing = 0
    for tier, sn, diff, order in entries_raw:
        song = by_no.get(sn)
        if not song:
            missing += 1
            continue
        romaji = primary_romaji(song)
        keys = match_keys(song)
        if not romaji:
            missing += 1
            continue
        entries.append(
            {
                "romaji": romaji,
                "keys": keys,
                "difficulty": diff,
                "tier": tier,
                "order": order,
                "songNo": sn,
            }
        )

    payload = {
        "version": 1,
        "source": DIFFCHART_URL,
        "type": "clear",
        "level": 10,
        "tiers": tiers,
        "entries": entries,
    }

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, indent=2)
        f.write("\n")

    print(f"Wrote {out_path}: {len(entries)} entries ({missing} skipped)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
