#!/usr/bin/env bash
# GET /info -- summary state snapshot.
set -euo pipefail
HOST=${AOC_DEBUG_HOST:-127.0.0.1:9876}
curl -fsS "http://${HOST}/info" | jq
