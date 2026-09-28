#!/usr/bin/env bash
# Runs the KnightShift dedicated server on Linux under Wine, headless (Xvfb).
# The game window never appears; the live log and the server commands (status, start,
# end, pause, resume, show, hide, quit) are on this terminal - type a command and Enter.
#
#   ./run.sh /path/to/game
#
# Run ./setup.sh once first. Set [Server] in the game dir's ksnetfix.ini (Console=auto or
# stdio, Window=hidden, Render=startup). Ctrl+C stops the server cleanly.
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
# Load our dinput8.dll (the server) from the game dir instead of Wine's builtin.
export WINEDLLOVERRIDES="dinput8=n,b;${WINEDLLOVERRIDES:-}"

if [[ ! -d "$WINEPREFIX" ]]; then
    echo "prefix $WINEPREFIX not found - run ./setup.sh $GAMEDIR first" >&2
    exit 1
fi

cd "$GAMEDIR"
# ex1 is launched directly (not through KnightShift.exe) so this terminal's stdin/stdout
# reach the server console. A virtual X display (Xvfb) is enough - software GL renders the
# few start-up frames, then Render=startup stops drawing.
if command -v xvfb-run >/dev/null; then
    exec xvfb-run -a -s "-screen 0 1024x768x24" wine KnightShift.ex1
elif command -v Xvfb >/dev/null; then
    Xvfb :99 -screen 0 1024x768x24 >/dev/null 2>&1 &
    XVFB_PID=$!
    trap 'kill $XVFB_PID 2>/dev/null' EXIT
    export DISPLAY=:99
    exec wine KnightShift.ex1
else
    echo "Xvfb not found (apt install xvfb). Running against \$DISPLAY=$DISPLAY" >&2
    exec wine KnightShift.ex1
fi
