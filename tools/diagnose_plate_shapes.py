#!/usr/bin/env python3
"""
diagnose_plate_shapes.py — Phase 13d-A2 step 1.

Reads the per-plate + per-edge CSVs emitted by `aoc_mapgen --dump-plates`
and `aoc_mapgen --dump-edges` and computes spherical-shape metrics for
each plate. Goal: surface whether the plates produced by the current
sim look like real Earth plates (highly elongated, irregular) or like
near-circular Voronoi blobs (the user's complaint).

Inputs:
  --plates PATH   plate-level CSV (one row per plate)
  --edges PATH    boundary-edge CSV (vertices in normalised world frame)

Output (stdout):
  Per-plate table with:
    plate_id, vertex_count, area_sphere_sr, perimeter_sphere_rad,
    compactness, pca_aspect, classification
  Aggregate: mean compactness, median compactness, mean PCA aspect,
  count of "blob-like" plates (compactness > 0.85), count of
  "thin-strip" plates (compactness < 0.20).

Compactness: Polsby-Popper isoperimetric ratio 4*pi*A / P^2 on the
unit sphere. 1.0 = perfect circle (geodesic disc); lower = elongated
or lobed. Real Earth plates: Pacific ~0.45, Eurasia ~0.30, Africa
~0.55; an "all-blobs" sim would land most plates around 0.85-0.95.

PCA aspect: ratio of principal-axis lengths (major / minor) of the
polygon vertex cloud projected onto the local tangent plane at the
plate centroid. 1.0 = isotropic blob; >> 1 = elongated.

Mollweide inverse formulas:
    x = (mapX*2 - 1) * 2*sqrt(2)
    y = (mapY*2 - 1) * sqrt(2)
    theta = asin(y / sqrt(2))
    lat = asin((2*theta + sin(2*theta)) / pi)
    lon = pi*x / (2*sqrt(2)*cos(theta))
The unit-square parametrisation matches mollweideForward / mollweideInverse
in include/aoc/map/gen/SphereGeometry.hpp.
"""
from __future__ import annotations

import argparse
import csv
import math
import sys
from collections import defaultdict
from typing import Dict, List, Tuple


SQRT_2 = math.sqrt(2.0)
TWO_SQRT_2 = 2.0 * SQRT_2


def mollweide_inverse(map_x: float, map_y: float) -> Tuple[float, float] | None:
    """Returns (lat_rad, lon_rad) or None if (map_x, map_y) is outside
    the projection ellipse."""
    x = (map_x * 2.0 - 1.0) * TWO_SQRT_2
    y = (map_y * 2.0 - 1.0) * SQRT_2
    if y < -SQRT_2 or y > SQRT_2:
        return None
    s = y / SQRT_2
    if s < -1.0 or s > 1.0:
        return None
    theta = math.asin(s)
    lat_arg = (2.0 * theta + math.sin(2.0 * theta)) / math.pi
    if lat_arg < -1.0 or lat_arg > 1.0:
        return None
    lat = math.asin(lat_arg)
    cos_theta = math.cos(theta)
    if abs(cos_theta) < 1e-9:
        # Pole singularity
        return (lat, 0.0)
    lon = math.pi * x / (TWO_SQRT_2 * cos_theta)
    if lon < -math.pi - 1e-6 or lon > math.pi + 1e-6:
        return None
    # Clamp residual numeric drift
    if lon < -math.pi:
        lon = -math.pi
    elif lon > math.pi:
        lon = math.pi
    return (lat, lon)


def latlon_to_vec3(lat_rad: float, lon_rad: float) -> Tuple[float, float, float]:
    cos_lat = math.cos(lat_rad)
    return (cos_lat * math.cos(lon_rad),
            cos_lat * math.sin(lon_rad),
            math.sin(lat_rad))


def haversine_rad(lat_a: float, lon_a: float,
                  lat_b: float, lon_b: float) -> float:
    dlat = lat_b - lat_a
    dlon = lon_b - lon_a
    s = (math.sin(dlat * 0.5) ** 2
         + math.cos(lat_a) * math.cos(lat_b)
         * math.sin(dlon * 0.5) ** 2)
    s = max(0.0, min(1.0, s))
    return 2.0 * math.asin(math.sqrt(s))


def spherical_triangle_area(a: Tuple[float, float, float],
                            b: Tuple[float, float, float],
                            c: Tuple[float, float, float]) -> float:
    """L'Huilier's theorem: spherical excess of triangle (a, b, c) on
    the unit sphere given as Cartesian unit vectors. Returns the
    absolute solid-angle area (steradians)."""
    def arc(u, v):
        d = max(-1.0, min(1.0, u[0] * v[0] + u[1] * v[1] + u[2] * v[2]))
        return math.acos(d)
    sa = arc(b, c)
    sb = arc(a, c)
    sc = arc(a, b)
    s = 0.5 * (sa + sb + sc)
    t = (math.tan(s * 0.5)
         * math.tan((s - sa) * 0.5)
         * math.tan((s - sb) * 0.5)
         * math.tan((s - sc) * 0.5))
    if t <= 0.0:
        return 0.0
    return 4.0 * math.atan(math.sqrt(t))


def spherical_polygon_area(vertices_latlon: List[Tuple[float, float]]) -> float:
    """Triangle-fan from the polygon centroid (mean unit vector). Each
    triangle's area is summed without sign because the script treats
    plate polygons as unsigned regions on the sphere; for simple non-
    self-intersecting rings on a hemisphere this matches the standard
    spherical-excess formula. For polygons that span more than a
    hemisphere the result becomes (4*pi - true_area), which the caller
    should bear in mind when interpreting compactness for global-scale
    plates -- mark them via vertex_count + perimeter heuristics."""
    n = len(vertices_latlon)
    if n < 3:
        return 0.0
    pts = [latlon_to_vec3(la, lo) for (la, lo) in vertices_latlon]
    cx = sum(p[0] for p in pts) / n
    cy = sum(p[1] for p in pts) / n
    cz = sum(p[2] for p in pts) / n
    cn = math.sqrt(cx * cx + cy * cy + cz * cz)
    if cn < 1e-9:
        # Vertices average to origin -- the polygon roughly covers a
        # full hemisphere. Fall back to half-sphere area.
        return 2.0 * math.pi
    centroid = (cx / cn, cy / cn, cz / cn)
    total = 0.0
    for i in range(n):
        a = pts[i]
        b = pts[(i + 1) % n]
        total += spherical_triangle_area(centroid, a, b)
    return total


def spherical_perimeter(vertices_latlon: List[Tuple[float, float]]) -> float:
    n = len(vertices_latlon)
    if n < 2:
        return 0.0
    total = 0.0
    for i in range(n):
        j = (i + 1) % n
        la, lo = vertices_latlon[i]
        lb, lob = vertices_latlon[j]
        total += haversine_rad(la, lo, lb, lob)
    return total


def pca_aspect_ratio(vertices_latlon: List[Tuple[float, float]],
                     centroid_latlon: Tuple[float, float]) -> float:
    """Project vertices into the tangent plane at the centroid, fit a
    2x2 covariance matrix, return sqrt(eig_max / eig_min)."""
    if len(vertices_latlon) < 3:
        return 1.0
    cla, clo = centroid_latlon
    cos_clat = math.cos(cla)
    pts = []
    for (la, lo) in vertices_latlon:
        d_lat = la - cla
        d_lon = lo - clo
        if d_lon > math.pi:
            d_lon -= 2.0 * math.pi
        elif d_lon < -math.pi:
            d_lon += 2.0 * math.pi
        # East-north tangent components.
        e = d_lon * cos_clat
        n = d_lat
        pts.append((e, n))
    # Mean-centre.
    me = sum(p[0] for p in pts) / len(pts)
    mn = sum(p[1] for p in pts) / len(pts)
    sxx = syy = sxy = 0.0
    for (e, n) in pts:
        de = e - me
        dn = n - mn
        sxx += de * de
        syy += dn * dn
        sxy += de * dn
    sxx /= len(pts)
    syy /= len(pts)
    sxy /= len(pts)
    trace = sxx + syy
    det = sxx * syy - sxy * sxy
    disc = max(0.0, trace * trace * 0.25 - det)
    sqrt_disc = math.sqrt(disc)
    eig_max = trace * 0.5 + sqrt_disc
    eig_min = trace * 0.5 - sqrt_disc
    if eig_min < 1e-12:
        return float("inf")
    return math.sqrt(eig_max / eig_min)


def classify(compactness: float, aspect: float) -> str:
    if compactness > 0.85:
        return "BLOB"
    if compactness < 0.20:
        return "THIN"
    if aspect > 3.0:
        return "ELONGATED"
    return "OK"


def read_plates(path: str) -> Dict[int, Dict]:
    out: Dict[int, Dict] = {}
    with open(path, newline="") as fp:
        r = csv.DictReader(fp)
        for row in r:
            pid = int(row["plate_id"])
            out[pid] = {
                "lat_deg": float(row["latDeg"]),
                "lon_deg": float(row["lonDeg"]),
                "weight":  float(row["weight"]),
                "land_fraction": float(row["landFraction"]),
                "polygon_vertex_count": int(row["polygon_vertex_count"]),
            }
    return out


def read_edges(path: str) -> Dict[int, List[Tuple[float, float, float, float, int, int]]]:
    """Returns plate_id -> list of (ax, ay, bx, by, edge_type, neighbor_id)
    in the order written by the C++ writer."""
    out: Dict[int, List] = defaultdict(list)
    with open(path, newline="") as fp:
        r = csv.DictReader(fp)
        for row in r:
            pid = int(row["plate_id"])
            out[pid].append((
                float(row["ax"]), float(row["ay"]),
                float(row["bx"]), float(row["by"]),
                int(row["edge_type"]),
                int(row["neighbor_plate_id"]),
            ))
    return out


def reconstruct_polygon_latlon(
    edges: List[Tuple[float, float, float, float, int, int]],
) -> List[Tuple[float, float]] | None:
    """Edges are stored consecutively for each plate (i -> i+1 around
    the ring). The edge_index column from the C++ writer preserves
    order, so the input list is already ordered. Vertex i = (ax_i, ay_i)
    of edge i. The closing vertex equals (bx_last, by_last) which
    coincides with (ax_0, ay_0) for a closed ring."""
    if not edges:
        return None
    pts = []
    skipped = 0
    for (ax, ay, _bx, _by, _et, _nbr) in edges:
        ll = mollweide_inverse(ax, ay)
        if ll is None:
            skipped += 1
            continue
        pts.append(ll)
    if len(pts) < 3:
        return None
    if skipped > 0 and len(pts) < len(edges):
        # Some vertices fell outside the Mollweide ellipse (polar
        # plates). Still usable as long as at least 3 valid vertices.
        pass
    return pts


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--plates", required=True,
                    help="path to --dump-plates CSV")
    ap.add_argument("--edges", required=True,
                    help="path to --dump-edges CSV")
    ap.add_argument("--blob-threshold", type=float, default=0.85)
    ap.add_argument("--thin-threshold", type=float, default=0.20)
    args = ap.parse_args()

    plates = read_plates(args.plates)
    edges_by_plate = read_edges(args.edges)

    rows: List[Dict] = []
    for pid in sorted(plates.keys()):
        meta = plates[pid]
        edges = edges_by_plate.get(pid, [])
        ring = reconstruct_polygon_latlon(edges)
        if ring is None or len(ring) < 3:
            rows.append({
                "plate_id": pid,
                "vertex_count": meta["polygon_vertex_count"],
                "area_sr": 0.0,
                "perimeter_rad": 0.0,
                "compactness": 0.0,
                "pca_aspect": 1.0,
                "classification": "DEGENERATE",
            })
            continue
        area = spherical_polygon_area(ring)
        peri = spherical_perimeter(ring)
        compactness = (4.0 * math.pi * area / (peri * peri)
                       if peri > 1e-9 else 0.0)
        # Centroid for PCA: mean of unit vectors then re-normalise.
        cx = sum(latlon_to_vec3(la, lo)[0] for (la, lo) in ring) / len(ring)
        cy = sum(latlon_to_vec3(la, lo)[1] for (la, lo) in ring) / len(ring)
        cz = sum(latlon_to_vec3(la, lo)[2] for (la, lo) in ring) / len(ring)
        cn = math.sqrt(cx * cx + cy * cy + cz * cz)
        if cn < 1e-9:
            cent_lat, cent_lon = 0.0, 0.0
        else:
            cx /= cn; cy /= cn; cz /= cn
            cent_lat = math.asin(max(-1.0, min(1.0, cz)))
            cent_lon = math.atan2(cy, cx)
        aspect = pca_aspect_ratio(ring, (cent_lat, cent_lon))
        rows.append({
            "plate_id": pid,
            "vertex_count": len(ring),
            "area_sr": area,
            "perimeter_rad": peri,
            "compactness": compactness,
            "pca_aspect": aspect,
            "classification": classify(compactness, aspect),
        })

    # Print per-plate table.
    print(f"{'pid':>3} {'verts':>5} {'area_sr':>9} {'peri_rad':>9} "
          f"{'compact':>7} {'aspect':>7} {'class':>10}")
    for r in rows:
        print(f"{r['plate_id']:>3} {r['vertex_count']:>5} "
              f"{r['area_sr']:>9.4f} {r['perimeter_rad']:>9.4f} "
              f"{r['compactness']:>7.3f} {r['pca_aspect']:>7.2f} "
              f"{r['classification']:>10}")

    # Aggregates.
    valid = [r for r in rows if r["classification"] != "DEGENERATE"]
    if valid:
        comp_sorted = sorted(r["compactness"] for r in valid)
        median_comp = comp_sorted[len(comp_sorted) // 2]
        mean_comp = sum(r["compactness"] for r in valid) / len(valid)
        mean_aspect = sum(min(r["pca_aspect"], 1e6) for r in valid) / len(valid)
        n_blob = sum(1 for r in valid
                     if r["compactness"] > args.blob_threshold)
        n_thin = sum(1 for r in valid
                     if r["compactness"] < args.thin_threshold)
        print()
        print(f"plates_total={len(rows)}  "
              f"plates_valid={len(valid)}")
        print(f"compactness mean={mean_comp:.3f}  "
              f"median={median_comp:.3f}")
        print(f"pca_aspect  mean={mean_aspect:.2f}")
        print(f"BLOB count (compactness > {args.blob_threshold:.2f}): "
              f"{n_blob}/{len(valid)}")
        print(f"THIN count (compactness < {args.thin_threshold:.2f}): "
              f"{n_thin}/{len(valid)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
