#!/usr/bin/env bash
# Stops the server started by start.sh: types "quit" into its console, then waits.
set -uo pipefail
if ! tmux has-session -t knightshift 2>/dev/null; then
    echo "not running"
    exit 0
fi
tmux send-keys -t knightshift "quit" Enter
for i in $(seq 1 20); do
    pgrep -x KnightShift.ex1 >/dev/null || break
    sleep 1
done
pkill -x KnightShift.ex1 2>/dev/null || true
tmux kill-session -t knightshift 2>/dev/null || true
echo "stopped"
