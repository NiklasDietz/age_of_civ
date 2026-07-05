#!/usr/bin/env bash
# Drive the continent-shape metrics over a fixed seed matrix and
# aggregate the results into build/plate_diag/SUMMARY.md.
#
# 2026-07-05: rewritten around tools/mapgen_metrics.py. The previous
# version drove diagnose_plate_shapes / diagnose_mountain_edges /
# diagnose_peak_sample_leak, which consumed --dump-edges /
# --dump-mountain-edges / --dump-physics-cells CSVs that the CLI no
# longer emits (Voronoi-polygon-era data model); all three scripts and
# flags have been deleted.
#
# Outputs:
#   build/plate_diag/m_s<N>.csv         per-tile CSV dumps
#   build/plate_diag/m_s<N>.plates.csv  per-plate stats (--dump-plates)
#   build/plate_diag/metrics.json       per-seed shape metrics
#   build/plate_diag/SUMMARY.md         headline metrics
#
# Usage: tools/run_diagnostic_matrix.sh [SEEDS...]
#   default seeds: 1..16 at 140x90.

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

SEED_LIST="$(IFS=,; echo "${SEEDS[*]}")"

python3 "${ROOT}/tools/mapgen_metrics.py" baseline \
    --binary "${MAPGEN}" \
    --outdir "${OUT}" \
    --seeds "${SEED_LIST}"

# --dump-plates per seed for plate-level stats (cell count, land frac,
# bbox, centroid) alongside the shape metrics.
for s in "${SEEDS[@]}"; do
    "${MAPGEN}" \
        --seed "${s}" --width 140 --height 90 \
        --output "${OUT}/m_s${s}" --format csv \
        --dump-plates "${OUT}/m_s${s}.plates.csv" > /dev/null
done

SUMMARY="${OUT}/SUMMARY.md"
{
    echo "# Plate diagnostic matrix"
    echo
    echo "Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "Seeds: ${SEEDS[*]}  Resolution: 140x90"
    echo
    echo "## Continent-shape metrics (tools/mapgen_metrics.py)"
    echo
    echo '```'
    python3 - "${OUT}/metrics.json" <<'EOF'
import json, sys
data = json.load(open(sys.argv[1]))
hdr = f"{'seed':>6} {'land%':>6} {'comps':>5} {'4-30':>4} {'coastD':>6} {'axis0/90':>8} {'beltaxis':>8}"
print(hdr)
for seed, m in data.items():
    co = m.get("coast_orientation") or {}
    mt = m.get("mountains") or {}
    print(f"{seed:>6} {m['land_fraction']*100:>6.1f} {m['n_land_components']:>5} "
          f"{m['component_bands']['4-30']:>4} {str(m['coast_box_dimension']):>6} "
          f"{str(co.get('axis_aligned_frac')):>8} "
          f"{str(mt.get('belt_axis_aligned_frac')):>8}")
EOF
    echo '```'
    echo
    echo "Detail: ${OUT}/metrics.json, per-plate stats in m_s<N>.plates.csv."
} > "${SUMMARY}"

echo "wrote ${SUMMARY}"
