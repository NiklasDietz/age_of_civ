#!/usr/bin/env bash
# Live-debug command sender. Writes the supplied command line to
# /tmp/aoc_debug.cmd, waits up to 2 seconds for the game's reply
# in /tmp/aoc_debug.out, prints the reply, exits.
#
# Usage examples:
#   tools/aoc_debug.sh info
#   tools/aoc_debug.sh dump-plates /tmp/p.csv
#   tools/aoc_debug.sh dump-grid /tmp/g.txt
#   tools/aoc_debug.sh set-creator-time 1500
#   tools/aoc_debug.sh re-roll 42
#   tools/aoc_debug.sh quit
#
# Requires the game running with the debug command-file watcher
# enabled (default in current builds). The game polls
# /tmp/aoc_debug.cmd at frame rate, so reply latency is one frame
# (~16 ms at 60 fps).

set -euo pipefail

CMD_FILE=${AOC_DEBUG_CMD:-/tmp/aoc_debug.cmd}
OUT_FILE=${AOC_DEBUG_OUT:-/tmp/aoc_debug.out}
TIMEOUT_SEC=${AOC_DEBUG_TIMEOUT:-2}

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <verb> [args...]" >&2
    exit 2
fi

# Stale output from a prior round-trip?  Remove so we don't read it.
rm -f "$OUT_FILE"

# Submit command. Quoted args are joined with spaces.
printf '%s' "$*" > "$CMD_FILE"

# Wait for response.
deadline=$(( $(date +%s) + TIMEOUT_SEC ))
while [ ! -s "$OUT_FILE" ]; do
    if [ "$(date +%s)" -ge "$deadline" ]; then
        echo "{\"error\":\"timeout waiting for $OUT_FILE\"}" >&2
        exit 3
    fi
    sleep 0.02
done

cat "$OUT_FILE"
