#!/usr/bin/env bash
# WP-L4: comprehensive mechanic audit. Runs N seeds × M maps × T turns,
# parses logs for every tracked mechanic, writes a versioned report to
# build/audit/REPORT.md so successive runs can be diffed.
#
# Usage: scripts/audit_mechanics.sh [TURNS] [PLAYERS] [SEEDS_LIST] [MAPS_LIST]
#   TURNS  default 1000
#   PLAYERS default 6
#   SEEDS_LIST  comma-separated, default "1,2,3"
#   MAPS_LIST   comma-separated, default "continents,islands,landwithseas,fractal"
#
# Examples:
#   scripts/audit_mechanics.sh
#   scripts/audit_mechanics.sh 1500 6 "1,2,3,4,5" "continents,islands"

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${REPO_ROOT}/build/aoc_simulate"
AUDIT_DIR="${REPO_ROOT}/build/audit"
REPORT="${AUDIT_DIR}/REPORT.md"

TURNS="${1:-1000}"
PLAYERS="${2:-6}"
SEEDS="${3:-1,2,3}"
MAPS="${4:-continents,islands,landwithseas,fractal}"

if [[ ! -x "${BIN}" ]]; then
  echo "error: ${BIN} not built. Run cmake --build build first." >&2
  exit 2
fi

mkdir -p "${AUDIT_DIR}"
rm -f "${AUDIT_DIR}"/*.log "${REPORT}"

# Parallel batch.
SIM_COUNT=0
for seed in ${SEEDS//,/ }; do
  for map in ${MAPS//,/ }; do
    log="${AUDIT_DIR}/${map}_s${seed}.log"
    "${BIN}" --turns "${TURNS}" --players "${PLAYERS}" --seed "${seed}" \
             --map-type "${map}" > "${log}" 2>&1 &
    SIM_COUNT=$((SIM_COUNT + 1))
  done
done
wait

LOGS="${AUDIT_DIR}"/*.log

cnt() { (grep -hoE "$1" ${LOGS} 2>/dev/null || true) | wc -l; }
unique_ids() { (grep -hoE "$1" ${LOGS} 2>/dev/null || true) | awk -F= '{print $2}' | sort -un | wc -l; }

# Build report.
{
  echo "# Mechanic audit report"
  echo
  echo "Generated: $(date -Iseconds)"
  echo "Sims: ${SIM_COUNT} (${TURNS}t × ${PLAYERS}p)"
  echo "Seeds: ${SEEDS}"
  echo "Maps: ${MAPS}"
  echo
  echo "## Production"
  printf -- '- Unique recipes fired: **%d**\n' "$(unique_ids 'recipe_first_fire: id=[0-9]+')"
  printf -- '- Total recipe fires: %d\n' "$(cnt 'recipe_first_fire:')"
  printf -- '- Wonders completed: %d\n' "$(cnt 'Completed wonder ')"
  printf -- '- Districts completed: %d\n' "$(cnt 'Completed district ')"
  printf -- '- Built Research Lab: %d\n' "$(cnt 'Built Research Lab')"
  printf -- '- Built Semiconductor Fab: %d\n' "$(cnt 'Built Semiconductor Fab')"
  printf -- '- Built Fusion Reactor: %d\n' "$(cnt 'Built Fusion Reactor')"
  echo
  echo "## Climate / Disasters (WP-A4)"
  printf -- '- Drought: %d\n' "$(cnt 'DROUGHT at tile')"
  printf -- '- Wildfire: %d\n' "$(cnt 'WILDFIRE destroyed')"
  printf -- '- Hurricane: %d\n' "$(cnt 'HURRICANE at coastal')"
  printf -- '- Earthquake: %d\n' "$(cnt 'EARTHQUAKE damaged')"
  printf -- '- Volcanic: %d\n' "$(cnt 'VOLCANIC ERUPTION')"
  printf -- '- Coast flood: %d\n' "$(cnt 'coastal tile.*flooded')"
  echo
  echo "## Loyalty / Cities"
  printf -- '- Combined revolt: %d\n' "$(cnt 'COMBINED REVOLT:')"
  printf -- '- Revolt-end auto-return: %d\n' "$(cnt 'REVOLT-END:')"
  printf -- '- Secession: %d\n' "$(cnt 'REVOLT:.*flips')"
  printf -- '- Faith rush (WP-A1): %d\n' "$(cnt 'Faith rush:')"
  echo
  echo "## Religion (WP-A2)"
  printf -- '- Conversions: %d\n' "$(cnt 'religion spread:')"
  printf -- '- Religion wins: %d\n' "$(cnt 'wins by RELIGION')"
  echo
  echo "## Espionage (WP-A8)"
  printf -- '- Caught spy events: %d\n' "$(cnt 'Spy .*failed')"
  printf -- '- Spy cascade trade suspends: %d\n' "$(cnt 'Spy cascade:')"
  echo
  echo "## Great People (WP-A3)"
  printf -- '- GP activations (any): %d\n' "$(cnt 'Scientist added|Scientist: 20-turn|Engineer added|Engineer placed free|General healed|Artist culture-bombed|Merchant: \+200')"
  printf -- '- Scientist pulse starts: %d\n' "$(cnt 'Scientist: 20-turn')"
  printf -- '- Engineer free district: %d\n' "$(cnt 'Engineer placed free Industrial')"
  printf -- '- Merchant trade slot: %d\n' "$(cnt 'Merchant: \+200')"
  echo
  echo "## Tile infrastructure (WP-C3)"
  printf -- '- PowerPole laid: %d\n' "$(cnt 'laid PowerPole|placed PowerPole')"
  printf -- '- Pipeline laid: %d\n' "$(cnt 'laid Pipeline|placed Pipeline')"
  echo
  echo "## Greenhouse (WP-C4)"
  printf -- '- Greenhouse built: %d\n' "$(cnt 'built Greenhouse')"
  printf -- '- Crops planted: %d\n' "$(cnt 'planted crop')"
  echo
  echo "## Trade (WP-K)"
  printf -- '- Routes established: %d\n' "$(cnt 'Trade route type:')"
  printf -- '- Routes by type:\n'
  for rt in Land Sea Air; do
    printf -- '    - %s: %d\n' "$rt" "$(cnt "Trade route type: ${rt}")"
  done
  printf -- '- Cap-rejected (slot pool): %d\n' "$(cnt 'Trade route rejected: player')"
  printf -- '- Range-rejected (tech gate): %d\n' "$(cnt 'Trade route rejected: P.*longest segment')"
  printf -- '- Trade agreements formed: %d\n' "$(cnt 'Trade deal:|Free Trade Zone|Customs Union|Transit treaty')"
  printf -- '- AI Trading Post relays: %d\n' "$(cnt 'TradingPost relay')"
  echo
  echo "## Space race (WP-B)"
  printf -- '- Earth Satellite: %d\n' "$(cnt "completed 'Launch Earth Satellite'")"
  printf -- '- Moon Landing: %d\n' "$(cnt "completed 'Launch Moon Landing'")"
  printf -- '- Lunar Colony: %d\n' "$(cnt "completed 'Establish Lunar Colony'")"
  printf -- '- Mars Colony: %d\n' "$(cnt "completed 'Mars Colony Ship'")"
  printf -- '- Exoplanet: %d\n' "$(cnt "completed 'Exoplanet")"
  printf -- '- Mars resource drain events: %d\n' "$(cnt 'Mars Colony consumed')"
  echo
  echo "## Military"
  printf -- '- Nuclear strikes: %d\n' "$(cnt 'NUCLEAR STRIKE')"
  echo
  echo "## Victory outcomes"
  echo
  echo "| Type | Count |"
  echo "|---|---:|"
  for vt in CULTURE SCIENCE RELIGION DOMINATION CONFEDERATION PRESTIGE SCORE; do
    n=$(cnt "wins by ${vt}")
    echo "| ${vt} | ${n} |"
  done
  echo
  echo "### Per-sim outcome + turn"
  echo
  echo '```'
  for f in ${LOGS}; do
    g=$(grep -E "GAME OVER on turn" "$f" | head -1)
    printf '%-30s %s\n' "$(basename "$f")" "${g#*GAME OVER }"
  done
  echo '```'
  echo
  echo "### Decision turn distribution"
  echo
  echo '```'
  grep -hE "GAME OVER on turn [0-9]+" ${LOGS} \
    | awk '{for(i=1;i<=NF;i++) if ($i=="turn") print $(i+1)}' \
    | awk -F: '{print $1}' | sort -n
  echo '```'
} > "${REPORT}"

echo "Wrote ${REPORT}"
echo
head -40 "${REPORT}"
