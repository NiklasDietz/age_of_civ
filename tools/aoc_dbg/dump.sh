#!/usr/bin/env bash
# Bulk dump: writes plates.csv + grid.txt under /tmp/aoc_dbg/<label>/
# and prints the directory path. Usage:
#   dump.sh                    -> label = current timestamp
#   dump.sh before-fix         -> label = before-fix
set -euo pipefail
HOST=${AOC_DEBUG_HOST:-127.0.0.1:9876}
LABEL=${1:-$(date +%Y%m%d-%H%M%S)}
DIR="/tmp/aoc_dbg/${LABEL}"
mkdir -p "${DIR}"
curl -fsS -X POST "http://${HOST}/dump/plates?path=${DIR}/plates.csv" >/dev/null
curl -fsS -X POST "http://${HOST}/dump/grid?path=${DIR}/grid.txt"     >/dev/null
curl -fsS "http://${HOST}/info" > "${DIR}/info.json"
echo "${DIR}"
