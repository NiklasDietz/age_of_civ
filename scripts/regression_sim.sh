#!/usr/bin/env bash
# Golden sim-replay regression: run aoc_simulate at fixed seeds and compare the
# three output CSVs against committed baselines. Catches behaviour drift from
# refactors / perf changes.
#
# Usage:
#   scripts/regression_sim.sh            compare against baselines (fails on drift)
#   scripts/regression_sim.sh --update   (re)generate the baselines, then stop
#
# Override the binary with AOC_SIMULATE=/path/to/aoc_simulate.
# Tune float tolerance with ABS_TOL / REL_TOL env vars (see csv_diff.py).
#
# Determinism: xoshiro256** RNG is seeded from --seed; OMP_NUM_THREADS is
# pinned to 1 by regression_lib.sh. The headless loop must be run on a single
# build/toolchain for baselines to match (float results are toolchain-sensitive).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/regression_lib.sh
source "${SCRIPT_DIR}/regression_lib.sh"

GOLDEN_DIR="${REPO_ROOT}/tests/golden/sim"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

ABS_TOL="${ABS_TOL:-1e-3}"
REL_TOL="${REL_TOL:-1e-5}"

# One scenario per line: "<seed> <turns> <players>". Keep these stable; changing
# a scenario invalidates its baseline.
SCENARIOS=(
    "12345 200 8"
    "6789 200 4"
    "2024 120 6"
)

UPDATE=0
if [[ "${1:-}" == "--update" ]]; then
    UPDATE=1
fi

SIMULATE="$(find_tool aoc_simulate AOC_SIMULATE)"
DIFF_PY="${SCRIPT_DIR}/csv_diff.py"

run_scenario() {
    local seed="$1" turns="$2" players="$3" out_base="$4"
    "${SIMULATE}" --seed "${seed}" --turns "${turns}" --players "${players}" \
        --output "${out_base}.csv" >/dev/null
}

# Suffixes produced by aoc_simulate for a given --output base "X.csv".
suffixes() {
    printf '%s\n' ".csv" "_events.csv" "_tiles.csv"
}

if [[ "${UPDATE}" -eq 1 ]]; then
    mkdir -p "${GOLDEN_DIR}"
    for scenario in "${SCENARIOS[@]}"; do
        read -r seed turns players <<<"${scenario}"
        echo "regression-sim: generating baseline seed=${seed} turns=${turns} players=${players}"
        run_scenario "${seed}" "${turns}" "${players}" "${WORK_DIR}/sim"
        for suffix in $(suffixes); do
            cp "${WORK_DIR}/sim${suffix}" "${GOLDEN_DIR}/seed${seed}${suffix}"
        done
    done
    echo "regression-sim: baselines written to ${GOLDEN_DIR} (commit them)."
    exit 0
fi

if [[ ! -d "${GOLDEN_DIR}" ]]; then
    echo "regression-sim: no baselines in ${GOLDEN_DIR}. Run with --update first." >&2
    exit 2
fi

failed=0
for scenario in "${SCENARIOS[@]}"; do
    read -r seed turns players <<<"${scenario}"
    echo "regression-sim: checking seed=${seed} turns=${turns} players=${players}"
    run_scenario "${seed}" "${turns}" "${players}" "${WORK_DIR}/sim"
    for suffix in $(suffixes); do
        baseline="${GOLDEN_DIR}/seed${seed}${suffix}"
        current="${WORK_DIR}/sim${suffix}"
        if [[ ! -f "${baseline}" ]]; then
            echo "regression-sim: missing baseline ${baseline} (run --update)" >&2
            failed=1
            continue
        fi
        if ! python3 "${DIFF_PY}" "${baseline}" "${current}" \
                --abs-tol "${ABS_TOL}" --rel-tol "${REL_TOL}"; then
            echo "regression-sim: DRIFT in seed${seed}${suffix}" >&2
            failed=1
        fi
    done
done

if [[ "${failed}" -ne 0 ]]; then
    echo "regression-sim: FAILED" >&2
    exit 1
fi
echo "regression-sim: PASS"
