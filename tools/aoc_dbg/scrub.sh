#!/usr/bin/env bash
# POST /sim/set-creator-time?my=N -- jump to specific epoch.
# Usage: scrub.sh <my>
set -euo pipefail
HOST=${AOC_DEBUG_HOST:-127.0.0.1:9876}
if [ "$#" -lt 1 ]; then
    echo "usage: $0 <my>" >&2
    exit 2
fi
curl -fsS -X POST "http://${HOST}/sim/set-creator-time?my=$1" | jq
