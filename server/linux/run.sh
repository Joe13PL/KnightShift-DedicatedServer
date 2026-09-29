#!/usr/bin/env bash
# Runs the KnightShift dedicated server on Linux under Wine, headless (Xvfb).
# The game window never appears; the live log and the server commands (status, start,
# end, pause, resume, show, hide, quit) are on this terminal - type a command and Enter.
#
#   ./run.sh /path/to/game
#
# Run ./setup.sh once first and put your CD key into ksnetfix.ini ([Server] CdKey=).
# Ctrl+C stops the server cleanly.
set -euo pipefail

GAMEDIR="${1:-}"
if [[ -z "$GAMEDIR" || ! -f "$GAMEDIR/KnightShift.ex1" ]]; then
    echo "usage: ./run.sh /path/to/game" >&2
    exit 1
fi
GAMEDIR="$(cd "$GAMEDIR" && pwd)"
export WINEPREFIX="${WINEPREFIX:-$HOME/.knightshift-server}"
export WINEARCH=win32
export WINEDEBUG="${WINEDEBUG:--all}"
export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
# Load our dinput8.dll (the server) from the game dir instead of Wine's builtin.
export WINEDLLOVERRIDES="dinput8=n,b;${WINEDLLOVERRIDES:-}"

if [[ ! -f "$WINEPREFIX/ks-renderer" ]]; then
    echo "prefix $WINEPREFIX not set up - run ./setup.sh $GAMEDIR first" >&2
    exit 1
fi
# Graphics mode for the game (it would otherwise start its Config.exe GUI and quit).
RENDERER="${KS_RENDERER:-$(cat "$WINEPREFIX/ks-renderer")}"

DISPLAYNUM="${KS_DISPLAY:-99}"
Xvfb ":$DISPLAYNUM" -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 &
XVFB_PID=$!
trap 'kill $XVFB_PID 2>/dev/null; wineserver -k 2>/dev/null || true' EXIT
export DISPLAY=":$DISPLAYNUM"
sleep 1

cd "$GAMEDIR"
# ex1 is started directly (not through KnightShift.exe) so this terminal's stdin/stdout
# reach the server console. Software OpenGL renders the start-up frames, then
# Render=startup stops drawing.
wine KnightShift.ex1 "$RENDERER"
