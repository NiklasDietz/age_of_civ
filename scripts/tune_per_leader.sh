#!/usr/bin/env bash
#
# Per-leader GA tuning driver.
#
# Runs `aoc_evolve` 12 times, once per hand-crafted leader, seeding the
# entire population from that leader's archetype. The winner of each run
# is that leader's GA-tuned values while preserving archetype flavor
# (mutations only wander locally from the seed, bounded by --generations).
#
# Outputs go to: results/per_leader/<index>_<name>.txt
# Aggregate summary: results/per_leader/_summary.md

set -euo pipefail

cd "$(dirname "$0")/.."

BUILD="${BUILD:-build}"
BIN="$BUILD/ml/cpp/aoc_evolve"
OUT_DIR="results/per_leader"
mkdir -p "$OUT_DIR"

GEN="${GEN:-30}"
POP="${POP:-16}"
GAMES="${GAMES:-3}"
TURNS_LIST="${TURNS_LIST:-200,350,500}"
PLAYERS="${PLAYERS:-6}"
WORKERS="${WORKERS:-16}"
SEED_BASE="${SEED_BASE:-1000}"
OPPONENT_MODE="${OPPONENT_MODE:-coevolve}"
LOG_LEVEL="${LOG_LEVEL:-warn}"

declare -a NAMES=(
    "Trajan" "Cleopatra" "QinShiHuang" "Frederick"
    "Pericles" "Victoria" "Hojo" "Cyrus"
    "Montezuma" "Gandhi" "Peter" "PedroII"
)

echo "=== Per-leader tuning ==="
echo "    generations: $GEN   population: $POP   games: $GAMES"
echo "    turns-list: $TURNS_LIST   players: $PLAYERS   workers: $WORKERS"
echo "    opponent-mode: $OPPONENT_MODE   log-level: $LOG_LEVEL"
echo "    output: $OUT_DIR/"
echo

for i in "${!NAMES[@]}"; do
    NAME="${NAMES[$i]}"
    OUT="$OUT_DIR/$(printf '%02d' "$i")_${NAME}.txt"
    SEED=$((SEED_BASE + i))
    echo "[$i/12] Tuning leader $i ($NAME) -> $OUT"
    "$BIN" \
        --generations "$GEN" \
        --population "$POP" \
        --games "$GAMES" \
        --turns-list "$TURNS_LIST" \
        --players "$PLAYERS" \
        --workers "$WORKERS" \
        --seed "$SEED" \
        --seed-leader "$i" \
        --opponent-mode "$OPPONENT_MODE" \
        --log-level "$LOG_LEVEL" \
        >/dev/null 2>"$OUT.progress"
    # evolved_summary.txt is overwritten each run; capture it
    cp evolved_summary.txt "$OUT"
    BEST=$(grep -m1 "Best ever fitness" "$OUT.progress" | awk '{print $NF}' || echo "?")
    echo "    best-ever: $BEST"
done

# Aggregate Hard tier from each run into one summary
SUMMARY="$OUT_DIR/_summary.md"
{
    echo "# Per-leader GA-tuned values"
    echo
    echo "Generated $(date -Iseconds). Config: gen=$GEN pop=$POP games=$GAMES turns=$TURNS_LIST players=$PLAYERS mode=$OPPONENT_MODE"
    echo
    for i in "${!NAMES[@]}"; do
        NAME="${NAMES[$i]}"
        OUT="$OUT_DIR/$(printf '%02d' "$i")_${NAME}.txt"
        [ -f "$OUT" ] || continue
        echo "## $i — $NAME"
        echo
        echo '```'
        awk '/^Hard AI/,/^Medium AI/' "$OUT" | sed '/^Medium AI/d'
        echo '```'
        echo
    done
} > "$SUMMARY"

echo
echo "Done. Summary: $SUMMARY"
