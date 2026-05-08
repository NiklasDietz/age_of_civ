#!/usr/bin/env bash
# Phase 13d-A2 step 4: drive the plate-shape / mountain-edge / peak-
# sample-leak diagnostic suite over a fixed seed matrix and aggregate
# the results into build/plate_diag/SUMMARY.md.
#
# Outputs:
#   build/plate_diag/m_s<N>.{plates,edges,medges,csv}.csv   raw dumps
#   build/plate_diag/m_s<N>.cells.plate*.csv                physics-cell dumps
#   build/plate_diag/SUMMARY.md                             headline metrics
#
# Usage: tools/run_diagnostic_matrix.sh [SEEDS...]
#   default seeds: 1..16 at 140x90 (fast); high-res 280x180 done for s1+s7.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ROOT}/build"
OUT="${BUILD}/plate_diag"
MAPGEN="${BUILD}/aoc_mapgen"

if [[ ! -x "${MAPGEN}" ]]; then
    echo "${MAPGEN} not built -- running cmake --build build first" >&2
    cmake --build "${BUILD}" -j"$(nproc)" --target aoc_mapgen
fi

mkdir -p "${OUT}"

SEEDS=("$@")
if [[ ${#SEEDS[@]} -eq 0 ]]; then
    SEEDS=(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16)
fi

# ---- per-seed dump (low-res 140x90) ----------------------------------
for s in "${SEEDS[@]}"; do
    echo "[matrix] dumping seed ${s} ..."
    rm -f "${OUT}/m_s${s}.cells.plate"*.csv
    "${MAPGEN}" \
        --seed "${s}" --width 140 --height 90 \
        --output "${OUT}/m_s${s}" --format csv \
        --dump-plates "${OUT}/m_s${s}.plates.csv" \
        --dump-edges "${OUT}/m_s${s}.edges.csv" \
        --dump-mountain-edges "${OUT}/m_s${s}.medges.csv" \
        --dump-physics-cells "${OUT}/m_s${s}.cells" > /dev/null
done

# ---- aggregate plate shapes ------------------------------------------
SHAPES_LOG="${OUT}/shapes.log"
{
    for s in "${SEEDS[@]}"; do
        echo "### seed ${s}"
        python3 "${ROOT}/tools/diagnose_plate_shapes.py" \
            --plates "${OUT}/m_s${s}.plates.csv" \
            --edges  "${OUT}/m_s${s}.edges.csv" || true
        echo
    done
} > "${SHAPES_LOG}"

# ---- aggregate mountain-edge anomaly ---------------------------------
MEDGE_LOG="${OUT}/medge.log"
python3 "${ROOT}/tools/diagnose_mountain_edges.py" \
    --files "${OUT}"/m_s*.medges.csv > "${MEDGE_LOG}"

# ---- aggregate peak-sample leak (per-seed, expensive) ----------------
LEAK_LOG="${OUT}/leak.log"
{
    for s in "${SEEDS[@]}"; do
        # Skip seeds that produced 0 mountains (script just prints empty).
        if [[ ! -s "${OUT}/m_s${s}.medges.csv" ]] \
            || [[ "$(wc -l < "${OUT}/m_s${s}.medges.csv")" -le 1 ]]; then
            echo "### seed ${s}: 0 mountains, skipping leak check"
            continue
        fi
        echo "### seed ${s}"
        python3 "${ROOT}/tools/diagnose_peak_sample_leak.py" \
            --mountain-edges "${OUT}/m_s${s}.medges.csv" \
            --cells "${OUT}/m_s${s}.cells.plate*.csv" 2>/dev/null || true
        echo
    done
} > "${LEAK_LOG}"

# ---- summary ---------------------------------------------------------
SUMMARY="${OUT}/SUMMARY.md"
{
    echo "# Plate diagnostic matrix"
    echo
    echo "Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "Seeds: ${SEEDS[*]}  Resolution: 140x90"
    echo
    echo "## Plate shapes (Polsby-Popper compactness, PCA aspect)"
    echo
    echo '```'
    awk '/compactness mean=/ { match($0, /mean=([0-9.]+)/, a); match($0, /median=([0-9.]+)/, b); printf "  %s\n", $0 }' "${SHAPES_LOG}" \
        || cat "${SHAPES_LOG}"
    echo '```'
    echo
    echo "## Mountain-edge anomaly (% mountains with nearest edge != type 2/4)"
    echo
    echo '```'
    cat "${MEDGE_LOG}"
    echo '```'
    echo
    echo "## PeakSample leak (winner_plate != tile.owner_plate)"
    echo
    echo '```'
    awk '
        /^### seed/ { seed=$3; next }
        /MISMATCH/  { printf "  s%-3s  %s\n", seed, $0 }
        /winner_distance_km/ { printf "  s%-3s  %s\n", seed, $0 }
    ' "${LEAK_LOG}"
    echo '```'
    echo
    echo "## Headlines"
    echo
    grep -h 'AGGREGATE' "${MEDGE_LOG}" || true
    echo
    echo "Detail logs in ${OUT}/{shapes,medge,leak}.log."
} > "${SUMMARY}"

echo "wrote ${SUMMARY}"
