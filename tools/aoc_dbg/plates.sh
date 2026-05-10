#!/usr/bin/env bash
# GET /plates -- per-plate JSON array. Pretty-printed.
set -euo pipefail
HOST=${AOC_DEBUG_HOST:-127.0.0.1:9876}
curl -fsS "http://${HOST}/plates" | jq
