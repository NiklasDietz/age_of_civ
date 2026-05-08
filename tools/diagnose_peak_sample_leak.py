#!/usr/bin/env python3
"""
diagnose_peak_sample_leak.py — Phase 13d-A2 step 3.

Quantifies how often the world-frame elevation pass picks up a peak
elevation from a plate other than the tile's owning plate. The
hypothesis (raised by step 2 results, where 4+ seeds had mountains
1000+ km from any plate boundary) is that
`PhysicsGrid::peakSample(halfSearch=14)` reaches across plate edges,
producing "phantom mountains" deep inside neighbouring plates.

Replicates the inner loop at MapGenerator.cpp:3424-3454: for each
mountain tile, walks every plate's PhysicsGrid cell dump and finds
the max surface elevation within ~140 km haversine distance
(halfSearch=14 cells * 10 km). The winning cell's plate_id is the
"winner". Compare to the mountain tile's stored owner.

Inputs (one seed at a time):
  --mountain-edges PATH   CSV from `--dump-mountain-edges`
  --cells GLOB            glob of `--dump-physics-cells` PATH.plateNNN.csv
                          (e.g. `build/plate_diag/m_s7.cells.plate*.csv`)

Output:
  Per-mountain rows: tile_index, owner, winner, distance_km,
  winner_elev_m, MISMATCH flag.
  Aggregate: mismatch %, mean winner-elev, mean distance from tile to
  winning cell's plate centroid.

Performance: cells bucketed into 1° lat x 1° lon grid (180 x 360 = 64k
buckets). Per tile: walk 3x3 buckets (~12 candidate cells each on
average), haversine-rank, take max. Total 100-1000 mtn x ~100 cells.
Pure Python stdlib.
"""
from __future__ import annotations

import argparse
import csv
import glob
import math
import sys
from collections import defaultdict
from typing import Dict, List, Tuple


EARTH_RADIUS_KM = 6371.0
HALFSEARCH_KM = 140.0  # halfSearch=14 cells * 10 km cell size


def haversine_km(lat_a: float, lon_a: float,
                 lat_b: float, lon_b: float) -> float:
    rla = math.radians(lat_a)
    rlb = math.radians(lat_b)
    dlat = rlb - rla
    dlon = math.radians(lon_b - lon_a)
    s = (math.sin(dlat * 0.5) ** 2
         + math.cos(rla) * math.cos(rlb)
         * math.sin(dlon * 0.5) ** 2)
    s = max(0.0, min(1.0, s))
    return 2.0 * EARTH_RADIUS_KM * math.asin(math.sqrt(s))


def load_cells(paths: List[str]) -> Dict[Tuple[int, int], List[Tuple[float, float, int, float]]]:
    """Returns 1°-lat x 1°-lon bucket -> list of (lat, lon, plate_id,
    elev_m). Only active cells (cellActive=1) included."""
    buckets: Dict[Tuple[int, int], List] = defaultdict(list)
    for path in paths:
        with open(path, newline="") as fp:
            r = csv.DictReader(fp)
            for row in r:
                if int(row["active"]) == 0:
                    continue
                lat = float(row["lat"])
                lon = float(row["lon"])
                pid = int(row["plate_id"])
                elev = float(row["surface_elevation_m"])
                if lon > 180.0:
                    lon -= 360.0
                elif lon < -180.0:
                    lon += 360.0
                lat = max(-89.999, min(89.999, lat))
                lat_b = int(math.floor(lat))
                lon_b = int(math.floor(lon))
                buckets[(lat_b, lon_b)].append((lat, lon, pid, elev))
    return buckets


def find_winner(lat: float, lon: float,
                buckets: Dict[Tuple[int, int], List]) -> Tuple[int, float, float] | None:
    """Returns (plate_id, elev_m, distance_km) of the highest-elev cell
    within HALFSEARCH_KM. Searches a 3x3 bucket neighbourhood with
    longitude wrap."""
    lat_b = int(math.floor(lat))
    lon_b = int(math.floor(lon))
    best_elev = -1e30
    best_pid = -1
    best_dist = 0.0
    for dlat in (-1, 0, 1):
        lb = lat_b + dlat
        if lb < -90 or lb > 89:
            continue
        for dlon in (-1, 0, 1):
            wb = lon_b + dlon
            if wb < -180:
                wb += 360
            elif wb > 179:
                wb -= 360
            for (clat, clon, pid, elev) in buckets.get((lb, wb), ()):
                d = haversine_km(lat, lon, clat, clon)
                if d > HALFSEARCH_KM:
                    continue
                if elev > best_elev:
                    best_elev = elev
                    best_pid = pid
                    best_dist = d
    if best_pid < 0:
        return None
    return (best_pid, best_elev, best_dist)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mountain-edges", required=True,
                    help="path to --dump-mountain-edges CSV")
    ap.add_argument("--cells", required=True,
                    help="glob for --dump-physics-cells.plate*.csv files")
    ap.add_argument("--limit", type=int, default=0,
                    help="cap rows printed (0 = no cap, just aggregates)")
    args = ap.parse_args()

    cell_paths = sorted(glob.glob(args.cells))
    if not cell_paths:
        print(f"error: --cells glob '{args.cells}' matched 0 files",
              file=sys.stderr)
        return 1
    print(f"loading {len(cell_paths)} cell files...", file=sys.stderr)
    buckets = load_cells(cell_paths)
    total_cells = sum(len(v) for v in buckets.values())
    print(f"  {total_cells} active cells in {len(buckets)} buckets",
          file=sys.stderr)

    rows = []
    with open(args.mountain_edges, newline="") as fp:
        r = csv.DictReader(fp)
        for row in r:
            rows.append({
                "tile_index": int(row["tile_index"]),
                "lat":        float(row["lat"]),
                "lon":        float(row["lon"]),
                "owner":      int(row["owner_plate_id"]),
                "edge_owner": int(row["nearest_edge_owner"]),
                "edge_dist":  float(row["nearest_edge_distance_km"]),
            })

    if not rows:
        print("no mountain rows in input", file=sys.stderr)
        return 0

    if args.limit > 0:
        print(f"{'tile':>6} {'owner':>5} {'winner':>6} "
              f"{'win_elev':>8} {'win_dist':>8} {'edge_dist':>9}")
    mismatches = 0
    distances = []
    elevs = []
    no_winner = 0
    for row in rows:
        w = find_winner(row["lat"], row["lon"], buckets)
        if w is None:
            no_winner += 1
            continue
        wp, we, wd = w
        elevs.append(we)
        distances.append(wd)
        is_mismatch = (wp != row["owner"]) and (row["owner"] != 255)
        if is_mismatch:
            mismatches += 1
        if args.limit > 0 and len(elevs) <= args.limit:
            flag = "MISMATCH" if is_mismatch else ""
            print(f"{row['tile_index']:>6} {row['owner']:>5} {wp:>6} "
                  f"{we:>8.0f} {wd:>8.1f} {row['edge_dist']:>9.0f} {flag}")

    total = len(rows) - no_winner
    if total == 0:
        print("no winners (no cells within HALFSEARCH_KM of any tile)")
        return 0

    pct = 100.0 * mismatches / total
    distances.sort()
    elevs.sort()
    print()
    print(f"mountain_tiles={len(rows)}  no_winner={no_winner}  "
          f"with_winner={total}")
    print(f"MISMATCH (winner != owner): {mismatches}/{total} = {pct:.1f}%")
    print(f"winner_elev    median={elevs[len(elevs)//2]:.0f} m  "
          f"max={elevs[-1]:.0f} m")
    print(f"winner_distance_km median={distances[len(distances)//2]:.1f}  "
          f"max={distances[-1]:.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
