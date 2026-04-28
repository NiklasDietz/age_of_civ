#!/usr/bin/env bash
# WP-L4 v2: full-matrix audit. Sweeps player counts, turn lengths, and map
# types so balance results are statistically meaningful instead of single-
# config noise. Aggregates victory-mix totals across all configs.
#
# Default matrix:
#   players: 4, 6, 8
#   turns:   500, 1000, 1500, 2000
#   maps:    continents, islands, landwithseas, fractal, pangaea, archipelago, realistic
#   seeds:   1, 2, 3
#
# Total = 3 × 4 × 7 × 3 = 252 sims. Run in parallel batches (cap workers).
#
# Usage: scripts/audit_matrix.sh [PLAYERS_LIST] [TURNS_LIST] [MAPS_LIST] [SEEDS_LIST] [WORKERS]
#   Each list is comma-separated. Workers default to nproc.
#
# Examples:
#   scripts/audit_matrix.sh                               # full default matrix
#   scripts/audit_matrix.sh "6" "1000,2000" "continents" # narrow sweep
#   scripts/audit_matrix.sh "4,6,8" "1000" "continents,islands" "1,2,3,4,5"

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${REPO_ROOT}/build/aoc_simulate"
AUDIT_DIR="${REPO_ROOT}/build/audit_matrix"
REPORT="${AUDIT_DIR}/REPORT.md"

PLAYERS_LIST="${1:-2,4,6,8,10,12,14,16,18,20}"
TURNS_LIST="${2:-500,1000,1500,2000}"
MAPS_LIST="${3:-continents,islands,landwithseas,fractal,pangaea,archipelago,realistic}"
SEEDS_LIST="${4:-1,2,3}"
WORKERS="${5:-$(nproc)}"
VICTORY_TYPES="${6:-all}"

if [[ ! -x "${BIN}" ]]; then
  echo "error: ${BIN} not built. Run cmake --build build first." >&2
  exit 2
fi

mkdir -p "${AUDIT_DIR}"
rm -f "${AUDIT_DIR}"/*.log "${REPORT}"

# Build the full job list.
JOBS=()
for players in ${PLAYERS_LIST//,/ }; do
  for turns in ${TURNS_LIST//,/ }; do
    for map in ${MAPS_LIST//,/ }; do
      for seed in ${SEEDS_LIST//,/ }; do
        JOBS+=("${players} ${turns} ${map} ${seed}")
      done
    done
  done
done

TOTAL=${#JOBS[@]}
echo "Running ${TOTAL} sims with ${WORKERS} workers..."
echo "Players: ${PLAYERS_LIST}"
echo "Turns:   ${TURNS_LIST}"
echo "Maps:    ${MAPS_LIST}"
echo "Seeds:   ${SEEDS_LIST}"
echo

# Parallel batched run.
RUNNING=0
DONE=0
for job in "${JOBS[@]}"; do
  read -r players turns map seed <<< "${job}"
  log="${AUDIT_DIR}/p${players}_t${turns}_${map}_s${seed}.log"
  "${BIN}" --turns "${turns}" --players "${players}" --seed "${seed}" \
           --map-type "${map}" --victory-types "${VICTORY_TYPES}" > "${log}" 2>&1 &
  RUNNING=$((RUNNING + 1))
  if (( RUNNING >= WORKERS )); then
    wait -n
    RUNNING=$((RUNNING - 1))
    DONE=$((DONE + 1))
    if (( DONE % 20 == 0 )); then
      echo "  ...${DONE}/${TOTAL} done"
    fi
  fi
done
wait

LOGS="${AUDIT_DIR}"/*.log

cnt() { (grep -hoE "$1" ${LOGS} 2>/dev/null || true) | wc -l; }
unique_ids() { (grep -hoE "$1" ${LOGS} 2>/dev/null || true) | awk -F= '{print $2}' | sort -un | wc -l; }

# Aggregate per-config victory totals.
{
  echo "# Audit matrix report"
  echo
  echo "Generated: $(date -Iseconds)"
  echo "Total sims: ${TOTAL}"
  echo "Players: ${PLAYERS_LIST}"
  echo "Turns:   ${TURNS_LIST}"
  echo "Maps:    ${MAPS_LIST}"
  echo "Seeds:   ${SEEDS_LIST}"
  echo

  echo "## Aggregate victory outcomes (all configs)"
  echo
  echo "| Type | Count | % |"
  echo "|---|---:|---:|"
  for vt in CULTURE SCIENCE RELIGION DOMINATION PRESTIGE SCORE; do
    n=$(cnt "wins by ${vt}")
    pct=$(awk "BEGIN { printf \"%.1f\", ${n} * 100.0 / ${TOTAL} }")
    echo "| ${vt} | ${n} | ${pct}% |"
  done
  echo
  echo "## Mechanic counts (aggregate)"
  printf -- '- Wonders completed: %d\n' "$(cnt 'Completed wonder ')"
  printf -- '- Districts completed: %d\n' "$(cnt 'Completed district ')"
  printf -- '- Trade routes established: %d\n' "$(cnt 'Trade route type:')"
  printf -- '- Religion conversions: %d\n' "$(cnt 'religion spread:')"
  printf -- '- City captures: %d\n' "$(cnt 'captured by player')"
  printf -- '- Civ eliminations: %d\n' "$(cnt 'eliminated.*last city')"
  printf -- '- Mars Colony completions: %d\n' "$(cnt "completed 'Mars Colony Ship'")"
  printf -- '- Lunar Colony completions: %d\n' "$(cnt "completed 'Establish Lunar Colony'")"
  printf -- '- Nuclear strikes: %d\n' "$(cnt 'NUCLEAR STRIKE')"
  echo

  echo "## Victory mix per player count"
  echo
  for players in ${PLAYERS_LIST//,/ }; do
    glob="${AUDIT_DIR}/p${players}_*.log"
    pcount=$(ls -1 ${glob} 2>/dev/null | wc -l)
    [[ ${pcount} -eq 0 ]] && continue
    echo "### players=${players} (${pcount} sims)"
    echo "| Type | Count |"
    echo "|---|---:|"
    for vt in CULTURE SCIENCE RELIGION DOMINATION PRESTIGE SCORE; do
      n=$( (grep -hoE "wins by ${vt}" ${glob} 2>/dev/null || true) | wc -l)
      echo "| ${vt} | ${n} |"
    done
    echo
  done

  echo "## Victory mix per turn-length"
  echo
  for turns in ${TURNS_LIST//,/ }; do
    glob="${AUDIT_DIR}/*_t${turns}_*.log"
    pcount=$(ls -1 ${glob} 2>/dev/null | wc -l)
    [[ ${pcount} -eq 0 ]] && continue
    echo "### turns=${turns} (${pcount} sims)"
    echo "| Type | Count |"
    echo "|---|---:|"
    for vt in CULTURE SCIENCE RELIGION DOMINATION PRESTIGE SCORE; do
      n=$( (grep -hoE "wins by ${vt}" ${glob} 2>/dev/null || true) | wc -l)
      echo "| ${vt} | ${n} |"
    done
    echo
  done

  echo "## Victory mix per map type"
  echo
  for map in ${MAPS_LIST//,/ }; do
    glob="${AUDIT_DIR}/*_${map}_*.log"
    pcount=$(ls -1 ${glob} 2>/dev/null | wc -l)
    [[ ${pcount} -eq 0 ]] && continue
    echo "### map=${map} (${pcount} sims)"
    echo "| Type | Count |"
    echo "|---|---:|"
    for vt in CULTURE SCIENCE RELIGION DOMINATION PRESTIGE SCORE; do
      n=$( (grep -hoE "wins by ${vt}" ${glob} 2>/dev/null || true) | wc -l)
      echo "| ${vt} | ${n} |"
    done
    echo
  done

  echo "## Decision turn distribution"
  echo '```'
  grep -hE "GAME OVER on turn [0-9]+" ${LOGS} \
    | awk '{for(i=1;i<=NF;i++) if ($i=="turn") print $(i+1)}' \
    | awk -F: '{print $1}' | sort -n | uniq -c | sort -rn | head -20
  echo '```'
} > "${REPORT}"

echo
echo "Wrote ${REPORT}"
echo
head -50 "${REPORT}"
