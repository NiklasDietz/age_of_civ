#!/usr/bin/env bash
# sweep_matrix.sh -- run aoc_simulate over a (map x length) matrix with all 12
# civs active, dump every trace to CSV, and leave per-run artifacts in OUT_DIR.
#
# Defaults produce 12 games (4 maps x 3 lengths). Override any axis via env:
#   MAPS="continents pangaea"
#   LENGTHS="200 400"
#   PLAYERS=12
#   WORKERS=4
#   OUT_DIR=/tmp/aoc_sweep
#
# Usage:
#   scripts/sweep_matrix.sh          # full 12-game sweep, 4 parallel
#   WORKERS=8 scripts/sweep_matrix.sh
#   MAPS=pangaea LENGTHS=800 scripts/sweep_matrix.sh   # single deep run

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIM="${SIM:-$ROOT/build/aoc_simulate}"
DUMP="${DUMP:-$ROOT/build/aoc_trace_dump}"
OUT_DIR="${OUT_DIR:-/tmp/aoc_sweep}"
MAPS="${MAPS:-continents pangaea archipelago fractal}"
LENGTHS="${LENGTHS:-200 400 800}"
PLAYERS="${PLAYERS:-12}"
WORKERS="${WORKERS:-4}"
VICTORY_TYPES="${VICTORY_TYPES:-all}"

if [[ ! -x "$SIM" ]]; then
    echo "error: aoc_simulate not found at $SIM" >&2
    echo "build with: cmake --build build -j --target aoc_simulate aoc_trace_dump" >&2
    exit 1
fi
if [[ ! -x "$DUMP" ]]; then
    echo "error: aoc_trace_dump not found at $DUMP" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

run_one() {
    local map="$1" turns="$2"
    local tag="${map}_${turns}"
    "$SIM" --turns "$turns" --players "$PLAYERS" --map-type "$map" \
        --victory-types "$VICTORY_TYPES" \
        --output "$OUT_DIR/${tag}.csv" \
        --trace-file "$OUT_DIR/${tag}.aocl" \
        >/dev/null 2>"$OUT_DIR/${tag}.log"
    echo "sim done  $tag"
}
export -f run_one
export SIM OUT_DIR PLAYERS VICTORY_TYPES

echo "== aoc sweep =="
echo "  maps:    $MAPS"
echo "  lengths: $LENGTHS"
echo "  players: $PLAYERS"
echo "  workers: $WORKERS"
echo "  victory: $VICTORY_TYPES"
echo "  out:     $OUT_DIR"
echo

{
    for map in $MAPS; do
        for turns in $LENGTHS; do
            printf '%s\t%s\n' "$map" "$turns"
        done
    done
} | xargs -P "$WORKERS" -L 1 bash -c 'run_one "$@"' _

echo
echo "== dumping traces =="
for f in "$OUT_DIR"/*.aocl; do
    [[ -f "$f" ]] || continue
    base="${f%.aocl}"
    "$DUMP" "$f" "$base" 2>/dev/null
    echo "dump done $(basename "$base")"
done

echo
echo "artifacts in $OUT_DIR:"
ls -lh "$OUT_DIR" | awk 'NR>1 {print "  " $9 "  " $5}' | sort
