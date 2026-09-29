#!/usr/bin/env bash
# Builds a slim, self-contained KnightShift dedicated-server pack FROM YOUR OWN game copy.
#
#   ./make-pack.sh /path/to/game [output-dir]
#
# /path/to/game = your legal KnightShift install (the folder with KnightShift.ex1 and WDFiles/).
# The pack contains the game's own files, so it is ONLY for your own use / your own server -
# do NOT redistribute it. This script does not download anything; it copies from your install
# and from this repo's build/ (run ../../build.sh first).
#
# Result: <output-dir>/KnightShift-Server/ (ready to run) and KnightShift-Server.zip.
# It drops the intro videos, music and speech (~0.5 GB) - a headless server needs none of them.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
GAME="${1:-}"
OUT="${2:-$REPO/dist}"
BUILD="$REPO/build"

if [[ -z "$GAME" || ! -f "$GAME/KnightShift.ex1" ]]; then
    echo "usage: ./make-pack.sh /path/to/game [output-dir]   (game dir must contain KnightShift.ex1)" >&2
    exit 1
fi
if [[ ! -f "$BUILD/dinput8.dll" ]]; then
    echo "build/dinput8.dll not found - run ../../build.sh first" >&2
    exit 1
fi
GAME="$(cd "$GAME" && pwd)"
DEST="$OUT/KnightShift-Server"
rm -rf "$DEST"
mkdir -p "$DEST/WDFiles"

echo ">> copying game executables"
for f in KnightShift.exe KnightShift.ex1 KnightShift.ex2 ijl10.dll KS.ico; do
    [[ -f "$GAME/$f" ]] && cp "$GAME/$f" "$DEST/"
done

echo ">> copying game data (without intro videos, music, speech)"
DROP="Video.wd Video2.wd Video3.wd Video2snd.wd Music.wd Speeches.wd"
for f in "$GAME"/WDFiles/*; do
    name="$(basename "$f")"
    skip=0
    for d in $DROP; do [[ "$name" == "$d" ]] && skip=1; done
    [[ $skip -eq 0 ]] && cp "$f" "$DEST/WDFiles/"
done

echo ">> copying server files (dinput8.dll, steam_api.dll) and config"
cp "$BUILD/dinput8.dll" "$BUILD/steam_api.dll" "$DEST/"
# ksnetfix.ini = KSNetFix defaults with [Steam] Enabled=0 + our [Server] section, CdKey left blank.
{
    awk 'BEGIN{s=0} /^\[Steam\]/{s=1} /^\[/{if($0!~/Steam/)s=0} {if(s && $0 ~ /^Enabled=/) print "Enabled=0\r"; else print}' "$REPO/KSNetFix/ksnetfix.ini"
    printf '\r\n'
    cat "$REPO/server.ini"
} > "$DEST/ksnetfix.ini"

echo ">> adding start scripts and instructions"
cp "$HERE/files/start.bat" "$HERE/files/stop.bat" "$HERE/files/INSTRUKCJA.txt" "$DEST/"
# Linux / Wine: graphics-mode helper next to the game, scripts in linux/ (LF line endings)
[[ -f "$BUILD/d3d8enum.exe" ]] && cp "$BUILD/d3d8enum.exe" "$DEST/"
mkdir -p "$DEST/linux"
for f in setup.sh run.sh start.sh stop.sh README.md knightshift-server.service; do
    tr -d '\r' < "$HERE/../linux/$f" > "$DEST/linux/$f"
done
chmod +x "$DEST"/linux/*.sh

echo ">> zipping"
( cd "$OUT" && rm -f KnightShift-Server.zip && /c/Windows/System32/tar.exe -a -c -f KnightShift-Server.zip KnightShift-Server )
SIZE="$(du -sh "$DEST" | cut -f1)"
echo ">> done: $DEST ($SIZE) and $OUT/KnightShift-Server.zip"
echo "   For your own use only - it contains game files. Enter your own CD key in ksnetfix.ini ([Server] CdKey=)."
