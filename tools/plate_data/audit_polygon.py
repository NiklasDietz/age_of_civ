#!/usr/bin/env python3
"""
Polygon-tectonic system audit -- runs aoc_mapgen across N seeds at a fixed
size (200x80, 40 epochs) and reports realism metrics:

- mountain percent of land tiles (target 4-7%, Earth-like)
- continent count (target 5-12)
- largest landmass / total land ratio (target < 0.7)
- per-seed map gen wall-time (target < 2s)
- mountain-cluster shape stats (chain-like vs blob, distance from coast)

Usage:
    python3 tools/plate_data/audit_polygon.py [N_SEEDS] [--shape-stats]

Defaults: 20 seeds, 200x80 map, 40 epochs. CSV-only output is tossed in /tmp.
With --shape-stats: skip the realism summary and only emit the cluster shape
section. Without the flag, both passes run.
"""

import csv
import os
import statistics
import subprocess
import sys
import time
from collections import deque
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
BINARY = REPO_ROOT / "build" / "aoc_mapgen"

WIDTH = 200
HEIGHT = 80
EPOCHS = 40
DEFAULT_N = 20

WATER = {"Ocean", "Coast", "Shallow Water"}
NON_LAND = WATER | {"Tundra", "Snow"}  # match audit_game.py: polar excluded
MIN_COMPONENT = 30  # min land hexes to count as a continent
MIN_MTN_CLUSTER = 3  # min hexes to count a mountain cluster for shape stats
KM_PER_TILE = 30.0  # tile-to-km conversion for length reporting


def hex_neighbors(c, r):
    yield (c - 1, r)
    yield (c + 1, r)
    yield (c, r - 1)
    yield (c, r + 1)
    if r % 2 == 0:
        yield (c - 1, r - 1)
        yield (c - 1, r + 1)
    else:
        yield (c + 1, r - 1)
        yield (c + 1, r + 1)


def run_seed(seed: int):
    """Run the mapgen binary once. Returns dict of metrics or None on failure."""
    out_base = f"/tmp/audit_polygon_{seed}"
    cmd = [
        str(BINARY),
        "--width", str(WIDTH),
        "--height", str(HEIGHT),
        "--seed", str(seed),
        "--epochs", str(EPOCHS),
        "--output", out_base,
        "--format", "csv",
    ]
    t0 = time.perf_counter()
    proc = subprocess.run(
        cmd,
        env={**os.environ, "OMP_NUM_THREADS": "1"},
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    elapsed_ms = (time.perf_counter() - t0) * 1000.0
    if proc.returncode != 0:
        sys.stderr.write(f"seed {seed} failed (rc={proc.returncode}):\n{proc.stderr}\n")
        return None

    csv_path = f"{out_base}.csv"
    land = {}
    mountains = set()
    water = set()
    total_tiles = 0
    with open(csv_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            total_tiles += 1
            terrain = row["Terrain"]
            col = int(row["Col"])
            r = int(row["Row"])
            if terrain in WATER:
                water.add((col, r))
            if terrain in NON_LAND:
                continue
            land[(col, r)] = terrain
            if terrain == "Mountain":
                mountains.add((col, r))

    # Connected-component flood fill on land tiles to count continents.
    visited = set()
    components = []
    for cell in land:
        if cell in visited:
            continue
        comp = set()
        stack = [cell]
        while stack:
            x = stack.pop()
            if x in visited or x not in land:
                continue
            visited.add(x)
            comp.add(x)
            stack.extend(hex_neighbors(*x))
        if len(comp) >= MIN_COMPONENT:
            components.append(comp)

    land_count = len(land)
    if land_count == 0:
        return None
    sizes = [len(c) for c in components]
    largest = max(sizes, default=0)
    total_in_components = sum(sizes) or 1
    shape = compute_mountain_shape_stats(mountains, water, land)
    return {
        "seed": seed,
        "elapsed_ms": elapsed_ms,
        "land": land_count,
        "mountains": len(mountains),
        "mountain_pct": 100.0 * len(mountains) / land_count,
        "continents": len(components),
        "largest_frac": largest / total_in_components,
        "tiles": total_tiles,
        "shape": shape,
    }


def compute_mountain_shape_stats(mountains, water, land):
    """Per-cluster bounding-box / aspect / fill / coast-distance stats.

    Returns dict with keys: clusters (list of per-cluster dicts) and
    cluster_count. Each cluster dict has size, length_tiles, width_tiles,
    aspect_ratio, fill_ratio, centroid_coast_km. centroid_coast_km is None if
    the cluster centroid does not land on any reachable land tile (e.g. all
    members are inland past the BFS frontier from coast).
    """
    if not mountains:
        return {"clusters": [], "cluster_count": 0}

    # 1. BFS connected components on the mountain set with hex adjacency.
    visited = set()
    clusters = []
    for cell in mountains:
        if cell in visited:
            continue
        comp = []
        stack = [cell]
        while stack:
            x = stack.pop()
            if x in visited or x not in mountains:
                continue
            visited.add(x)
            comp.append(x)
            for n in hex_neighbors(*x):
                if n in mountains and n not in visited:
                    stack.append(n)
        clusters.append(comp)

    # 2. Multi-source BFS from water tiles over land+mountain tiles to build a
    # coast-distance field (in tile steps). Mountains count as land for this.
    coast_dist = {}  # (c,r) -> integer tile-step distance to nearest water
    landlike = set(land.keys()) | mountains
    queue = deque()
    for w in water:
        for n in hex_neighbors(*w):
            if n in landlike and n not in coast_dist:
                coast_dist[n] = 1
                queue.append(n)
    while queue:
        cur = queue.popleft()
        d = coast_dist[cur]
        for n in hex_neighbors(*cur):
            if n in landlike and n not in coast_dist:
                coast_dist[n] = d + 1
                queue.append(n)

    # 3. Per-cluster geometry (bounding-box dims, aspect, fill, coast dist).
    cluster_stats = []
    for comp in clusters:
        if len(comp) < MIN_MTN_CLUSTER:
            continue
        cols = [c for c, _ in comp]
        rows = [r for _, r in comp]
        width = max(cols) - min(cols) + 1
        height = max(rows) - min(rows) + 1
        length_tiles = max(width, height)
        width_tiles = min(width, height)
        bbox_area = length_tiles * width_tiles
        aspect = length_tiles / width_tiles if width_tiles > 0 else float(length_tiles)
        fill = len(comp) / bbox_area if bbox_area > 0 else 0.0
        cent_c = sum(cols) / len(cols)
        cent_r = sum(rows) / len(rows)
        # Snap centroid to the nearest cluster tile so coast-distance lookup
        # uses an actual landlike cell (centroid mean often lands inside the
        # bounding box but not on any cluster tile).
        snap = min(comp, key=lambda x: (x[0] - cent_c) ** 2 + (x[1] - cent_r) ** 2)
        d_tiles = coast_dist.get(snap)
        d_km = d_tiles * KM_PER_TILE if d_tiles is not None else None
        cluster_stats.append({
            "size": len(comp),
            "length_tiles": length_tiles,
            "width_tiles": width_tiles,
            "length_km": length_tiles * KM_PER_TILE,
            "aspect_ratio": aspect,
            "fill_ratio": fill,
            "centroid_coast_km": d_km,
        })

    return {"clusters": cluster_stats, "cluster_count": len(cluster_stats)}


def fmt_dist(values, top=8):
    """Compact distribution string: most-common values first."""
    counts = {}
    for v in values:
        counts[v] = counts.get(v, 0) + 1
    items = sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))[:top]
    return " ".join(f"{k}:{v}" for k, v in items)


def parse_args(argv):
    """Tiny CLI parser: positional seed count + --shape-stats flag."""
    shape_only = False
    n = DEFAULT_N
    pos = []
    for a in argv[1:]:
        if a == "--shape-stats":
            shape_only = True
        elif a.startswith("--"):
            sys.stderr.write(f"error: unknown flag {a}\n")
            sys.exit(2)
        else:
            pos.append(a)
    if pos:
        try:
            n = int(pos[0])
        except ValueError:
            sys.stderr.write(f"error: seed count must be int, got {pos[0]}\n")
            sys.exit(2)
    return n, shape_only


def report_shape_stats(rows):
    """Aggregate per-seed cluster stats into the shape-stats summary block."""
    counts = [r["shape"]["cluster_count"] for r in rows]
    sizes, lengths_km, aspects, fills, coast_km = [], [], [], [], []
    for r in rows:
        for c in r["shape"]["clusters"]:
            sizes.append(c["size"])
            lengths_km.append(c["length_km"])
            aspects.append(c["aspect_ratio"])
            fills.append(c["fill_ratio"])
            if c["centroid_coast_km"] is not None:
                coast_km.append(c["centroid_coast_km"])

    print(f"=== Sim mountain-cluster shape stats ({len(rows)} seeds, "
          f"{WIDTH}x{HEIGHT}) ===")
    if not sizes:
        print("(no clusters >= 3 tiles found in any seed)")
        return
    print(f"Cluster count per sim: mean={statistics.mean(counts):.2f}, "
          f"median={int(statistics.median(counts))}")
    print(f"Cluster size (tiles): min={min(sizes)}, "
          f"median={int(statistics.median(sizes))}, max={max(sizes)}")
    print(f"Cluster length (km, 1 tile = {int(KM_PER_TILE)} km): "
          f"min={int(min(lengths_km))}, "
          f"median={int(statistics.median(lengths_km))}, "
          f"max={int(max(lengths_km))}")
    print(f"Aspect ratio (length/width): min={min(aspects):.2f}, "
          f"median={statistics.median(aspects):.2f}, "
          f"max={max(aspects):.2f}")
    print(f"    Ideal: 5-15 (chains). Lower = blob.")
    print(f"Fill ratio (solidity): min={min(fills):.2f}, "
          f"median={statistics.median(fills):.2f}, "
          f"max={max(fills):.2f}")
    print(f"    Ideal: 0.2-0.5 (thin chain). Higher = blob.")
    if coast_km:
        print(f"Cluster centroid distance from coast (km):")
        print(f"    min={int(min(coast_km))}, "
              f"median={int(statistics.median(coast_km))}, "
              f"max={int(max(coast_km))}")
        print(f"    Real-Earth Andes: ~150-300 km from coast")
    else:
        print("Cluster centroid distance from coast: unavailable "
              "(no clusters reachable from water)")


def report_realism(rows):
    """Existing realism block: mountain%, continents, dominance, time, FLAGS."""
    mtn = [r["mountain_pct"] for r in rows]
    cont = [r["continents"] for r in rows]
    largest = [r["largest_frac"] for r in rows]
    times = [r["elapsed_ms"] for r in rows]

    print(f"=== audit_polygon.py ({WIDTH}x{HEIGHT}, {EPOCHS} epochs, "
          f"{len(rows)} seeds) ===")
    print(f"mountain%: mean={statistics.mean(mtn):.2f} "
          f"median={statistics.median(mtn):.2f} "
          f"min-max={min(mtn):.2f}-{max(mtn):.2f}")
    print(f"continents: mean={statistics.mean(cont):.2f} "
          f"median={int(statistics.median(cont))} "
          f"min-max={min(cont)}-{max(cont)} "
          f"dist={fmt_dist(cont)}")
    print(f"largest/total: mean={statistics.mean(largest):.3f} "
          f"median={statistics.median(largest):.3f} "
          f"min-max={min(largest):.3f}-{max(largest):.3f}")
    print(f"gen_time_ms: mean={int(statistics.mean(times))} "
          f"median={int(statistics.median(times))} "
          f"min-max={int(min(times))}-{int(max(times))}")

    # Worst-case row (largest single-continent dominance + extreme mountain)
    worst_dom = max(rows, key=lambda r: r["largest_frac"])
    worst_mtn = max(rows, key=lambda r: r["mountain_pct"])
    print(f"worst-dominance seed={worst_dom['seed']} "
          f"largest/total={worst_dom['largest_frac']:.3f} "
          f"continents={worst_dom['continents']}")
    print(f"worst-mountain  seed={worst_mtn['seed']} "
          f"mtn%={worst_mtn['mountain_pct']:.2f} "
          f"land={worst_mtn['land']}")

    # Target-bound flagging.
    flag = []
    if statistics.mean(mtn) < 4.0 or statistics.mean(mtn) > 7.0:
        flag.append(f"mountain% mean {statistics.mean(mtn):.2f} outside 4-7")
    if statistics.mean(cont) < 5 or statistics.mean(cont) > 12:
        flag.append(f"continents mean {statistics.mean(cont):.2f} outside 5-12")
    if statistics.mean(largest) > 0.70:
        flag.append(f"largest/total mean {statistics.mean(largest):.3f} > 0.70")
    if statistics.mean(times) > 2000.0:
        flag.append(f"gen_time mean {statistics.mean(times):.0f}ms > 2000")
    if flag:
        print("FLAGS: " + "; ".join(flag))
    else:
        print("FLAGS: none (all metrics in target band)")


def main():
    n, shape_only = parse_args(sys.argv)
    if not BINARY.exists():
        sys.stderr.write(f"error: missing binary at {BINARY}\n")
        sys.exit(1)

    rows = []
    for seed in range(1, n + 1):
        r = run_seed(seed)
        if r is None:
            continue
        rows.append(r)

    if not rows:
        sys.stderr.write("no successful runs\n")
        sys.exit(1)

    if not shape_only:
        report_realism(rows)
    report_shape_stats(rows)


if __name__ == "__main__":
    main()
