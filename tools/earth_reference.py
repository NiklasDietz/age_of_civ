#!/usr/bin/env python3
"""Measure EARTH with mapgen_metrics.py, to anchor the shape-gate bands.

The worldgen programme's shape gates were originally cited from published
figures. That is unsound for coastline statistics, which are ruler-dependent
(Mandelbrot 1967): a band quoted for one resolution says nothing about another.
mapgen_metrics.py says as much in its own notes and asks for exactly this --
"rasterise Natural Earth 1:110m onto this grid and measure it with the same
code".

This does that. It rasterises Natural Earth 1:110m land onto the same grid, via
mapgen_metrics.projection_inverse so the projection cannot disagree, and reports
every shape metric. Validation that the rasterisation is faithful: land fraction
comes out 0.290 against Earth's true 0.292.

MEASURED 2026-08-31, 140x90 Lambert (the gate resolution):

    land fraction            0.290   (true 0.292)
    coast_box_dimension      1.169   band [1.15, 1.25]  -- band CONFIRMED
    perimeter_over_disc      2.326   band [1.20, 2.00]  -- Earth FAILS the band
    inland_depth_over_disc   0.631   band [0.60, 1.15]
    largest / land           0.544   band [0.00, 0.60]
    components               39      coast tiles 1142

Both coastline metrics are strongly resolution-dependent, which is the whole
point: at 280x180 the same Earth reads box dimension 1.102 and
perimeter_over_disc 2.843. A band is therefore only meaningful at the
resolution it was anchored at, and these are anchored at 140x90.

Usage:
  earth_reference.py --geojson PATH [--width 140] [--height 90]

Get the input from the Natural Earth vector repository:
  curl -sSL -o ne_110m_land.geojson \\
    https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_110m_land.geojson
"""

import argparse
import importlib.util
import json
import os
import sys


def _load_metrics():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "mapgen_metrics.py")
    spec = importlib.util.spec_from_file_location("mapgen_metrics", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def load_polygons(geojson_path):
    """Flatten the FeatureCollection to (exterior, holes) rings in lon/lat."""
    with open(geojson_path) as fh:
        data = json.load(fh)
    polys = []
    for feat in data["features"]:
        geom = feat["geometry"]
        rings = ([geom["coordinates"]] if geom["type"] == "Polygon"
                 else geom["coordinates"])
        for ring in rings:
            polys.append((ring[0], ring[1:]))
    return polys


def _point_in_ring(ring, x, y):
    inside = False
    j = len(ring) - 1
    for i in range(len(ring)):
        xi, yi = ring[i][0], ring[i][1]
        xj, yj = ring[j][0], ring[j][1]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi + 1e-300) + xi:
            inside = not inside
        j = i
    return inside


def rasterise(polys, mm, width, height, projection="lambert"):
    """Land mask on the metric's own grid convention: g[row][col]."""
    # Bounding boxes make the point-in-polygon sweep tractable in pure Python.
    boxed = []
    for ext, holes in polys:
        xs = [p[0] for p in ext]
        ys = [p[1] for p in ext]
        boxed.append((min(xs), max(xs), min(ys), max(ys), ext, holes))
    grid = [[False] * width for _ in range(height)]
    for r in range(height):
        for c in range(width):
            ll = mm.projection_inverse(projection, (c + 0.5) / width,
                                       (r + 0.5) / height)
            if ll is None:
                continue
            lat, lon = ll
            for x0, x1, y0, y1, ext, holes in boxed:
                if lon < x0 or lon > x1 or lat < y0 or lat > y1:
                    continue
                if _point_in_ring(ext, lon, lat) and not any(
                        _point_in_ring(h, lon, lat) for h in holes):
                    grid[r][c] = True
                    break
    return grid


def coast_points(grid, width, height):
    pts = []
    for r in range(height):
        for c in range(width):
            if not grid[r][c]:
                continue
            for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                rr = r + dr
                if rr < 0 or rr >= height or not grid[rr][(c + dc) % width]:
                    pts.append((c, r))
                    break
    return pts


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--geojson", required=True, help="ne_110m_land.geojson")
    ap.add_argument("--width", type=int, default=140)
    ap.add_argument("--height", type=int, default=90)
    ap.add_argument("--projection", default="lambert")
    args = ap.parse_args()

    mm = _load_metrics()
    grid = rasterise(load_polygons(args.geojson), mm, args.width, args.height,
                     args.projection)
    W, H = args.width, args.height
    land = sum(1 for row in grid for v in row if v) / float(W * H)
    pts = coast_points(grid, W, H)
    iso = mm.isoperimetric_ratio(grid, W, H, True)
    dep = mm.inland_depth_stats(grid, W, H, True)
    comps = mm.component_cells(grid, W, H, True)
    total = sum(len(x) for x in comps)

    print(f"Earth, Natural Earth 1:110m on {W}x{H} {args.projection}:")
    rows = [
        ("land fraction", land, "land_fraction"),
        ("coast_box_dimension", mm.box_count_dimension(pts, W, H), "coast_box_dimension"),
        ("perimeter_over_disc", iso["perimeter_over_disc"], "perimeter_over_disc"),
        ("inland_depth_over_disc", dep["inland_depth_over_disc"], "inland_depth_over_disc"),
        ("largest / land", max(len(x) for x in comps) / total, "largest_share_of_land"),
    ]
    for label, value, key in rows:
        band = mm.GATES.get(key)
        if band is None:
            print(f"  {label:<24} {value:.3f}")
            continue
        lo, hi, _ = band
        mark = "in band" if lo <= value <= hi else "OUT OF BAND"
        print(f"  {label:<24} {value:.3f}   [{lo}, {hi}]  {mark}")
    print(f"  components               {len(comps)}   coast tiles {len(pts)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
