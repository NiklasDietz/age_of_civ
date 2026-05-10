#!/usr/bin/env bash
# Walk through the creator sim epoch-by-epoch, dumping a snapshot at
# each step. Use case: capture per-epoch state across the full 3 Gy
# run so I can diff successive epochs and pinpoint which mechanism
# changed what. Defaults: from creator's current time to 3000 My in
# 50 My epochs, snapshot directories under /tmp/aoc_dbg/walk-<seq>/.
#
# Usage: walk.sh [start_my] [end_my] [step_my]
#        defaults: start = current /info creatorTime, end = 3000, step = 50
set -euo pipefail
HOST=${AOC_DEBUG_HOST:-127.0.0.1:9876}
START=${1:-}
END=${2:-3000}
STEP=${3:-50}

if [ -z "${START}" ]; then
    START=$(curl -fsS "http://${HOST}/info" | jq -r '.creatorTime')
fi

i=0
t="${START}"
while [ "${t}" -le "${END}" ]; do
    label=$(printf "walk-%03d-%dmy" "${i}" "${t}")
    curl -fsS -X POST "http://${HOST}/sim/set-creator-time?my=${t}" >/dev/null
    "$(dirname "$0")/dump.sh" "${label}" >/dev/null
    echo "${label}"
    i=$((i + 1))
    t=$((t + STEP))
done
