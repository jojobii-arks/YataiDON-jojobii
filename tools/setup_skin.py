#!/usr/bin/env python3
"""Assemble a runnable overlay skin for this fork.

The fork's own skin work lives in ``Skins/YataiDONNijiiro-jojobii`` — a small
overlay whose ``Graphics/skin_config.json`` declares a ``"parent"`` skin. The
parent (``Skins/YataiDONNijiiro``) supplies every texture, font and sound, and it
is *not* in this repository: it is a git submodule on an external host, and its
assets cannot be redistributed here.

So a fresh clone is missing two things:

1. The parent skins themselves — fetch with ``git submodule update --init``.
2. The overlay's *derived* assets (``tools/skin-assets.txt``) — files the overlay
   declares but does not own. Each one is byte-identical to the parent's copy at
   the same relative path, so they are copied in rather than tracked.

Usage::

    python tools/setup_skin.py                 # fetch parents + copy derived assets
    python tools/setup_skin.py --check         # report what's missing, change nothing
    python tools/setup_skin.py --engine-only   # skip the submodule fetch (offline)

Exit codes: 0 = the skin is assembled, 1 = still incomplete (see the report).
"""
from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

OVERLAY = Path("Skins/YataiDONNijiiro-jojobii")
PARENT = Path("Skins/YataiDONNijiiro")
MANIFEST = Path("tools/skin-assets.txt")
PARENTS = ("Skins/PyTaikoGreen", "Skins/YataiDONRed", "Skins/YataiDONNijiiro")
SKIN_NAME = OVERLAY.name


def repo_root() -> Path:
    """The repo this script lives in (tools/ is one level down)."""
    return Path(__file__).resolve().parent.parent


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def identical(a: Path, b: Path) -> bool:
    return a.stat().st_size == b.stat().st_size and digest(a) == digest(b)


def load_manifest(root: Path) -> list[str]:
    """Paths under the overlay that are derived; a trailing slash means a dir."""
    path = root / MANIFEST
    if not path.is_file():
        sys.exit(f"error: manifest missing: {MANIFEST}")
    entries = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if line:
            entries.append(line)
    return entries


def expand(root: Path, entry: str) -> list[tuple[Path, Path]]:
    """[(source, destination)] for one manifest entry, relative to the overlay."""
    src = root / PARENT / entry.rstrip("/")
    dst = root / OVERLAY / entry.rstrip("/")
    if entry.endswith("/"):
        if not src.is_dir():
            return []
        out = []
        for f in sorted(src.rglob("*")):
            if f.is_file():
                out.append((f, dst / f.relative_to(src)))
        return out
    return [(src, dst)]


def parent_present(root: Path) -> bool:
    """A submodule that is initialized has content; an empty dir means un-fetched."""
    d = root / PARENT
    return d.is_dir() and any(d.iterdir())


def fetch_parents(root: Path, check_only: bool) -> bool:
    """git submodule update --init for the three skin submodules. True on success."""
    if parent_present(root):
        print(f"[ok]      parents present: {PARENT}")
        return True
    if check_only:
        print(f"[missing] parents not fetched: {PARENT} (run: git submodule update --init)")
        return False
    print(f"[fetch]   git submodule update --init {' '.join(PARENTS)}")
    proc = subprocess.run(
        ["git", "submodule", "update", "--init", *PARENTS],
        cwd=root,
        text=True,
    )
    if proc.returncode != 0:
        print("[error]   submodule fetch failed — the host may be unreachable.")
        print("          The engine still builds; only Nijiiro-based skins need it.")
        return False
    ok = parent_present(root)
    print(f"[{'ok' if ok else 'error'}]      parents {'present' if ok else 'still empty'}")
    return ok


def copy_derived(root: Path, check_only: bool, force: bool) -> bool:
    entries = load_manifest(root)
    pairs: list[tuple[Path, Path]] = []
    for entry in entries:
        pairs.extend(expand(root, entry))

    copied = skipped = missing = 0
    for src, dst in pairs:
        rel = dst.relative_to(root / OVERLAY).as_posix()
        if not src.is_file():
            print(f"[missing] source not in the parent: {src.relative_to(root).as_posix()}")
            missing += 1
            continue
        if dst.is_file() and not force and identical(src, dst):
            skipped += 1
            continue
        if check_only:
            print(f"[missing] {rel}")
            copied += 1
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        copied += 1

    if skip := skipped:
        print(f"[ok]      {skip} derived asset(s) already in place")
    if copied:
        verb = "to copy" if check_only else "copied"
        print(f"[{'missing' if check_only else 'copy'}]   {copied} derived asset(s) {verb}")
    if missing:
        print("[error]   the parent is incomplete — is the submodule fully checked out?")
    return missing == 0 and not (check_only and copied)


def ensure_skin_line(root: Path, check_only: bool) -> bool:
    """Point config.toml at this skin.

    config.toml is gitignored — the game writes it on first run — so a fresh
    clone has none, and the game would boot on its default skin instead of the
    overlay. Every key the loader reads goes through ``value_or(default)``, so a
    minimal file carrying only the skin line is valid and the game fills in the
    rest (and rewrites the full file the first time it saves).
    """
    cfg = root / "config.toml"
    if not cfg.is_file():
        if check_only:
            print(f"[missing] no config.toml — the overlay would not be selected (skin = '{SKIN_NAME}')")
            return False
        cfg.write_text(
            f"[paths]\nskin = '{SKIN_NAME}'\ntja_path = [ 'Songs' ]\n",
            encoding="utf-8",
            newline="",
        )
        print(f"[ok]      wrote a minimal config.toml selecting '{SKIN_NAME}'")
        print("          tja_path points at this repo's own Songs/ folder —")
        print("          set it to your own library to play your charts.")
        print("          (the game fills in every other key from its defaults)")
        return True

    text = cfg.read_text(encoding="utf-8", errors="surrogateescape")
    newline = "\r\n" if "\r\n" in text else "\n"
    target = f"skin = '{SKIN_NAME}'"
    hit = False
    out = []
    in_paths = False
    for line in text.splitlines(keepends=True):
        bare = line.rstrip("\r\n")
        if bare.strip().startswith("["):
            in_paths = bare.strip() == "[paths]"
        if in_paths and bare.strip().startswith("skin") and "=" in bare:
            hit = True
            if bare == target:
                out.append(line)
                continue
            if check_only:
                print(f"[missing] config.toml [paths] skin is {bare.strip()!r}, want {target!r}")
                return False
            out.append(target + newline)
            continue
        out.append(line)

    if not hit:
        print(f"[note]    config.toml has no [paths] skin line — set it to '{SKIN_NAME}'")
        return True
    new = "".join(out)
    if new != text and not check_only:
        cfg.write_text(new, encoding="utf-8", errors="surrogateescape", newline="")
        print(f"[ok]      config.toml [paths] skin = '{SKIN_NAME}'")
    else:
        print(f"[ok]      config.toml already selects '{SKIN_NAME}'")
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="report only, change nothing")
    ap.add_argument("--engine-only", action="store_true", help="skip fetching parent skins")
    ap.add_argument("--force", action="store_true", help="re-copy even when identical")
    args = ap.parse_args()

    root = repo_root()
    print(f"repo: {root}\nskin: {OVERLAY}\n")

    if not (root / OVERLAY / "Graphics" / "skin_config.json").is_file():
        print(f"error: {OVERLAY}/Graphics/skin_config.json not found — wrong working tree?")
        return 1

    parents_ok = True
    if args.engine_only:
        print("[skip]    parent fetch (--engine-only)")
        parents_ok = parent_present(root)
        if not parents_ok:
            print("[note]    without the parents this skin cannot load, but the engine runs.")
    else:
        parents_ok = fetch_parents(root, args.check)

    assets_ok = copy_derived(root, args.check, args.force) if parents_ok else False
    cfg_ok = ensure_skin_line(root, args.check) if parents_ok else True

    ok = parents_ok and assets_ok and cfg_ok
    print("\n" + ("skin assembled." if ok else "INCOMPLETE — see the lines above."))
    if ok and not args.check:
        print("Launch the game and the pro song select should be on the main menu.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
