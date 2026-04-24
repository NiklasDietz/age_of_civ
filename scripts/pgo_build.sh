#!/usr/bin/env bash
# WP-F: two-stage PGO build pipeline.
#
# 1. Reconfigure + build instrumented binary.
# 2. Run a training simulation to collect profile data.
# 3. Reconfigure + rebuild using the collected profiles.
#
# Before + after runtime timings for a fixed sim are printed. The
# training run and the after-run share the same flags (other than PGO)
# so timings are comparable.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
TURNS="${TURNS:-200}"
PLAYERS="${PLAYERS:-6}"
SEED="${SEED:-1}"

have_cmd() { command -v "$1" >/dev/null 2>&1; }
timed_run() {
  local label="$1"; shift
  local start=$(date +%s.%N)
  "$@" >/dev/null 2>&1
  local rc=$?
  local end=$(date +%s.%N)
  printf '%-16s rc=%d  elapsed=%.2fs\n' "${label}" "${rc}" \
    "$(awk -v a="${end}" -v b="${start}" 'BEGIN{print a-b}')"
  return "${rc}"
}

echo "=== WP-F PGO build pipeline ==="
echo "repo: ${REPO_ROOT}"
echo "turns=${TURNS} players=${PLAYERS} seed=${SEED}"
echo

# --- baseline timing (no PGO, fresh release build) ---
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DAOC_PGO_PHASE=off >/dev/null
cmake --build "${BUILD_DIR}" -j >/dev/null
timed_run "baseline" "${BUILD_DIR}/aoc_simulate" \
  --turns "${TURNS}" --players "${PLAYERS}" --seed "${SEED}"

# --- stage 1: instrumented build + training run ---
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DAOC_PGO_PHASE=generate >/dev/null
cmake --build "${BUILD_DIR}" -j >/dev/null
timed_run "pgo-train" "${BUILD_DIR}/aoc_simulate" \
  --turns "${TURNS}" --players "${PLAYERS}" --seed "${SEED}"

# --- stage 2: optimized build using profiles ---
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DAOC_PGO_PHASE=use >/dev/null
cmake --build "${BUILD_DIR}" -j >/dev/null
timed_run "pgo-optimized" "${BUILD_DIR}/aoc_simulate" \
  --turns "${TURNS}" --players "${PLAYERS}" --seed "${SEED}"

echo
echo "PGO pipeline complete."
