#!/usr/bin/env python3
"""Extract Earth-tectonic statistical signatures from GPlates rotation
files for procedural sim calibration.

Produces ``data/plate_statistics.csv`` consumed by MapGenerator.cpp at
plate-spawn time. The sim is purely algorithmic — these numbers are
distribution parameters, not seed positions.

Inputs (Müller 2022 1000-Ma reconstruction):
  - tools/plate_data/muller2022/Rotations/1000_0_rotfile.rot

Each line in a .rot file:
  <plate_id> <time_Ma> <pole_lat> <pole_lon> <angle_deg> <ref_plate_id> ! comment

Stats extracted:
  - active_plate_count_at_age(age_Ma) — number of plates with ANY rotation
    record at that time. Drops near present (some plates are young).
  - plate_motion_speed(plate_id, age_Ma) — magnitude of finite-rotation
    angular velocity at that time, in deg/Ma.
  - log-normal fit of motion-speed distribution.
  - active-plate-count time-series → mean/median/p95.
  - rolling birth/death cadence: every 10-Ma window count of plates
    appearing/disappearing.

Outputs ``data/plate_statistics.csv`` and a human-readable
``tools/plate_data/STATISTICS.md`` summary.
"""

from __future__ import annotations

import csv
import math
import os
import statistics
import sys
from collections import defaultdict
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
ROT_FILE = (REPO_ROOT / "tools" / "plate_data" / "muller2022"
            / "Rotations" / "1000_0_rotfile.rot")
OUT_CSV = REPO_ROOT / "data" / "plate_statistics.csv"
OUT_MD = REPO_ROOT / "tools" / "plate_data" / "STATISTICS.md"


def parse_rotation_file(path: Path):
    """Yield (plate_id, time_Ma, pole_lat, pole_lon, angle_deg, ref_id)."""
    with path.open("r", encoding="utf-8") as fp:
        for line_num, raw in enumerate(fp, start=1):
            line = raw.split("!", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) < 6:
                continue
            try:
                plate_id = int(parts[0])
                time_ma = float(parts[1])
                pole_lat = float(parts[2])
                pole_lon = float(parts[3])
                angle_deg = float(parts[4])
                ref_id = int(parts[5])
            except ValueError:
                continue
            yield plate_id, time_ma, pole_lat, pole_lon, angle_deg, ref_id


def collect_per_plate_records():
    """Group rotation records by plate id; sort by time ascending."""
    records = defaultdict(list)
    for plate_id, time_ma, lat, lon, angle, ref in parse_rotation_file(ROT_FILE):
        records[plate_id].append((time_ma, lat, lon, angle, ref))
    for entries in records.values():
        entries.sort(key=lambda r: r[0])
    return records


def haversine_deg(a_lat, a_lon, b_lat, b_lon):
    """Haversine distance between two (lat, lon) on the unit sphere, in
    degrees. Used to compute Euler-pole angular shift between adjacent
    rotation records."""
    a_lat_r = math.radians(a_lat)
    b_lat_r = math.radians(b_lat)
    d_lat = math.radians(b_lat - a_lat)
    d_lon = math.radians(b_lon - a_lon)
    a = (math.sin(d_lat / 2) ** 2
         + math.cos(a_lat_r) * math.cos(b_lat_r) * math.sin(d_lon / 2) ** 2)
    return math.degrees(2 * math.asin(min(1.0, math.sqrt(a))))


def compute_motion_speeds(records):
    """For every (plate, t1, t2) pair with t2 > t1, compute the
    angular speed |dω/dt| in deg/Ma between adjacent rotation records.

    Skips (0, 0, 0, 0) sentinels (plates fixed in their ref frame —
    these are not real motion, they are book-keeping).
    """
    speeds = []
    for plate_id, entries in records.items():
        for prev, cur in zip(entries, entries[1:]):
            t0, lat0, lon0, ang0, _ = prev
            t1, lat1, lon1, ang1, _ = cur
            dt = t1 - t0
            if dt <= 0.0:
                continue
            # Net rotation between two stages: combine pole shift and
            # angle change. We approximate it by the chord between the
            # finite-rotation vectors. Same first-order accuracy as the
            # naive |Δangle| / Δt for small Δt.
            d_pole = haversine_deg(lat0, lon0, lat1, lon1)
            d_angle = abs(ang1 - ang0)
            net = math.hypot(d_pole, d_angle)
            speeds.append(net / dt)
    return speeds


def percentile(values, p):
    if not values:
        return 0.0
    s = sorted(values)
    idx = int(p / 100.0 * (len(s) - 1) + 0.5)
    return s[max(0, min(len(s) - 1, idx))]


def build_active_count_timeseries(records, time_step_ma=10.0,
                                  max_time_ma=1000.0):
    """Estimate active plate count at each 10-Ma slice. A plate is
    considered active at time t if its rotation history brackets t
    (i.e. it has at least one earlier and one later record, OR it has
    a record at t exactly)."""
    times = []
    counts = []
    t = 0.0
    while t <= max_time_ma + 1e-6:
        active = 0
        for entries in records.values():
            if not entries:
                continue
            t_min = entries[0][0]
            t_max = entries[-1][0]
            if t_min - 1e-3 <= t <= t_max + 1e-3:
                active += 1
        times.append(t)
        counts.append(active)
        t += time_step_ma
    return times, counts


def lognormal_fit(values):
    """μ, σ of the natural-log of `values`. Skips zeros (plates with
    sentinel rotation rows)."""
    pos = [v for v in values if v > 1e-9]
    if not pos:
        return 0.0, 0.0
    log_vals = [math.log(v) for v in pos]
    mu = statistics.mean(log_vals)
    sigma = statistics.pstdev(log_vals) if len(log_vals) > 1 else 0.0
    return mu, sigma


def main():
    if not ROT_FILE.exists():
        print(f"error: {ROT_FILE} not found. Re-download via "
              f"tools/plate_data/README.md", file=sys.stderr)
        return 1

    records = collect_per_plate_records()
    speeds = compute_motion_speeds(records)
    times, counts = build_active_count_timeseries(records)
    mu, sigma = lognormal_fit(speeds)

    speed_p50 = percentile(speeds, 50)
    speed_p95 = percentile(speeds, 95)
    speed_max = max(speeds) if speeds else 0.0

    count_p50 = percentile(counts, 50)
    count_p95 = percentile(counts, 95)
    count_min = min(counts) if counts else 0
    count_max = max(counts) if counts else 0

    # Birth/death cadence: count plates whose oldest record falls in
    # each 50-Ma window (births) and whose youngest record falls in
    # each window (deaths). Maps reorganisation events.
    bin_size_ma = 50.0
    n_bins = 21  # 0 to 1050 Ma
    births = [0] * n_bins
    deaths = [0] * n_bins
    for entries in records.values():
        if not entries:
            continue
        oldest = entries[-1][0]  # records sorted ascending; oldest = max
        youngest = entries[0][0]
        b_idx = min(n_bins - 1, int(oldest / bin_size_ma))
        d_idx = min(n_bins - 1, int(youngest / bin_size_ma))
        births[b_idx] += 1
        deaths[d_idx] += 1

    OUT_CSV.parent.mkdir(parents=True, exist_ok=True)
    with OUT_CSV.open("w", newline="") as fp:
        wr = csv.writer(fp)
        wr.writerow(["#metric", "value", "units", "source"])
        wr.writerow(["plate_count_p50", count_p50, "plates",
                     "Müller 2022 PB2002 active-plate-count median"])
        wr.writerow(["plate_count_p95", count_p95, "plates",
                     "Müller 2022 PB2002 active-plate-count p95"])
        wr.writerow(["plate_count_min", count_min, "plates",
                     "Müller 2022 PB2002 active-plate-count min over 1 Ga"])
        wr.writerow(["plate_count_max", count_max, "plates",
                     "Müller 2022 PB2002 active-plate-count max over 1 Ga"])
        wr.writerow(["motion_speed_p50", f"{speed_p50:.4f}", "deg/Ma",
                     "Müller 2022 finite-rotation derivative median"])
        wr.writerow(["motion_speed_p95", f"{speed_p95:.4f}", "deg/Ma",
                     "Müller 2022 finite-rotation derivative p95"])
        wr.writerow(["motion_speed_max", f"{speed_max:.4f}", "deg/Ma",
                     "Müller 2022 finite-rotation derivative max"])
        wr.writerow(["motion_lognormal_mu", f"{mu:.4f}", "ln(deg/Ma)",
                     "Müller 2022 motion log-normal location"])
        wr.writerow(["motion_lognormal_sigma", f"{sigma:.4f}", "ln(deg/Ma)",
                     "Müller 2022 motion log-normal scale"])
        wr.writerow(["#births_per_50Ma", "", "", ""])
        for i, n in enumerate(births):
            t = i * bin_size_ma
            wr.writerow([f"births_{int(t):04d}_Ma", n, "plates",
                         "Müller 2022 plate-births per 50-Ma bin"])
        wr.writerow(["#deaths_per_50Ma", "", "", ""])
        for i, n in enumerate(deaths):
            t = i * bin_size_ma
            wr.writerow([f"deaths_{int(t):04d}_Ma", n, "plates",
                         "Müller 2022 plate-deaths per 50-Ma bin"])
        wr.writerow(["#active_count_timeseries", "", "", ""])
        for t, n in zip(times, counts):
            wr.writerow([f"active_count_{int(t):04d}_Ma", n, "plates",
                         "Müller 2022 active-plate-count time series"])

    md = []
    md.append("# Plate-tectonics statistical signatures\n")
    md.append("Extracted from Müller 2022 1000-Ma rotation file.\n")
    md.append("Generated by `tools/plate_data/extract_statistics.py`.\n\n")
    md.append("| Metric | Value | Units |\n")
    md.append("|---|---|---|\n")
    md.append(f"| Active plate count median | {count_p50} | plates |\n")
    md.append(f"| Active plate count p95 | {count_p95} | plates |\n")
    md.append(f"| Active plate count min/max | {count_min} / {count_max} | plates |\n")
    md.append(f"| Motion speed median | {speed_p50:.3f} | deg/Ma |\n")
    md.append(f"| Motion speed p95 | {speed_p95:.3f} | deg/Ma |\n")
    md.append(f"| Motion log-normal μ | {mu:.3f} | ln(deg/Ma) |\n")
    md.append(f"| Motion log-normal σ | {sigma:.3f} | ln(deg/Ma) |\n")
    md.append(f"| Total speed records | {len(speeds)} | — |\n")
    md.append(f"| Total plate IDs | {len(records)} | — |\n")
    md.append("\n## Generator-side calibration targets\n")
    md.append(f"- Initial plate-count seed: draw from "
              f"truncated normal centred on {count_p50} (±{count_p95 - count_p50}).\n")
    md.append(f"- Per-plate angular velocity: log-normal "
              f"(μ={mu:.3f}, σ={sigma:.3f}).\n")
    md.append(f"- Plate births per 50-Ma window: cluster around peaks "
              f"(supercontinent breakup events).\n")

    OUT_MD.write_text("".join(md), encoding="utf-8")
    print(f"wrote {OUT_CSV} ({len(speeds)} speed records, "
          f"{len(records)} plate IDs)")
    print(f"wrote {OUT_MD}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
