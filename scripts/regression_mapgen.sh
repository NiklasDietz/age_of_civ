#!/usr/bin/env bash
# Golden map-generation regression: run aoc_mapgen at fixed seeds and compare
# the per-tile CSV and plate-stats dump against committed baselines.
#
# Map generation is fully deterministic for a given seed (aoc_mapgen forces
# single-thread OpenMP internally), so this comparison is EXACT -- any diff is a
# real behaviour change. The polar-erosion fix in WP-11 already changed map
# output intentionally, so baselines must be regenerated after that lands.
#
# Usage:
#   scripts/regression_mapgen.sh            compare against baselines
#   scripts/regression_mapgen.sh --update   (re)generate baselines, then stop
#
# Override the binary with AOC_MAPGEN=/path/to/aoc_mapgen.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/regression_lib.sh
source "${SCRIPT_DIR}/regression_lib.sh"

GOLDEN_DIR="${REPO_ROOT}/tests/golden/mapgen"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

# "<seed> <width> <height>"
SCENARIOS=(
    "789 140 90"
    "4242 100 60"
)

UPDATE=0
if [[ "${1:-}" == "--update" ]]; then
    UPDATE=1
fi

MAPGEN="$(find_tool aoc_mapgen AOC_MAPGEN)"

run_scenario() {
    local seed="$1" width="$2" height="$3" out_base="$4"
    "${MAPGEN}" --seed "${seed}" --width "${width}" --height "${height}" \
        --output "${out_base}" --format csv --dump-plates "${out_base}_plates.csv" \
        >/dev/null
}

if [[ "${UPDATE}" -eq 1 ]]; then
    mkdir -p "${GOLDEN_DIR}"
    for scenario in "${SCENARIOS[@]}"; do
        read -r seed width height <<<"${scenario}"
        echo "regression-mapgen: generating baseline seed=${seed} ${width}x${height}"
        run_scenario "${seed}" "${width}" "${height}" "${WORK_DIR}/map"
        cp "${WORK_DIR}/map.csv" "${GOLDEN_DIR}/seed${seed}.csv"
        cp "${WORK_DIR}/map_plates.csv" "${GOLDEN_DIR}/seed${seed}_plates.csv"
    done
    echo "regression-mapgen: baselines written to ${GOLDEN_DIR} (commit them)."
    exit 0
fi

if [[ ! -d "${GOLDEN_DIR}" ]]; then
    echo "regression-mapgen: no baselines in ${GOLDEN_DIR}. Run with --update first." >&2
    exit 2
fi

failed=0
for scenario in "${SCENARIOS[@]}"; do
    read -r seed width height <<<"${scenario}"
    echo "regression-mapgen: checking seed=${seed} ${width}x${height}"
    run_scenario "${seed}" "${width}" "${height}" "${WORK_DIR}/map"
    for name in "seed${seed}.csv" "seed${seed}_plates.csv"; do
        baseline="${GOLDEN_DIR}/${name}"
        current="${WORK_DIR}/map${name#seed${seed}}"
        if [[ ! -f "${baseline}" ]]; then
            echo "regression-mapgen: missing baseline ${baseline} (run --update)" >&2
            failed=1
            continue
        fi
        if ! diff -q "${baseline}" "${current}" >/dev/null; then
            echo "regression-mapgen: DRIFT in ${name}" >&2
            diff "${baseline}" "${current}" | head -10 >&2 || true
            failed=1
        fi
    done
done

if [[ "${failed}" -ne 0 ]]; then
    echo "regression-mapgen: FAILED" >&2
    exit 1
fi
echo "regression-mapgen: PASS"
