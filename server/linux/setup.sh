#!/usr/bin/env bash
# One-time Wine prefix setup for the KnightShift dedicated server on Linux.
#
#   ./setup.sh /path/to/game
#
# /path/to/game is a directory that holds YOUR OWN copy of the game plus the server
# build, i.e. these files (see server/linux/README.md for how to get them):
#     KnightShift.exe  KnightShift.ex1  KnightShift.ex2  ijl10.dll
#     dinput8.dll  steam_api.dll  ksnetfix.ini        (dinput8.dll = build/ of this repo)
#     WDFiles/ ...  (the game data; a slim server set is enough - see README)
#
# Needs: wine (32-bit support), winetricks, xvfb. Nothing here downloads game files.
set -euo pipefail

GAMEDIR="${1:-}"
if [[ -z "$GAMEDIR" || ! -f "$GAMEDIR/KnightShift.ex1" ]]; then
    echo "usage: ./setup.sh /path/to/game   (must contain KnightShift.ex1)" >&2
    exit 1
fi
GAMEDIR="$(cd "$GAMEDIR" && pwd)"
export WINEPREFIX="${WINEPREFIX:-$HOME/.knightshift-server}"
export WINEARCH=win32          # 32-bit game -> 32-bit prefix (keys land under SOFTWARE\Reality Pump)
export WINEDEBUG="${WINEDEBUG:--all}"

for tool in wine winetricks; do
    command -v "$tool" >/dev/null || { echo "missing: $tool" >&2; exit 1; }
done
command -v Xvfb >/dev/null || command -v xvfb-run >/dev/null || echo "note: Xvfb not found - install it before running run.sh" >&2

echo ">> creating Wine prefix: $WINEPREFIX"
wineboot --init >/dev/null 2>&1 || true
wineserver -w

echo ">> installing fonts (corefonts) - needed for the CD-key / text screens"
winetricks -q corefonts >/dev/null 2>&1 || echo "   (corefonts failed - install manually if text is missing)"

WINPATH="$(winepath -w "$GAMEDIR" 2>/dev/null || echo "$GAMEDIR")"
REG_ESCAPED="${WINPATH//\\/\\\\}"        # double backslashes for .reg
TMPREG="$(mktemp --suffix=.reg)"
sed "s|@GAMEDIR@|${REG_ESCAPED}|g" "$(dirname "$0")/knightshift.reg.template" > "$TMPREG"
echo ">> importing registry keys for game dir: $WINPATH"
wine regedit "$TMPREG" >/dev/null 2>&1
wineserver -w
rm -f "$TMPREG"

echo ">> done. Start the server with:"
echo "     WINEPREFIX=$WINEPREFIX ./run.sh $GAMEDIR"
