#!/usr/bin/env bash
# Wall-clock micro-benchmark for the perf work. Runs a fixed sim workload and a
# fixed map-gen workload N times and reports the median elapsed seconds. Pin the
# machine (no other load) and run before and after a change to get a delta.
#
# Usage:
#   scripts/bench.sh [runs]      default runs=5
#
# Override binaries with AOC_SIMULATE / AOC_MAPGEN.
# Sim runs serial; map gen forces single-thread internally, so set
# OMP threads for the sim part via OMP_NUM_THREADS if you want to measure the
# parallel map-gen passes specifically (bench pins 1 by default for stability).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/regression_lib.sh
source "${SCRIPT_DIR}/regression_lib.sh"

RUNS="${1:-5}"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

SIMULATE="$(find_tool aoc_simulate AOC_SIMULATE)"
MAPGEN="$(find_tool aoc_mapgen AOC_MAPGEN)"

# median <values...>  -> prints median to stdout
median() {
    local sorted
    mapfile -t sorted < <(printf '%s\n' "$@" | sort -n)
    local n=${#sorted[@]}
    echo "${sorted[$((n / 2))]}"
}

elapsed_seconds() {
    # Run "$@" and print elapsed wall-clock seconds with millisecond precision.
    local start end
    start="$(date +%s.%N)"
    "$@" >/dev/null 2>&1
    end="$(date +%s.%N)"
    awk -v a="${start}" -v b="${end}" 'BEGIN { printf "%.3f", b - a }'
}

echo "bench: ${RUNS} runs each (median reported), OMP_NUM_THREADS=${OMP_NUM_THREADS}"

sim_times=()
for ((i = 0; i < RUNS; i++)); do
    sim_times+=("$(elapsed_seconds "${SIMULATE}" --seed 123 --turns 500 --players 8 \
        --output "${WORK_DIR}/bench.csv")")
done
echo "bench: aoc_simulate  500 turns / 8 players  median = $(median "${sim_times[@]}") s"

mapgen_times=()
for ((i = 0; i < RUNS; i++)); do
    mapgen_times+=("$(elapsed_seconds "${MAPGEN}" --seed 123 --width 200 --height 120 \
        --output "${WORK_DIR}/benchmap" --format csv)")
done
echo "bench: aoc_mapgen    200x120                median = $(median "${mapgen_times[@]}") s"
