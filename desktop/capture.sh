#!/usr/bin/env bash
# Runs the desktop build and captures its window: a screenshot for looking at the UI without a console.
# The window appears on the desktop for a couple of seconds while it is captured.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/scratch"
CARD="$OUT/card"
SHOT="${1:-$OUT/shot.png}"
WAIT="${2:-5}"

cd "$CARD"
"$OUT/horizon-launcher" >"$OUT/run.log" 2>&1 &
PID=$!

sleep "$WAIT"

ID="$(xwininfo -root -tree 2>/dev/null | grep -i 'horizon launcher' | head -1 | awk '{print $1}')"
if [ -n "${ID:-}" ]; then
    timeout 10 import -window "$ID" "$SHOT" 2>/dev/null || echo "capture failed"
else
    echo "no window found"
fi

kill "$PID" 2>/dev/null
wait "$PID" 2>/dev/null
echo "==> $SHOT"
tail -4 "$CARD/sdmc:/switch/horizon/launcher-log.txt" 2>/dev/null
