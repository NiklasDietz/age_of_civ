#!/usr/bin/env bash
# GET /schema -- list every route exposed by the running game.
set -euo pipefail
HOST=${AOC_DEBUG_HOST:-127.0.0.1:9876}
curl -fsS "http://${HOST}/schema" | jq
