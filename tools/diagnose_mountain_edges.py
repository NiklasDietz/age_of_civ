#!/usr/bin/env python3
"""
diagnose_mountain_edges.py — Phase 13d-A2 step 2.

Reads the per-mountain CSV emitted by `aoc_mapgen --dump-mountain-edges`
(one path or many globbed) and quantifies how mountain placement
relates to plate-boundary classification. Goal: confirm or refute the
seed-1 raw finding that 100% of mountains' nearest edge is type 0
(unclassified), which would prove the edge classifier — not peakSample
window leak — is the dominant cause of "mountains in strange positions".

Edge-type semantics (MapGenerator.cpp:1080-1101):
    0 = unknown / open boundary
    1 = spreading ridge (divergent)
    2 = subduction trench (convergent ocean-cont or ocean-ocean)
    3 = transform fault
    4 = collision suture (continental-continental convergent)

Real Earth: mountains arise at types 2 and 4 (Andes / Himalaya / Alps).
Type 1 + 3 should rarely produce subaerial mountains; type 0 indicates
the classifier failed to identify the boundary motion.

Inputs:
  --files <path...>   one or more mountain-edge CSVs (one per seed)

Output (stdout):
  Per-file row: seed, total_mountains, edge_type histogram (counts +
  percent), median + 90th-percentile distance to nearest edge, anomaly
  score (% mountains whose nearest edge is type 0/1/3 — i.e. NOT a
  convergent type 2/4 boundary).

  Aggregate: same metrics summed across all input files.
"""
from __future__ import annotations

import argparse
import csv
import os
import sys
from collections import Counter
from typing import Iterable, List, Tuple


EXPECTED_CONVERGENT_TYPES = {2, 4}


def percentile(values: List[float], p: float) -> float:
    if not values:
        return 0.0
    s = sorted(values)
    if p <= 0.0:
        return s[0]
    if p >= 1.0:
        return s[-1]
    idx = int(round(p * (len(s) - 1)))
    return s[idx]


def read_file(path: str) -> List[Tuple[int, float, int]]:
    """Returns list of (edge_type, distance_km, owner_plate_id) per row."""
    out = []
    with open(path, newline="") as fp:
        r = csv.DictReader(fp)
        for row in r:
            try:
                et = int(row["nearest_edge_type"])
                dk = float(row["nearest_edge_distance_km"])
                op = int(row["owner_plate_id"])
            except (KeyError, ValueError):
                continue
            out.append((et, dk, op))
    return out


def summarise(rows: List[Tuple[int, float, int]]) -> dict:
    total = len(rows)
    types = Counter(r[0] for r in rows)
    distances = [r[1] for r in rows if r[1] >= 0.0]
    convergent = sum(1 for r in rows if r[0] in EXPECTED_CONVERGENT_TYPES)
    anomaly = total - convergent
    return {
        "total":           total,
        "type_counts":     dict(types),
        "median_km":       percentile(distances, 0.5),
        "p90_km":          percentile(distances, 0.9),
        "max_km":          percentile(distances, 1.0),
        "convergent":      convergent,
        "anomaly":         anomaly,
        "anomaly_pct":     (100.0 * anomaly / total) if total else 0.0,
    }


def fmt_type_hist(types: dict, total: int) -> str:
    parts = []
    for et in sorted(types.keys()):
        c = types[et]
        pct = (100.0 * c / total) if total else 0.0
        parts.append(f"t{et}={c}({pct:.0f}%)")
    return " ".join(parts)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--files", nargs="+", required=True,
                    help="one or more mountain-edge CSVs")
    args = ap.parse_args()

    print(f"{'file':<32} {'mtn':>5} {'med_km':>7} {'p90_km':>7} "
          f"{'anom%':>6} {'edge_type_hist':<30}")
    aggregate: List[Tuple[int, float, int]] = []
    for path in args.files:
        rows = read_file(path)
        s = summarise(rows)
        name = os.path.basename(path)
        print(f"{name:<32} {s['total']:>5} {s['median_km']:>7.0f} "
              f"{s['p90_km']:>7.0f} {s['anomaly_pct']:>5.1f}% "
              f"{fmt_type_hist(s['type_counts'], s['total']):<30}")
        aggregate.extend(rows)

    if len(args.files) > 1:
        s = summarise(aggregate)
        print()
        print(f"AGGREGATE  total_mountains={s['total']}  "
              f"convergent(t2/t4)={s['convergent']}  "
              f"anomaly={s['anomaly']} ({s['anomaly_pct']:.1f}%)")
        print(f"distance med={s['median_km']:.0f} km  "
              f"p90={s['p90_km']:.0f} km  max={s['max_km']:.0f} km")
        print(f"edge_type histogram: "
              f"{fmt_type_hist(s['type_counts'], s['total'])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
