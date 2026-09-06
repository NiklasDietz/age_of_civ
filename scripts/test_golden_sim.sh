#!/usr/bin/env bash
# Golden state-regression gate: a seed-42 500-turn sim must hash to the
# blessed value. Unlike the determinism test (self-comparing, never
# re-blessed), this test pins ABSOLUTE behavior: any commit that changes
# sim results turns it red and must consciously re-bless with:
#
#   scripts/test_golden_sim.sh <sim> <outdir> <golden-file> --bless
#
# and state the reason in the commit message.
#
# NOTE: hashes are machine-local (release builds use -march=native, float
# results may differ across CPUs). Re-bless once when switching machines.
#
# Usage: test_golden_sim.sh <aoc_simulate-binary> <output-dir> <golden-file> [--bless] [seed]
#
# The seed defaults to 42. A second blessed seed exists because a single one
# hides divergence: a balance change repeatedly moved the conquest count on one
# seed and left the others alone, which a lone golden cannot show.
set -euo pipefail

SIM="$1"
OUTDIR="$2"
GOLDEN_FILE="$3"
BLESS="${4:-}"
SEED="${5:-42}"
TURNS=500
PLAYERS=4

# Per-seed output names so two golden tests can run side by side.
RUN="${OUTDIR}/golden_run_seed${SEED}"

"${SIM}" --turns ${TURNS} --players ${PLAYERS} --seed ${SEED} \
    --output "${RUN}.csv" > "${RUN}.log" 2>&1

actual=$(cat "${RUN}.csv" \
             "${RUN}_events.csv" \
             "${RUN}_tiles.csv" | sha256sum | cut -d' ' -f1)

if [[ "${BLESS}" == "--bless" ]]; then
    echo "${actual}" > "${GOLDEN_FILE}"
    echo "golden: blessed seed ${SEED}: ${actual}"
    exit 0
fi

if [[ ! -f "${GOLDEN_FILE}" ]]; then
    echo "golden: no blessed hash at ${GOLDEN_FILE} (run with --bless)" >&2
    exit 1
fi

expected=$(cat "${GOLDEN_FILE}")
if [[ "${actual}" != "${expected}" ]]; then
    echo "golden: MISMATCH (seed ${SEED})" >&2
    echo "  expected ${expected}" >&2
    echo "  actual   ${actual}" >&2
    echo "  If this behavior change is intentional, re-bless and explain in the commit." >&2
    exit 1
fi
echo "golden: OK (seed ${SEED}: ${actual})"
