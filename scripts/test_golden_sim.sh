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
# Usage: test_golden_sim.sh <aoc_simulate-binary> <output-dir> <golden-file> [--bless]
set -euo pipefail

SIM="$1"
OUTDIR="$2"
GOLDEN_FILE="$3"
BLESS="${4:-}"
SEED=42
TURNS=500
PLAYERS=4

"${SIM}" --turns ${TURNS} --players ${PLAYERS} --seed ${SEED} \
    --output "${OUTDIR}/golden_run.csv" > "${OUTDIR}/golden_run.log" 2>&1

actual=$(cat "${OUTDIR}/golden_run.csv" \
             "${OUTDIR}/golden_run_events.csv" \
             "${OUTDIR}/golden_run_tiles.csv" | sha256sum | cut -d' ' -f1)

if [[ "${BLESS}" == "--bless" ]]; then
    echo "${actual}" > "${GOLDEN_FILE}"
    echo "golden: blessed ${actual}"
    exit 0
fi

if [[ ! -f "${GOLDEN_FILE}" ]]; then
    echo "golden: no blessed hash at ${GOLDEN_FILE} (run with --bless)" >&2
    exit 1
fi

expected=$(cat "${GOLDEN_FILE}")
if [[ "${actual}" != "${expected}" ]]; then
    echo "golden: MISMATCH" >&2
    echo "  expected ${expected}" >&2
    echo "  actual   ${actual}" >&2
    echo "  If this behavior change is intentional, re-bless and explain in the commit." >&2
    exit 1
fi
echo "golden: OK (${actual})"
