#!/usr/bin/env python3
"""Continent-shape metrics for aoc_mapgen output.

Phase-gate instrument for the continent-realism program: computes land
fraction, land-component size bands, coastline box-count fractal dimension,
coast/mountain-belt orientation histograms, and a stable content hash from
an aoc_mapgen CSV dump (--format csv). The CSV is used instead of ASCII
because feature glyphs (Ice, Reef) mask the underlying terrain there.

Usage:
  mapgen_metrics.py analyze MAP.csv [--json]
  mapgen_metrics.py baseline --binary BIN --outdir DIR
      [--seeds 42,7,100,200,1234,777] [--width 140] [--height 90]

`baseline` runs the binary once per seed, analyzes each map, and writes
DIR/metrics.json plus the per-seed CSVs. Compare two metrics.json files
across a change to see what moved.
"""

import argparse
import csv
import hashlib
import json
import math
import subprocess
import sys
from collections import deque
from pathlib import Path

WATER_TERRAINS = {"Ocean", "Coast", "ShallowWater"}

# odd-r offset neighbours (odd rows shifted right, matching HexCoord.hpp).
NEIGHBOURS_EVEN = ((1, 0), (-1, 0), (0, -1), (-1, -1), (0, 1), (-1, 1))
NEIGHBOURS_ODD = ((1, 0), (-1, 0), (1, -1), (0, -1), (1, 1), (0, 1))


def load_csv(path):
    """Return (width, height, land, mountain) bool grids indexed [row][col]."""
    cells = {}
    max_col = max_row = 0
    with open(path, newline="") as fh:
        for rec in csv.DictReader(fh):
            col, row = int(rec["Col"]), int(rec["Row"])
            cells[(col, row)] = rec["Terrain"]
            max_col = max(max_col, col)
            max_row = max(max_row, row)
    width, height = max_col + 1, max_row + 1
    land = [[False] * width for _ in range(height)]
    mountain = [[False] * width for _ in range(height)]
    for (col, row), terrain in cells.items():
        land[row][col] = terrain not in WATER_TERRAINS
        mountain[row][col] = terrain == "Mountain"
    return width, height, land, mountain


def hex_neighbours(col, row, width, height):
    table = NEIGHBOURS_ODD if row & 1 else NEIGHBOURS_EVEN
    for dc, dr in table:
        nc, nr = col + dc, row + dr
        if 0 <= nc < width and 0 <= nr < height:
            yield nc, nr


def components(mask, width, height):
    """Connected-component sizes over the hex adjacency."""
    seen = [[False] * width for _ in range(height)]
    sizes = []
    for row in range(height):
        for col in range(width):
            if not mask[row][col] or seen[row][col]:
                continue
            size = 0
            queue = deque([(col, row)])
            seen[row][col] = True
            while queue:
                c, r = queue.popleft()
                size += 1
                for nc, nr in hex_neighbours(c, r, width, height):
                    if mask[nr][nc] and not seen[nr][nc]:
                        seen[nr][nc] = True
                        queue.append((nc, nr))
            sizes.append(size)
    return sorted(sizes, reverse=True)


def coastline_cells(land, width, height):
    coast = []
    for row in range(height):
        for col in range(width):
            if not land[row][col]:
                continue
            if any(not land[nr][nc]
                   for nc, nr in hex_neighbours(col, row, width, height)):
                coast.append((col, row))
    return coast


def box_count_dimension(points, width, height):
    """Least-squares slope of log N(s) vs log(1/s) over s in {1,2,4,8,16}."""
    if len(points) < 8:
        return None
    scales, counts = [], []
    for s in (1, 2, 4, 8, 16):
        if s > min(width, height) // 2:
            break
        boxes = {(c // s, r // s) for c, r in points}
        scales.append(s)
        counts.append(len(boxes))
    if len(scales) < 3:
        return None
    xs = [math.log(1.0 / s) for s in scales]
    ys = [math.log(n) for n in counts]
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    denom = sum((x - mx) ** 2 for x in xs)
    if denom == 0:
        return None
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / denom


def sobel_orientation_bins(cells, land, width, height):
    """Boundary-tangent orientation histogram in 4 bins of 45 deg (mod 180).

    Bin 0 = tangent within +-22.5 deg of horizontal, bin 2 = vertical;
    axis_aligned = bins 0+2. Gradient from a 3x3 Sobel on the square-grid
    approximation of the land mask (consistent across before/after, which
    is all a phase gate needs).
    """
    bins = [0, 0, 0, 0]
    for col, row in cells:
        gx = gy = 0.0
        for dr in (-1, 0, 1):
            for dc in (-1, 0, 1):
                c, r = col + dc, row + dr
                v = 1.0 if (0 <= c < width and 0 <= r < height
                            and land[r][c]) else 0.0
                kx = dc * (2 if dr == 0 else 1)
                ky = dr * (2 if dc == 0 else 1)
                gx += v * kx
                gy += v * ky
        if gx == 0.0 and gy == 0.0:
            continue
        tangent = math.degrees(math.atan2(gy, gx)) + 90.0
        idx = int(((tangent % 180.0) + 22.5) // 45.0) % 4
        bins[idx] += 1
    total = sum(bins)
    if total == 0:
        return None
    return {
        "bins_deg_0_45_90_135": [round(b / total, 4) for b in bins],
        "axis_aligned_frac": round((bins[0] + bins[2]) / total, 4),
        "samples": total,
    }


def component_pca_axis_deg(comp_cells):
    """Principal-axis angle (deg mod 180) and eigenvalue ratio of a
    component's cell coordinates; odd rows shifted +0.5 col to undo the
    offset stagger."""
    pts = [(c + (0.5 if r & 1 else 0.0), r * 0.866) for c, r in comp_cells]
    n = len(pts)
    mx = sum(p[0] for p in pts) / n
    my = sum(p[1] for p in pts) / n
    sxx = sum((p[0] - mx) ** 2 for p in pts) / n
    syy = sum((p[1] - my) ** 2 for p in pts) / n
    sxy = sum((p[0] - mx) * (p[1] - my) for p in pts) / n
    tr, det = sxx + syy, sxx * syy - sxy * sxy
    disc = max(tr * tr / 4.0 - det, 0.0)
    l1 = tr / 2.0 + math.sqrt(disc)
    l2 = tr / 2.0 - math.sqrt(disc)
    angle = math.degrees(0.5 * math.atan2(2.0 * sxy, sxx - syy)) % 180.0
    ratio = (l1 / l2) if l2 > 1e-9 else float("inf")
    return angle, ratio


def mountain_belt_stats(mountain, width, height):
    seen = [[False] * width for _ in range(height)]
    comps = []
    for row in range(height):
        for col in range(width):
            if not mountain[row][col] or seen[row][col]:
                continue
            comp = []
            queue = deque([(col, row)])
            seen[row][col] = True
            while queue:
                c, r = queue.popleft()
                comp.append((c, r))
                for nc, nr in hex_neighbours(c, r, width, height):
                    if mountain[nr][nc] and not seen[nr][nc]:
                        seen[nr][nc] = True
                        queue.append((nc, nr))
            comps.append(comp)
    belts = [c for c in comps if len(c) >= 3]
    if not belts:
        return {"n_components": len(comps), "n_belts": 0}
    bins = [0, 0, 0, 0]
    ratios = []
    for comp in belts:
        angle, ratio = component_pca_axis_deg(comp)
        bins[int((angle + 22.5) // 45.0) % 4] += 1
        if math.isfinite(ratio):
            ratios.append(ratio)
    total = sum(bins)
    return {
        "n_components": len(comps),
        "n_belts": total,
        "belt_axis_bins_0_45_90_135": bins,
        "belt_axis_aligned_frac": round((bins[0] + bins[2]) / total, 4),
        "mean_elongation": round(sum(ratios) / len(ratios), 2) if ratios else None,
    }


def analyze(csv_path):
    width, height, land, mountain = load_csv(csv_path)
    land_cells = sum(row.count(True) for row in land)
    total = width * height
    comp_sizes = components(land, width, height)
    coast = coastline_cells(land, width, height)
    bands = {
        "1-3": sum(1 for s in comp_sizes if s <= 3),
        "4-30": sum(1 for s in comp_sizes if 4 <= s <= 30),
        "31-100": sum(1 for s in comp_sizes if 31 <= s <= 100),
        ">100": sum(1 for s in comp_sizes if s > 100),
    }
    return {
        "file": str(csv_path),
        "sha256": hashlib.sha256(Path(csv_path).read_bytes()).hexdigest(),
        "width": width,
        "height": height,
        "land_fraction": round(land_cells / total, 4),
        "n_land_components": len(comp_sizes),
        "component_bands": bands,
        "largest_components": comp_sizes[:8],
        "largest_over_median": (
            round(comp_sizes[0] / comp_sizes[len(comp_sizes) // 2], 2)
            if comp_sizes else None),
        "coastline_cells": len(coast),
        "coast_perimeter_over_land": (
            round(len(coast) / land_cells, 4) if land_cells else None),
        "coast_box_dimension": (
            round(dim, 3)
            if (dim := box_count_dimension(coast, width, height)) is not None
            else None),
        "coast_orientation": sobel_orientation_bins(coast, land, width, height),
        "mountains": mountain_belt_stats(mountain, width, height),
    }


def cmd_baseline(args):
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    seeds = [int(s) for s in args.seeds.split(",")]
    results = {}
    for seed in seeds:
        stem = outdir / f"seed{seed}"
        cmd = [args.binary, "--seed", str(seed),
               "--width", str(args.width), "--height", str(args.height),
               "--format", "csv", "--output", str(stem)]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            print(f"error: seed {seed} generation failed:\n{proc.stderr[-2000:]}",
                  file=sys.stderr)
            return 1
        results[str(seed)] = analyze(f"{stem}.csv")
        print(f"seed {seed}: land={results[str(seed)]['land_fraction']:.1%} "
              f"D={results[str(seed)]['coast_box_dimension']} "
              f"components={results[str(seed)]['n_land_components']}")
    metrics_path = outdir / "metrics.json"
    metrics_path.write_text(json.dumps(results, indent=2) + "\n")
    print(f"wrote {metrics_path}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)
    p_analyze = sub.add_parser("analyze", help="analyze one aoc_mapgen CSV")
    p_analyze.add_argument("csv_path")
    p_baseline = sub.add_parser("baseline",
                                help="generate + analyze a seed sweep")
    p_baseline.add_argument("--binary", required=True)
    p_baseline.add_argument("--outdir", required=True)
    p_baseline.add_argument("--seeds", default="42,7,100,200,1234,777")
    p_baseline.add_argument("--width", type=int, default=140)
    p_baseline.add_argument("--height", type=int, default=90)
    args = parser.parse_args()
    if args.cmd == "analyze":
        print(json.dumps(analyze(args.csv_path), indent=2))
        return 0
    return cmd_baseline(args)


if __name__ == "__main__":
    sys.exit(main())
