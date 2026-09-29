#!/usr/bin/env bash
# Starts the server in the background, in a tmux session - the console stays reachable:
#   ./start.sh /path/to/game       start
#   tmux attach -t knightshift     open the server console (Ctrl+B then D leaves it running)
#   ./stop.sh                      stop cleanly
set -euo pipefail
GAMEDIR="${1:?usage: ./start.sh /path/to/game}"
GAMEDIR="$(cd "$GAMEDIR" && pwd)"
HERE="$(cd "$(dirname "$0")" && pwd)"
command -v tmux >/dev/null || { echo "missing: tmux (apt install tmux)" >&2; exit 1; }
if tmux has-session -t knightshift 2>/dev/null; then
    echo "already running - tmux attach -t knightshift" >&2
    exit 1
fi
tmux new-session -d -s knightshift "\"$HERE/run.sh\" \"$GAMEDIR\"; echo 'server stopped - Enter closes'; read"
echo "server started. Console: tmux attach -t knightshift   (leave it with Ctrl+B, D)"
echo "log: $GAMEDIR/ksnetfix.log"
