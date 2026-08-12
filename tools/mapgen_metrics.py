#!/usr/bin/env python3
"""Continent-shape metrics for aoc_mapgen output.

Phase-gate instrument for the continent-realism program: computes land
fraction, land-component size bands, coastline box-count fractal dimension,
coast/mountain-belt orientation histograms, and a stable content hash from
an aoc_mapgen CSV dump (--format csv). The CSV is used instead of ASCII
because feature glyphs (Ice, Reef) mask the underlying terrain there.

Usage:
  mapgen_metrics.py analyze MAP.csv [--projection P] [--flat]
  mapgen_metrics.py baseline --binary BIN --outdir DIR
      [--seeds 42,7,100,200,1234,777] [--width 140] [--height 90]
      [--projection P] [--flat]
  mapgen_metrics.py selftest

`baseline` runs the binary once per seed, analyzes each map, and writes
DIR/metrics.json plus the per-seed CSVs. Compare two metrics.json files
across a change to see what moved.

`selftest` runs the instrument against synthetic reference masks with known
answers. Run it after ANY change here -- this file is the measurement all
worldgen phase gates depend on, and it has been wrong before (see below).

MEASUREMENT CORRECTNESS NOTES
-----------------------------
Two defects invalidated every metric produced before 2026-07-27, including
all committed baselines in tools/mapgen_baselines/:

  1. WATER_TERRAINS held "ShallowWater" while aoc::map::terrainName() emits
     "Shallow Water" (with a space), so every continental-shelf tile was
     counted as LAND. Land fraction was inflated by ~14 points and shelf
     tiles bridged separate landmasses into one component.
  2. hex_neighbours() clamped columns instead of wrapping them, so on a
     cylindrical map any landmass crossing the antimeridian was split in
     two and its shape statistics were meaningless. Measured: a synthetic
     disc reads elongation 1.33 / 1 component when centred and 2.70 /
     2 components when straddling the seam.

Do not compare any metrics.json produced before that fix against one
produced after it.

Note `Ice` is a FeatureType, not a TerrainType, so it never appears in the
CSV Terrain column and is deliberately absent from WATER_TERRAINS. An
ice-covered land tile is still land; ocean under sea ice is still water.
"""

import argparse
import csv
import os
import hashlib
import json
import math
import re
import subprocess
import sys
from collections import deque
from pathlib import Path

# Mirrors aoc::map::isWater() using the exact strings from
# aoc::map::terrainName() (include/aoc/map/Terrain.hpp). `selftest` re-reads
# that header and fails if the two ever drift apart again -- that drift is
# defect 1 above, and it silently corrupted eight committed baselines.
WATER_TERRAINS = {"Ocean", "Coast", "Shallow Water"}
ALL_TERRAINS = {
    "Ocean", "Coast", "Shallow Water", "Desert", "Plains",
    "Grassland", "Tundra", "Snow", "Mountain",
}

# odd-r offset neighbours (odd rows shifted right, matching HexCoord.hpp).
NEIGHBOURS_EVEN = ((1, 0), (-1, 0), (0, -1), (-1, -1), (0, 1), (-1, 1))
NEIGHBOURS_ODD = ((1, 0), (-1, 0), (1, -1), (0, -1), (1, 1), (0, 1))

MIN_ELONGATION_COMPONENT = 50


def load_csv(path):
    """Return (width, height, land, mountain) bool grids indexed [row][col]."""
    cells = {}
    max_col = max_row = 0
    unknown = set()
    with open(path, newline="") as fh:
        for rec in csv.DictReader(fh):
            col, row = int(rec["Col"]), int(rec["Row"])
            terrain = rec["Terrain"]
            if terrain not in ALL_TERRAINS:
                unknown.add(terrain)
            cells[(col, row)] = terrain
            max_col = max(max_col, col)
            max_row = max(max_row, row)
    if unknown:
        # Fail loudly: an unrecognised terrain name means the C++ enum moved
        # and this instrument is now silently misclassifying tiles.
        raise SystemExit(
            f"error: {path} contains terrain names this script does not know: "
            f"{sorted(unknown)}. Update WATER_TERRAINS/ALL_TERRAINS against "
            f"include/aoc/map/Terrain.hpp and re-baseline.")
    width, height = max_col + 1, max_row + 1
    land = [[False] * width for _ in range(height)]
    mountain = [[False] * width for _ in range(height)]
    for (col, row), terrain in cells.items():
        land[row][col] = terrain not in WATER_TERRAINS
        mountain[row][col] = terrain == "Mountain"
    return width, height, land, mountain


def hex_neighbours(col, row, width, height, wrap=True):
    """Odd-r offset neighbours. `wrap` joins the antimeridian seam, which is
    what the game's Cylindrical topology does; without it every seam-crossing
    landmass is reported as two."""
    table = NEIGHBOURS_ODD if row & 1 else NEIGHBOURS_EVEN
    for dc, dr in table:
        nc, nr = col + dc, row + dr
        if not (0 <= nr < height):
            continue
        if wrap:
            nc %= width
        elif not (0 <= nc < width):
            continue
        yield nc, nr


# ---------------------------------------------------------------------------
# Per-tile sphere-area weights
# ---------------------------------------------------------------------------
# A tile-count fraction only equals a planet-area fraction under an equal-area
# projection. Mollweide and Lambert cylindrical are equal-area (so every VALID
# tile carries the same weight); equirectangular is not (row area scales as
# cos(lat)). Mollweide additionally leaves ~21.4% of a rectangular grid outside
# its ellipse, and MapGenerator forces those tiles to ocean -- they are not
# part of the sphere at all, so they are excluded from both numerator and
# denominator rather than counted as sea.

def _mollweide_valid(col, row, width, height):
    nx = (col + 0.5) / width
    ny = (row + 0.5) / height
    dx, dy = (nx - 0.5) * 2.0, (ny - 0.5) * 2.0
    return dx * dx + dy * dy <= 1.0


# Inverse projections, mirroring aoc::map::gen::projectionInverse. Needed so
# orientation can be measured in SPHERE coordinates rather than grid-index
# coordinates -- see local_metric() for why that matters.

def projection_inverse(projection, nx, ny):
    """(nx, ny) in [0,1]^2 -> (latDeg, lonDeg), or None if off the sphere."""
    proj = projection.lower()
    if not (0.0 <= nx <= 1.0 and 0.0 <= ny <= 1.0):
        return None
    lon = nx * 360.0 - 180.0
    if proj in ("lambert", "lambert_equal_area", "equalarea"):
        return math.degrees(math.asin(max(-1.0, min(1.0, 1.0 - 2.0 * ny)))), lon
    if proj == "equirect":
        return 90.0 - ny * 180.0, lon
    if proj == "mollweide":
        dx, dy = (nx - 0.5) * 2.0, (ny - 0.5) * 2.0
        if dx * dx + dy * dy > 1.0:
            return None
        y = (ny - 0.5) * 2.0 * math.sqrt(2.0)
        theta = math.asin(max(-1.0, min(1.0, y / math.sqrt(2.0))))
        sin_lat = (2.0 * theta + math.sin(2.0 * theta)) / math.pi
        lat = math.degrees(math.asin(max(-1.0, min(1.0, sin_lat))))
        cos_theta = math.cos(theta)
        if cos_theta <= 1e-7:
            return lat, 0.0
        x = (nx - 0.5) * 4.0 * math.sqrt(2.0)
        return lat, math.degrees(math.pi * x / (2.0 * math.sqrt(2.0) * cos_theta))
    raise SystemExit(f"error: unknown projection {projection!r}")


EARTH_RADIUS_KM = 6371.0


def _haversine_km(a, b):
    lat1, lon1 = math.radians(a[0]), math.radians(a[1])
    lat2, lon2 = math.radians(b[0]), math.radians(b[1])
    dlat, dlon = lat2 - lat1, lon2 - lon1
    h = (math.sin(dlat / 2) ** 2
         + math.cos(lat1) * math.cos(lat2) * math.sin(dlon / 2) ** 2)
    return 2.0 * EARTH_RADIUS_KM * math.asin(min(1.0, math.sqrt(h)))


def local_metric(projection, width, height):
    """Return f(col, row) -> (km per column step, km per row step).

    Orientation measured in raw grid-index space reports the SAMPLING GRID's
    anisotropy, not the coastline's. Concretely, under Lambert equal-area at
    140x90 an equatorial tile is 286 km east-west by 142 km north-south -- a
    2:1 stretch -- so coastlines staircase preferentially east-west and
    axis_aligned_frac reads ~0.75 in the tropics no matter what the geology
    does. Rescaling the gradient by the local physical step sizes measures the
    tangent in the sphere's own tangent plane, which is the physically
    meaningful question ("do coastlines follow meridians and parallels more
    than chance?") and makes the metric projection-independent.

    Computed by finite differences through the inverse projection so it works
    for any projection without per-projection algebra.
    """
    cache = {}

    def latlon(c, r):
        return projection_inverse(projection, (c + 0.5) / width,
                                  (r + 0.5) / height)

    def metric(col, row):
        # For every cylindrical/pseudocylindrical projection here, the step
        # sizes depend on the row only, so caching by row is exact.
        if row in cache:
            return cache[row]
        here = latlon(col, row)
        if here is None:
            cache[row] = (1.0, 1.0)
            return cache[row]

        # Columns wrap (the grid is a cylinder), so the east-west span is
        # always a full two steps. Clamping instead -- and still dividing by
        # two -- halved the value at col 0 and col width-1 and, because of the
        # row cache, poisoned whole rows with it.
        east = latlon((col + 1) % width, row)
        west = latlon((col - 1) % width, row)
        dx = (_haversine_km(west, east) / 2.0
              if east and west else None)

        # Rows do NOT wrap: at row 0 / row height-1 a central difference is
        # unavailable, so take a one-sided step and do not halve it.
        up = latlon(col, row - 1) if row > 0 else None
        down = latlon(col, row + 1) if row < height - 1 else None
        if up and down:
            dy = _haversine_km(up, down) / 2.0
        elif down:
            dy = _haversine_km(here, down)
        elif up:
            dy = _haversine_km(up, here)
        else:
            dy = None

        # Degenerate (single-row grid, or a projection edge): fall back to
        # isotropic rather than dividing by ~0 and inventing a direction.
        if dx is None or dy is None or dx < 1e-3 or dy < 1e-3:
            cache[row] = (1.0, 1.0)
        else:
            cache[row] = (dx, dy)
        return cache[row]

    return metric


def area_weights(width, height, projection):
    """Return a [row][col] grid of relative sphere-area weights (0 = not on
    the sphere)."""
    proj = projection.lower()
    if proj in ("lambert", "lambert_equal_area", "equalarea"):
        return [[1.0] * width for _ in range(height)]
    if proj == "equirect":
        out = []
        for row in range(height):
            lat = 90.0 - (row + 0.5) * 180.0 / height
            w = max(0.0, math.cos(math.radians(lat)))
            out.append([w] * width)
        return out
    if proj == "mollweide":
        return [[1.0 if _mollweide_valid(c, r, width, height) else 0.0
                 for c in range(width)] for r in range(height)]
    raise SystemExit(f"error: unknown projection {projection!r} "
                     f"(expected lambert, equirect or mollweide)")


def components(mask, width, height, wrap=True):
    """Connected-component sizes over the hex adjacency."""
    return sorted((len(c) for c in component_cells(mask, width, height, wrap)),
                  reverse=True)


def component_cells(mask, width, height, wrap=True):
    """Yield each connected component as a list of (col, row)."""
    seen = [[False] * width for _ in range(height)]
    out = []
    for row in range(height):
        for col in range(width):
            if not mask[row][col] or seen[row][col]:
                continue
            comp = []
            queue = deque([(col, row)])
            seen[row][col] = True
            while queue:
                c, r = queue.popleft()
                comp.append((c, r))
                for nc, nr in hex_neighbours(c, r, width, height, wrap):
                    if mask[nr][nc] and not seen[nr][nc]:
                        seen[nr][nc] = True
                        queue.append((nc, nr))
            out.append(comp)
    return out


def unwrap_columns(cells, width, wrap=True):
    """Shift columns so a seam-crossing component is contiguous.

    A landmass straddling the antimeridian has cells at both ends of the
    column range; any PCA or bounding box over those raw coordinates is
    meaningless (it reports a globe-spanning east-west smear). Cut at the
    widest unoccupied circular arc of columns so the component occupies one
    contiguous span, then translate to origin. PCA is translation-invariant,
    so this only removes the artefact.
    """
    if not wrap or not cells:
        return cells
    occupied = sorted({c for c, _ in cells})
    if len(occupied) < 2:
        return cells
    best_gap, origin = -1, occupied[0]
    for i, a in enumerate(occupied):
        b = occupied[(i + 1) % len(occupied)]
        gap = (b - a) % width
        if gap > best_gap:
            best_gap, origin = gap, b
    return [((c - origin) % width, r) for c, r in cells]


def coastline_cells(land, width, height, wrap=True):
    coast = []
    for row in range(height):
        for col in range(width):
            if not land[row][col]:
                continue
            if any(not land[nr][nc]
                   for nc, nr in hex_neighbours(col, row, width, height, wrap)):
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


def sobel_orientation_bins(cells, land, width, height, wrap=True, metric=None):
    """Boundary-tangent orientation histogram in 4 bins of 45 deg (mod 180).

    Bin 0 = tangent within +-22.5 deg of EAST-WEST, bin 2 = NORTH-SOUTH;
    axis_aligned = bins 0+2, i.e. "how much of the coastline follows parallels
    or meridians more than chance would give".

    CALIBRATION (see `selftest`, which pins these): an isotropic boundary
    reads axis_aligned ~0.50 -- a perfect disc measures 0.506 and randomly
    oriented 4:1 ellipses 0.490. Wall-to-wall axis-aligned bars measure 0.98.
    So 0.50 is the correct null and the excess over it is real anisotropy.

    `metric` supplies (km per column step, km per row step) per cell so the
    gradient is rescaled into the sphere's local tangent plane. WITHOUT it the
    result measures the projection's tile aspect instead of the geology: under
    Lambert equal-area at 140x90 an equatorial tile is 286 x 142 km, and that
    2:1 stretch alone pushes tropical axis_aligned to ~0.75. Pass None only for
    synthetic grid-space controls, where the grid IS the space of interest.
    """
    bins = [0, 0, 0, 0]
    for col, row in cells:
        gx = gy = 0.0
        for dr in (-1, 0, 1):
            for dc in (-1, 0, 1):
                c, r = col + dc, row + dr
                if wrap:
                    c %= width
                inside = 0 <= c < width and 0 <= r < height
                v = 1.0 if (inside and land[r][c]) else 0.0
                kx = dc * (2 if dr == 0 else 1)
                ky = dr * (2 if dc == 0 else 1)
                gx += v * kx
                gy += v * ky
        if gx == 0.0 and gy == 0.0:
            continue
        if metric is not None:
            dx_km, dy_km = metric(col, row)
            # d/d(east) = (d/dcol) / (km per col); likewise north, with the row
            # index increasing southward.
            gx /= dx_km
            gy /= dy_km
        tangent = math.degrees(math.atan2(gy, gx)) + 90.0
        idx = int(((tangent % 180.0) + 22.5) // 45.0) % 4
        bins[idx] += 1
    total = sum(bins)
    if total == 0:
        return None
    return {
        "bins_deg_0_45_90_135": [round(b / total, 4) for b in bins],
        "axis_aligned_frac": round((bins[0] + bins[2]) / total, 4),
        "isotropic_null": 0.50,
        "sphere_metric": metric is not None,
        "samples": total,
    }


def _pca_2d(pts):
    """Principal-axis angle (deg mod 180) and EIGENVALUE (variance) ratio of a
    2D point cloud. Note this is the ratio of variances, i.e. the SQUARE of
    the axis ratio: an 80x2 ribbon reads ~1600, not ~40."""
    n = len(pts)
    if n == 0:
        return 0.0, float("inf")
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


def component_pca_axis_deg(comp_cells):
    """Principal-axis angle (deg mod 180) and eigenvalue ratio of a
    component's cell coordinates; odd rows shifted +0.5 col to undo the
    offset stagger. Caller must pass seam-unwrapped coordinates.

    GRID-INDEX SPACE. Under a non-equal-aspect projection this measures the
    sampling grid as much as the shape -- see component_pca_sphere_axis_ratio,
    which is the projection-independent answer. Kept because the projected
    shape is what the player actually sees on the map.
    """
    return _pca_2d([(c + (0.5 if r & 1 else 0.0), r * 0.866)
                    for c, r in comp_cells])


def _latlon_to_vec(lat_deg, lon_deg):
    la, lo = math.radians(lat_deg), math.radians(lon_deg)
    cl = math.cos(la)
    return (cl * math.cos(lo), cl * math.sin(lo), math.sin(la))


def component_pca_sphere_axis_ratio(comp_cells, projection, width, height):
    """(angle deg mod 180, AXIS ratio) of a component measured on the sphere.

    Why this exists: component_pca_axis_deg() runs on grid indices, so under
    Lambert equal-area -- where a tile is 286 x 142 km at the equator and
    74 x 547 km at 75 deg, a ~15x aspect swing -- a circular continent at high
    latitude reads as strongly elongated and an equatorial one as E-W
    stretched. That is the projection, not the geology. `coast_orientation`
    was already moved into sphere space for exactly this reason; elongation
    was not, so every elongation figure measured before this function existed
    is partly instrument.

    Method: project the component's cells AZIMUTHAL-EQUIDISTANTLY about their
    own centroid (great-circle distance from the centre is preserved exactly,
    which is the quantity a second moment is built from), then take the plane
    PCA in kilometres. Works for components of any size up to a hemisphere and
    needs no seam unwrapping -- great-circle azimuth handles the antimeridian
    natively.

    Returns the AXIS ratio (sqrt of the eigenvalue ratio), so the number is
    directly comparable to Earth's continents at ~1.5-2.5. This differs from
    component_pca_axis_deg(), which returns the variance ratio; the two are
    reported under separate keys and must not be compared to each other.
    """
    pts_ll = []
    for col, row in comp_cells:
        ll = projection_inverse(projection, (col + 0.5) / width,
                                (row + 0.5) / height)
        if ll is not None:
            pts_ll.append(ll)
    if len(pts_ll) < 3:
        return 0.0, float("inf")

    cx = sum(_latlon_to_vec(la, lo)[0] for la, lo in pts_ll)
    cy = sum(_latlon_to_vec(la, lo)[1] for la, lo in pts_ll)
    cz = sum(_latlon_to_vec(la, lo)[2] for la, lo in pts_ll)
    norm = math.sqrt(cx * cx + cy * cy + cz * cz)
    if norm < 1e-9:
        # Cells spread symmetrically enough that the mean vector vanishes (a
        # full zonal band). No meaningful centre, so no meaningful axis.
        return 0.0, float("inf")
    c_lat = math.degrees(math.asin(max(-1.0, min(1.0, cz / norm))))
    c_lon = math.degrees(math.atan2(cy, cx))

    lat1 = math.radians(c_lat)
    sin1, cos1 = math.sin(lat1), math.cos(lat1)
    plane = []
    for la, lo in pts_ll:
        lat2 = math.radians(la)
        dlon = math.radians(lo - c_lon)
        sin2, cos2 = math.sin(lat2), math.cos(lat2)
        cos_d = sin1 * sin2 + cos1 * cos2 * math.cos(dlon)
        d_km = EARTH_RADIUS_KM * math.acos(max(-1.0, min(1.0, cos_d)))
        az = math.atan2(cos2 * math.sin(dlon),
                        cos1 * sin2 - sin1 * cos2 * math.cos(dlon))
        # x = east, y = south, matching the grid-space convention (row index
        # increases southward) so the two angles are directly comparable.
        plane.append((d_km * math.sin(az), -d_km * math.cos(az)))
    angle, var_ratio = _pca_2d(plane)
    return angle, (math.sqrt(var_ratio) if math.isfinite(var_ratio)
                   else float("inf"))


def _weighted_median(pairs):
    """Median of `value` weighted by `weight`, over [(value, weight), ...]."""
    if not pairs:
        return None
    ordered = sorted(pairs)
    half = sum(w for _, w in ordered) / 2.0
    acc = 0.0
    for value, weight in ordered:
        acc += weight
        if acc >= half:
            return value
    return ordered[-1][0]


def mountain_belt_stats(mountain, width, height, wrap=True):
    comps = component_cells(mountain, width, height, wrap)
    belts = [c for c in comps if len(c) >= 3]
    if not belts:
        return {"n_components": len(comps), "n_belts": 0}
    bins = [0, 0, 0, 0]
    pairs = []
    for comp in belts:
        angle, ratio = component_pca_axis_deg(unwrap_columns(comp, width, wrap))
        bins[int((angle + 22.5) // 45.0) % 4] += 1
        if math.isfinite(ratio):
            pairs.append((ratio, float(len(comp))))
    total = sum(bins)
    median = _weighted_median(pairs)
    return {
        "n_components": len(comps),
        "n_belts": total,
        "belt_axis_bins_0_45_90_135": bins,
        "belt_axis_aligned_frac": round((bins[0] + bins[2]) / total, 4),
        "median_elongation": round(median, 2) if median is not None else None,
    }


def _elongation_block(pairs, degenerate):
    if not pairs and not degenerate:
        return None
    median = _weighted_median(pairs)
    ratios = sorted(r for r, _ in pairs)
    return {
        "median": round(median, 2) if median is not None else None,
        "p90": round(ratios[int(0.9 * (len(ratios) - 1))], 2) if ratios else None,
        "max": round(ratios[-1], 2) if ratios else None,
        "n_components": len(pairs),
        "n_degenerate": degenerate,
    }


def landmass_elongation(land, width, height, wrap=True, projection=None):
    """Area-weighted median elongation of land components.

    Two numbers, deliberately kept apart because they answer different
    questions and are on different scales:

      sphere_axis_ratio -- AXIS ratio measured on the sphere in kilometres
          (component_pca_sphere_axis_ratio). Projection-independent, so this
          is the one to compare against Earth's continents at ~1.5-2.5 and the
          one to gate physics changes on. Requires `projection`.
      grid_variance_ratio -- the legacy grid-index EIGENVALUE ratio. It is the
          square of an axis ratio AND it carries the projection's tile aspect,
          so a value here is not comparable to the one above. Kept because it
          describes the shape the player sees on the projected map, and
          because every committed baseline is in these units.

    Both use a weighted MEDIAN, not a mean: the ratio is unbounded (a 1-tile
    ribbon returns infinity), so a mean is set by whichever thin marginal
    strip happens to exist. `n_degenerate` counts components too thin to have
    a finite ratio -- a rising count there is itself the signal a mean would
    have buried.
    """
    grid_pairs, grid_degenerate = [], 0
    sph_pairs, sph_degenerate = [], 0
    for comp in component_cells(land, width, height, wrap):
        if len(comp) < MIN_ELONGATION_COMPONENT:
            continue
        weight = float(len(comp))
        _, ratio = component_pca_axis_deg(unwrap_columns(comp, width, wrap))
        if math.isfinite(ratio):
            grid_pairs.append((ratio, weight))
        else:
            grid_degenerate += 1
        if projection is not None:
            _, sratio = component_pca_sphere_axis_ratio(comp, projection,
                                                        width, height)
            if math.isfinite(sratio):
                sph_pairs.append((sratio, weight))
            else:
                sph_degenerate += 1
    grid = _elongation_block(grid_pairs, grid_degenerate)
    if grid is None:
        return None
    # Legacy keys stay at the top level so existing baselines and any reader
    # of `["landmass_elongation"]["median"]` keep working unchanged.
    out = dict(grid)
    out["units"] = "grid-index eigenvalue (variance) ratio"
    if projection is not None:
        out["sphere_axis_ratio"] = _elongation_block(sph_pairs, sph_degenerate)
    return out


def analyze(csv_path, projection="lambert", wrap=True):
    width, height, land, mountain = load_csv(csv_path)
    weights = area_weights(width, height, projection)
    land_area = sum(weights[r][c]
                    for r in range(height) for c in range(width)
                    if land[r][c])
    sphere_area = sum(sum(row) for row in weights)
    land_cells = sum(row.count(True) for row in land)
    comp_sizes = components(land, width, height, wrap)
    coast = coastline_cells(land, width, height, wrap)
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
        "projection": projection,
        "wrap": wrap,
        # Area-weighted: this is the fraction of the PLANET that is land, and
        # is the number to compare against Earth's 0.292. Under a non-equal-
        # area projection it differs from the raw tile fraction below.
        "land_fraction": round(land_area / sphere_area, 4) if sphere_area else None,
        "land_fraction_raw_tiles": round(land_cells / (width * height), 4),
        "sphere_tile_fraction": round(sphere_area / (width * height), 4),
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
        "coast_orientation": sobel_orientation_bins(
            coast, land, width, height, wrap,
            metric=local_metric(projection, width, height)),
        "mountains": mountain_belt_stats(mountain, width, height, wrap),
        "landmass_elongation": landmass_elongation(land, width, height, wrap,
                                                   projection=projection),
        # Share of all land in the biggest landmass, and how many landmasses
        # are big enough to matter. Earth: 0.57 and 6-7 respectively. Both are
        # gated, and neither was computed before 2026-08-12 -- "one giant blob"
        # was visible in every rendered map while the metrics JSON had no field
        # that could express it.
        "largest_share_of_land": (round(comp_sizes[0] / land_cells, 4)
                                  if comp_sizes and land_cells else None),
        "big_landmasses": sum(1 for s in comp_sizes
                              if land_cells and s / land_cells > 0.02),
    }


# ---------------------------------------------------------------------------
# selftest
# ---------------------------------------------------------------------------

def _blank(width, height):
    return [[False] * width for _ in range(height)]


def _sphere_cap(width, height, projection, lat_deg, lon_deg, radius_deg):
    """Mask of every tile whose centre lies within `radius_deg` of (lat, lon).

    A genuine spherical cap: round on the sphere at any latitude, arbitrarily
    distorted on the projected grid. That contrast is what makes it the right
    control for a shape metric.
    """
    g = _blank(width, height)
    centre = _latlon_to_vec(lat_deg, lon_deg)
    cos_r = math.cos(math.radians(radius_deg))
    for r in range(height):
        for c in range(width):
            ll = projection_inverse(projection, (c + 0.5) / width,
                                    (r + 0.5) / height)
            if ll is None:
                continue
            v = _latlon_to_vec(*ll)
            if sum(a * b for a, b in zip(v, centre)) >= cos_r:
                g[r][c] = True
    return g


def _disc(width, height, cx, cy, radius, wrap=True):
    g = _blank(width, height)
    for r in range(height):
        for c in range(width):
            dc = abs(c - cx)
            if wrap:
                dc = min(dc, width - dc)
            if dc * dc + (r - cy) ** 2 < radius * radius:
                g[r][c] = True
    return g


def _check(label, got, want, tol):
    ok = got is not None and abs(got - want) <= tol
    print(f"  [{'ok' if ok else 'FAIL'}] {label}: got {got}, want {want} +-{tol}")
    return ok


def cmd_selftest(_args):
    """Pin the instrument against masks whose answers are known a priori.

    Every worldgen phase gate is read off this file, so a silent regression
    here is indistinguishable from a physics regression. These cases caught
    the two defects documented at the top of this module.
    """
    W, H = 140, 90
    ok = True
    print("terrain-name agreement with include/aoc/map/Terrain.hpp:")
    header = Path(__file__).resolve().parents[1] / "include/aoc/map/Terrain.hpp"
    if not header.exists():
        print(f"  [FAIL] cannot read {header}")
        ok = False
    else:
        text = header.read_text()
        # Tolerate whitespace/newlines between the two braces: clang-format
        # wraps the initializer once the name list grows past the column limit,
        # and a `\{\{` pattern would silently match nothing and report every
        # terrain as missing rather than failing loudly.
        block = re.search(r"NAMES\s*=\s*\{\s*\{(.*?)\}\s*\}", text, re.S)
        names = set(re.findall(r'"([^"]+)"', block.group(1))) if block else set()
        water = re.search(r"constexpr bool isWater\(TerrainType type\)\s*\{(.*?)\}",
                          text, re.S)
        water_enums = set(re.findall(r"TerrainType::(\w+)", water.group(1))) \
            if water else set()
        # enum order in the header maps 1:1 onto the NAMES array, so zipping
        # them recovers enum -> display-name and hence isWater() -> names.
        enum_block = re.search(r"enum class TerrainType\s*:\s*uint8_t\s*\{(.*?)\}",
                               text, re.S)
        enum_names = [e for e in re.findall(r"^\s*(\w+),", enum_block.group(1),
                                           re.M) if e != "Count"] \
            if enum_block else []
        want_water = set()
        if block and enum_names:
            pairs = dict(zip(enum_names,
                             re.findall(r'"([^"]+)"', block.group(1))))
            want_water = {pairs[e] for e in water_enums if e in pairs}
        ok &= _check("terrain name count", float(len(names)),
                     float(len(ALL_TERRAINS)), 0.0)
        if names != ALL_TERRAINS:
            print(f"  [FAIL] ALL_TERRAINS drifted from the header: "
                  f"only-here={sorted(ALL_TERRAINS - names)} "
                  f"only-header={sorted(names - ALL_TERRAINS)}")
            ok = False
        else:
            print("  [ok] ALL_TERRAINS matches terrainName()")
        if want_water and want_water != WATER_TERRAINS:
            print(f"  [FAIL] WATER_TERRAINS drifted from isWater(): "
                  f"only-here={sorted(WATER_TERRAINS - want_water)} "
                  f"only-header={sorted(want_water - WATER_TERRAINS)}")
            ok = False
        elif want_water:
            print("  [ok] WATER_TERRAINS matches isWater()")

    print("orientation histogram null (isotropic must read ~0.50):")
    g = _disc(W, H, 70, 45, 25)
    r = sobel_orientation_bins(coastline_cells(g, W, H), g, W, H)
    ok &= _check("perfect disc", r["axis_aligned_frac"], 0.506, 0.02)

    import random
    random.seed(3)
    g = _blank(W, H)
    for _ in range(14):
        cx, cy = random.uniform(18, 122), random.uniform(15, 75)
        th = random.uniform(0, math.pi)
        for rr in range(H):
            for cc in range(W):
                dx, dy = cc - cx, rr - cy
                u = dx * math.cos(th) + dy * math.sin(th)
                v = -dx * math.sin(th) + dy * math.cos(th)
                if (u / 14.0) ** 2 + (v / 3.5) ** 2 < 1:
                    g[rr][cc] = True
    r = sobel_orientation_bins(coastline_cells(g, W, H), g, W, H)
    ok &= _check("4:1 ellipses, random orientation", r["axis_aligned_frac"],
                 0.490, 0.03)

    print("orientation histogram saturation (bars must read ~0.98):")
    g = _blank(W, H)
    for x0 in (20, 50, 80, 110):
        for rr in range(5, 85):
            for cc in range(x0, x0 + 6):
                g[rr][cc] = True
    r = sobel_orientation_bins(coastline_cells(g, W, H), g, W, H)
    ok &= _check("N-S bars", r["axis_aligned_frac"], 0.976, 0.03)

    g = _blank(W, H)
    for y0 in (15, 35, 55, 75):
        for rr in range(y0, y0 + 5):
            for cc in range(5, 135):
                g[rr][cc] = True
    r = sobel_orientation_bins(coastline_cells(g, W, H), g, W, H)
    ok &= _check("E-W bars", r["axis_aligned_frac"], 0.985, 0.03)

    print("antimeridian seam (defect 2: a seam-crossing disc is ONE landmass):")
    centred = _disc(W, H, 70, 45, 25)
    seam = _disc(W, H, 0, 45, 25)
    n_centred = len(components(centred, W, H, wrap=True))
    n_seam = len(components(seam, W, H, wrap=True))
    ok &= _check("centred disc components", float(n_centred), 1.0, 0.0)
    ok &= _check("seam disc components", float(n_seam), 1.0, 0.0)
    e_centred = landmass_elongation(centred, W, H, wrap=True)["median"]
    e_seam = landmass_elongation(seam, W, H, wrap=True)["median"]
    ok &= _check("centred disc elongation", e_centred, 1.33, 0.15)
    ok &= _check("seam disc elongation (must match centred)", e_seam,
                 e_centred, 0.15)
    print("  (without the wrap fix the seam disc read 2 components / 2.70)")
    ok &= _check("seam disc components WITHOUT wrap (regression witness)",
                 float(len(components(seam, W, H, wrap=False))), 2.0, 0.0)

    print("sphere metric (must recover real tile sizes, not grid indices):")
    lam = local_metric("lambert", 140, 90)
    dx_eq, dy_eq = lam(70, 45)
    ok &= _check("lambert equatorial km/col", dx_eq, 286.0, 12.0)
    ok &= _check("lambert equatorial km/row", dy_eq, 142.0, 12.0)
    eqr = local_metric("equirect", 140, 90)
    dx_60, _ = eqr(70, 15)   # row 15 -> lat +60 under equirect
    ok &= _check("equirect km/col at lat 60 (cos-scaled)", dx_60, 143.0, 12.0)
    # The metric must REMOVE the 2:1 equatorial stretch: an east-west
    # staircase that reads strongly axis-aligned in grid space must read less
    # so once rescaled into the sphere tangent plane.
    g = _blank(W, H)
    for rr in range(43, 48):
        for cc in range(20, 120):
            g[rr][cc] = True
    coast = coastline_cells(g, W, H)
    raw = sobel_orientation_bins(coast, g, W, H)["axis_aligned_frac"]
    sph = sobel_orientation_bins(coast, g, W, H, metric=lam)["axis_aligned_frac"]
    print(f"  [info] E-W slab: grid-space {raw}, sphere-space {sph}")
    ok &= _check("sphere metric does not invent anisotropy (still E-W)",
                 sph, raw, 0.25)

    print("area weighting:")
    wl = area_weights(8, 8, "lambert")
    ok &= _check("lambert is uniform", min(min(r) for r in wl), 1.0, 0.0)
    wm = area_weights(140, 90, "mollweide")
    frac = sum(sum(r) for r in wm) / (140 * 90)
    ok &= _check("mollweide valid-tile fraction (~pi/4)", frac, 0.7854, 0.01)
    we = area_weights(140, 90, "equirect")
    ok &= _check("equirect mean weight (~2/pi)",
                 sum(sum(r) for r in we) / (140 * 90), 0.6366, 0.01)

    print("elongation estimator robustness:")
    g = _blank(W, H)
    for cc in range(20, 100):
        g[45][cc] = True          # 1-tile ribbon -> infinite ratio
    for rr in range(20, 40):
        for cc in range(20, 40):
            g[rr][cc] = True      # compact blob -> ~1
    res = landmass_elongation(g, W, H, wrap=True)
    ok &= _check("degenerate ribbon counted, not averaged in",
                 float(res["n_degenerate"]), 1.0, 0.0)
    ok &= _check("median unaffected by the ribbon", res["median"], 1.0, 0.35)

    # The instrument's own defect: a spherical cap is round at EVERY latitude,
    # so any latitude dependence in the reported elongation is the projection
    # leaking into the measurement. Grid space fails this badly under Lambert;
    # sphere space must not.
    print("elongation is projection-independent (spherical caps):")
    grid_vals, sphere_vals = [], []
    for lat in (0.0, 45.0, 75.0):
        cap = _sphere_cap(W, H, "lambert", lat, 0.0, 18.0)
        res = landmass_elongation(cap, W, H, wrap=True, projection="lambert")
        grid_vals.append(res["median"])
        sphere_vals.append(res["sphere_axis_ratio"]["median"])
        print(f"  [info] cap at lat {lat:4.0f}: grid {res['median']}, "
              f"sphere {res['sphere_axis_ratio']['median']}")
    for lat, v in zip((0.0, 45.0, 75.0), sphere_vals):
        ok &= _check(f"sphere cap at lat {lat:.0f} reads round", v, 1.0, 0.20)
    print(f"  (grid space spans {min(grid_vals)}-{max(grid_vals)} for the same "
          f"three round caps -- that spread is the defect)")

    print("\nSELFTEST", "PASSED" if ok else "FAILED")
    return 0 if ok else 1


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
        if not args.flat:
            # The game is always Cylindrical (Application.cpp). Measuring Flat
            # is how the full-width ocean-stripe bug stayed invisible for a
            # whole phase programme -- see commit 9debafd.
            cmd.append("--cylindrical")
        if args.projection:
            cmd += ["--projection", args.projection]
        # The crust-budget numbers exist only on the generator's stderr, behind
        # these env gates. Before 2026-08-12 stderr was captured and then
        # DISCARDED on success, so half the gate set was silently uncomputed --
        # an unattended tuning loop would have iterated all night against
        # whichever four metrics happened to be in the JSON.
        env = dict(os.environ, AOC_DUMP_OROGENY="1", AOC_DUMP_SHELF="1")
        proc = subprocess.run(cmd, capture_output=True, text=True, env=env)
        if proc.returncode != 0:
            print(f"error: seed {seed} generation failed:\n{proc.stderr[-2000:]}",
                  file=sys.stderr)
            return 1
        results[str(seed)] = analyze(f"{stem}.csv",
                                     projection=args.metric_projection,
                                     wrap=not args.flat)
        res = results[str(seed)]
        res["crust_budget"] = parse_crust_budget(proc.stderr)
        print(f"seed {seed}: land={res['land_fraction']:.1%} "
              f"D={res['coast_box_dimension']} "
              f"components={res['n_land_components']} "
              f"largest={res['largest_share_of_land']} "
              f"axis={res['coast_orientation']['axis_aligned_frac']}")
    metrics_path = outdir / "metrics.json"
    metrics_path.write_text(json.dumps(results, indent=2) + "\n")
    print(f"wrote {metrics_path}")
    if args.gate:
        return report_gates(results)
    return 0


# Earth reference bands. Sources: continental crust is 41-43 % of Earth's
# surface with 29 % emergent and 12-13 % submerged margin; largest landmass
# (Afro-Eurasia) is 57 % of land; coastline box dimension 1.15-1.25. The
# axis_aligned_frac null of 0.50 is pinned by `selftest`.
GATES = {
    "land_fraction":            (0.25, 0.33,  "land fraction"),
    "crust_share":              (0.38, 0.44,  "continental crust / sphere"),
    "crust_submerged":          (0.25, 0.35,  "submerged share of that crust"),
    "largest_share_of_land":    (0.00, 0.60,  "largest landmass / land"),
    "big_landmasses":           (4,    99,    "landmasses >2 % of land"),
    "axis_aligned_frac":        (0.00, 0.55,  "coastline axis_aligned_frac"),
    "coast_box_dimension":      (1.15, 1.25,  "coastline box dimension"),
    "shelf_share_of_planet":    (0.05, 0.08,  "shelf / planet"),
}


def parse_crust_budget(stderr):
    """Pull the crust-budget numbers out of the generator's diagnostic dumps.

    Returns None for anything absent rather than a default, so a dump that
    stops being emitted reads as "not measured" instead of silently passing
    its gate.
    """
    out = {"crust_share": None, "crust_submerged": None,
           "shelf_share_of_planet": None}
    m = re.search(r"\[hypso\] continental crust ([\d.]+)% of sphere, "
                  r"of which ([\d.]+)% submerged", stderr)
    if m:
        out["crust_share"] = round(float(m.group(1)) / 100.0, 4)
        out["crust_submerged"] = round(float(m.group(2)) / 100.0, 4)
    # The 140 m depth cut is reported as a share of WATER; convert to a share
    # of the planet so it is comparable to Earth's ~5.3 %.
    m = re.search(r"\[shelf\] by 140 m depth cut it would be \d+ \(([\d.]+)%\)", stderr)
    if m:
        out["_shelf_share_of_water"] = round(float(m.group(1)) / 100.0, 4)
    return out


def gate_values(res):
    """Flatten one seed's result into the scalars GATES names."""
    cb = res.get("crust_budget") or {}
    shelf = cb.get("_shelf_share_of_water")
    land = res.get("land_fraction")
    return {
        "land_fraction": land,
        "crust_share": cb.get("crust_share"),
        "crust_submerged": cb.get("crust_submerged"),
        "largest_share_of_land": res.get("largest_share_of_land"),
        "big_landmasses": res.get("big_landmasses"),
        "axis_aligned_frac": (res.get("coast_orientation") or {}).get("axis_aligned_frac"),
        "coast_box_dimension": res.get("coast_box_dimension"),
        "shelf_share_of_planet": (round(shelf * (1.0 - land), 4)
                                  if shelf is not None and land is not None else None),
    }


def report_gates(results):
    """Print a per-gate pass/fail table over the seed sweep. Returns an exit
    code: non-zero if any gate is missed or unmeasured, so an unattended loop
    stops instead of iterating against a metric that quietly disappeared."""
    print("\ngate                          median   band            seeds pass")
    failed = 0
    for key, (lo, hi, label) in GATES.items():
        vals = [gate_values(r).get(key) for r in results.values()]
        present = [v for v in vals if v is not None]
        if not present:
            print(f"  {label:<28} NOT MEASURED -- gate cannot fail, treating as failure")
            failed += 1
            continue
        present.sort()
        median = present[len(present) // 2]
        npass = sum(1 for v in present if lo <= v <= hi)
        ok = npass == len(present)
        failed += 0 if ok else 1
        mark = "ok " if ok else "MISS"
        print(f"  {mark} {label:<26} {median:<8.3f} [{lo}, {hi}]"
              f"      {npass}/{len(present)}")
    print(f"\n{'GATES PASSED' if failed == 0 else f'{failed} GATE(S) FAILED'}")
    return 0 if failed == 0 else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_analyze = sub.add_parser("analyze", help="analyze one aoc_mapgen CSV")
    p_analyze.add_argument("csv_path")
    p_analyze.add_argument("--projection", default="lambert",
                           help="projection the map was generated with, which "
                                "sets the per-tile sphere-area weights "
                                "(lambert|equirect|mollweide)")
    p_analyze.add_argument("--flat", action="store_true",
                           help="map has Flat topology (no antimeridian wrap)")

    p_baseline = sub.add_parser("baseline",
                                help="generate + analyze a seed sweep")
    p_baseline.add_argument("--binary", required=True)
    p_baseline.add_argument("--outdir", required=True)
    p_baseline.add_argument("--seeds", default="42,7,100,200,1234,777")
    p_baseline.add_argument("--width", type=int, default=140)
    p_baseline.add_argument("--height", type=int, default=90)
    p_baseline.add_argument("--projection", default=None,
                            help="passed through to the generator")
    p_baseline.add_argument("--metric-projection", default="lambert",
                            help="projection used for area weighting "
                                 "(must match --projection)")
    p_baseline.add_argument("--flat", action="store_true",
                            help="generate Flat instead of Cylindrical")
    p_baseline.add_argument("--gate", action="store_true",
                            help="check every metric against its Earth-reference "
                                 "band and EXIT NON-ZERO if any is missed or was "
                                 "not measured at all")

    sub.add_parser("selftest", help="verify the instrument against known masks")

    args = parser.parse_args()
    if args.cmd == "analyze":
        print(json.dumps(analyze(args.csv_path, projection=args.projection,
                                 wrap=not args.flat), indent=2))
        return 0
    if args.cmd == "selftest":
        return cmd_selftest(args)
    return cmd_baseline(args)


if __name__ == "__main__":
    sys.exit(main())
