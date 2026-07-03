#!/usr/bin/env bash
# Determinism gate: run aoc_simulate twice with the same seed and distinct
# output paths, then byte-compare all three CSVs (main, _events, _tiles).
#
# This test is self-comparing and is NEVER re-blessed: a failure always
# means hidden entropy in the sim (pointer hashing, unordered iteration
# feeding decisions, random_device leakage, uninitialized reads).
#
# Usage: test_determinism.sh <aoc_simulate-binary> <output-dir>
set -euo pipefail

SIM="$1"
OUTDIR="$2"
SEED=777
TURNS=60
PLAYERS=4

rm -f "${OUTDIR}"/det_a*.csv "${OUTDIR}"/det_b*.csv

"${SIM}" --turns ${TURNS} --players ${PLAYERS} --seed ${SEED} \
    --output "${OUTDIR}/det_a.csv" > "${OUTDIR}/det_a.log" 2>&1
"${SIM}" --turns ${TURNS} --players ${PLAYERS} --seed ${SEED} \
    --output "${OUTDIR}/det_b.csv" > "${OUTDIR}/det_b.log" 2>&1

status=0
for suffix in "" "_events" "_tiles"; do
    a="${OUTDIR}/det_a${suffix}.csv"
    b="${OUTDIR}/det_b${suffix}.csv"
    if [[ ! -f "${a}" || ! -f "${b}" ]]; then
        echo "determinism: MISSING OUTPUT ${a} / ${b}" >&2
        status=1
        continue
    fi
    if ! cmp -s "${a}" "${b}"; then
        echo "determinism: DIVERGENCE in det_x${suffix}.csv (same seed, different output)" >&2
        status=1
    fi
done

if [[ ${status} -ne 0 ]]; then
    exit 1
fi
echo "determinism: OK (seed ${SEED}, ${TURNS} turns, ${PLAYERS} players)"
