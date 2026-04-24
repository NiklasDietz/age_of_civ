#!/usr/bin/env bash
# WP-E2 smoke test: 5-seed 500-turn headless batch + recipe-fire audit.
# Usage: scripts/smoke_recipes.sh [turns] [players]
#   turns   default 500
#   players default 6
#
# Prints:
#   - per-seed exit status
#   - "recipe_first_fire" count per seed (unique recipes that executed at
#     least once)
#   - unified count across all seeds (how many of the ~60 recipes ever fire)
#
# Fails (exit 1) if any seed crashes or if unified recipe count drops below
# a regression threshold (default 40, override with RECIPE_THRESHOLD env).

set -euo pipefail

TURNS="${1:-500}"
PLAYERS="${2:-6}"
RECIPE_THRESHOLD="${RECIPE_THRESHOLD:-40}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${REPO_ROOT}/build/aoc_simulate"
OUT_DIR="${REPO_ROOT}/build/smoke"

if [[ ! -x "${BIN}" ]]; then
  echo "error: ${BIN} not built. Run cmake --build build first." >&2
  exit 2
fi

mkdir -p "${OUT_DIR}"
FAIL=0
declare -a UNIQ_RECIPES

for seed in 1 2 3 4 5; do
  log="${OUT_DIR}/seed_${seed}.log"
  if "${BIN}" --turns "${TURNS}" --players "${PLAYERS}" --seed "${seed}" \
      > "${log}" 2>&1; then
    rc=0
  else
    rc=$?
  fi

  uniq=$(grep -cE "recipe_first_fire" "${log}" || true)
  UNIQ_RECIPES+=("${uniq}")
  printf 'seed=%d rc=%d unique_recipes_fired=%d log=%s\n' \
    "${seed}" "${rc}" "${uniq}" "${log}"

  if [[ "${rc}" -ne 0 ]]; then
    FAIL=1
  fi
done

# Union of recipe IDs across all seeds (robust to seed-dependent variance).
union=$(cat "${OUT_DIR}"/seed_*.log \
        | grep -oE "recipe_first_fire: id=[0-9]+" \
        | sort -u | wc -l)
printf 'union_unique_recipes=%d threshold=%d\n' "${union}" "${RECIPE_THRESHOLD}"

if [[ "${union}" -lt "${RECIPE_THRESHOLD}" ]]; then
  echo "regression: union recipe count ${union} < threshold ${RECIPE_THRESHOLD}" >&2
  FAIL=1
fi

exit "${FAIL}"
