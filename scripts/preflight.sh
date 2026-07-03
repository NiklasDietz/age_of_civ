#!/usr/bin/env bash
# Preflight gate -- run before committing / pushing. Solo-dev stand-in for
# hosted CI: builds both presets, runs the full test suite (ASan debug +
# release incl. determinism/golden sim gates), and a seeded smoke run.
#
# Usage: scripts/preflight.sh [--fast]
#   --fast  skip the debug/ASan leg (release build + tests only)
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

FAST=0
if [[ "${1:-}" == "--fast" ]]; then FAST=1; fi

step() { printf '\n=== %s ===\n' "$*"; }

step "configure + build (release)"
cmake --preset release > /dev/null
cmake --build --preset release

step "ctest (release: unit + smoke + determinism + golden)"
ctest --test-dir build/release --output-on-failure

if [[ ${FAST} -eq 0 ]]; then
    step "configure + build (debug: ASan/UBSan)"
    cmake --preset debug > /dev/null
    cmake --build --preset debug

    step "ctest (debug/ASan: unit + smoke)"
    ctest --test-dir build/debug --output-on-failure
fi

step "seeded smoke (100 turns, 4 players)"
./build/release/aoc_simulate --turns 100 --players 4 --seed 42 \
    --output build/release/preflight_smoke.csv > build/release/preflight_smoke.log 2>&1

echo
echo "preflight: ALL GREEN"
