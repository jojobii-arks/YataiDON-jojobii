#!/usr/bin/env bash
# Verify YataiDON pro screen Lua parses before a dev run.
# A broken edit shows as a BLANK pro screen (left column + backdrop still paint if
# the error is mid-function) and the engine does NOT always log it to latest.log,
# so check here first.
set -e
LUAC=/c/msys64/mingw64/bin/luac.exe
for f in Skins/YataiDONNijiiroLocal/Scripts/song_select/*.lua; do
  "$LUAC" -p "$f" && echo "ok: $f"
done
