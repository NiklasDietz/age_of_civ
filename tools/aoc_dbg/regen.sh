#!/usr/bin/env bash
# POST /sim/re-roll?seed=N -- new seed + regenerate.
# Usage: regen.sh <seed>
set -euo pipefail
HOST=${AOC_DEBUG_HOST:-127.0.0.1:9876}
if [ "$#" -lt 1 ]; then
    echo "usage: $0 <seed>" >&2
    exit 2
fi
curl -fsS -X POST "http://${HOST}/sim/re-roll?seed=$1" | jq
