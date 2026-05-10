#!/usr/bin/env bash
# POST /sim/step?dy=N -- advance scrub by N My (default 50 = one epoch).
# Negative dy steps back. Pipe through analysis loop:
#   while tools/aoc_dbg/step.sh; do tools/aoc_dbg/info.sh; sleep 0.2; done
# (loop until creator hits 3 Gy cap; info call shows current time).
set -euo pipefail
HOST=${AOC_DEBUG_HOST:-127.0.0.1:9876}
DY=${1:-50}
curl -fsS -X POST "http://${HOST}/sim/step?dy=${DY}" | jq
