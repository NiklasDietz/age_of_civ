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
      [--seeds S1,S2,...] [--width 140] [--height 90]
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

SEED-SET SIZE
-------------
The aggregate gate score looks like a precise integer and is not one. Measured
over the 24-seed default, the per-seed score has mean 5.8 of 12 and sd 2.0, so
the sweep total carries a sampling error of

    sd(total, normalised to /72) = 6 * 2.0 / sqrt(n_seeds)

    n= 6  +/-4.9   two runs must differ by 14.0/72 to mean anything
    n=12  +/-3.5                          9.9/72
    n=24  +/-2.5                          7.0/72
    n=48  +/-1.7                          4.9/72

Two variants are compared on independent draws, not paired ones: anything that
perturbs plate ownership changes boundaries, hence arcs, hence the whole world,
so a shared seed produces an unrelated map and the seed variance does not
cancel. That is why the second column carries the sqrt(2) of a difference.

The practical consequence is that this instrument cannot referee small aggregate
differences at any seed count worth running -- resolving 4/72 needs ~72 seeds.
Read the per-gate columns instead: a gate moving 0/24 -> 12/24 is real, and the
total moving 104 -> 108 is not. `report_resolution` prints the current sweep's
resolution under every gate table so the number is never quoted without it.
"""

import argparse
import csv
import os
import hashlib
import json
import math
import re
import subprocess
import statistics
import sys
from collections import deque
from pathlib import Path

# Mirrors aoc::map::isWater() using the exact strings from
# aoc::map::terrainName() (include/aoc/map/Terrain.hpp). `selftest` re-reads
# that header and fails if the two ever drift apart again -- that drift is
# defect 1 above, and it silently corrupted eight committed baselines.
WATER_TERRAINS = {"Ocean", "Coast", "Shallow Water"}
# Water that is NOT on continental crust. The complement is the crust-footprint
# proxy used by crust_mask_stats(): Coast and Shallow Water sit on (or beside)
# continental crust, deep Ocean does not.
DEEP_OCEAN_TERRAINS = {"Ocean"}
ALL_TERRAINS = {
    "Ocean", "Coast", "Shallow Water", "Desert", "Plains",
    "Grassland", "Tundra", "Snow", "Mountain",
}

# odd-r offset neighbours (odd rows shifted right, matching HexCoord.hpp).
NEIGHBOURS_EVEN = ((1, 0), (-1, 0), (0, -1), (-1, -1), (0, 1), (-1, 1))
NEIGHBOURS_ODD = ((1, 0), (-1, 0), (1, -1), (0, -1), (1, 1), (0, 1))

MIN_ELONGATION_COMPONENT = 50


def load_csv(path):
    """Return (width, height, land, mountain, crust) bool grids indexed [row][col]."""
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
    crust = [[False] * width for _ in range(height)]
    for (col, row), terrain in cells.items():
        land[row][col] = terrain not in WATER_TERRAINS
        mountain[row][col] = terrain == "Mountain"
        # Continental-crust footprint proxy: everything that is not deep
        # ocean. See crust_mask_stats() for what this over- and under-counts.
        crust[row][col] = terrain not in DEEP_OCEAN_TERRAINS
    return width, height, land, mountain, crust


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


# ---------------------------------------------------------------------------
# shape metrics -- second-order statistics
#
# Every pre-2026-08-31 gate is a first-order statistic: a fraction, a count,
# or one fractal dimension. None of them can tell a compact continent from a
# 3-tile ribbon of the same area, which is why "the land is a perforated
# lace" survived a year of gate-driven tuning. Measured on seeds 42/7/100/
# 2026: land is 2.0-3.1x the perimeter of an equal-area disc, 34-46 % of all
# land tiles touch water, and mean inland depth is 2.1-2.9 tiles.
#
# Note `coast_perimeter_over_land` was ALREADY computed and written into every
# metrics.json since before this programme started -- it read 0.3984 on seed
# 42 the whole time. It simply had no entry in GATES. The instrument was not
# missing; the acceptance criterion was.
#
# PROJECTION CAVEAT. These are tile-counting metrics. Under Lambert (the gate
# projection) every tile has equal AREA, so a tile count is an area -- but not
# equal SHAPE: cells stretch in longitude and compress in latitude toward the
# poles, so a perimeter measured by cell adjacency is latitude-distorted.
# `cmd_selftest` measures that distortion directly by scoring identical
# spherical caps at 0/45/70 deg; treat the reported spread as the metric's
# own error bar.
# ---------------------------------------------------------------------------

def inland_depth_map(land, width, height, wrap=True):
    """BFS hop distance from the nearest water tile, for every land tile.

    Returns a [row][col] int grid: 0 on water, 1 for a land tile adjacent to
    water, 2 for the next ring inward, and so on."""
    dist = [[-1] * width for _ in range(height)]
    queue = deque()
    for row in range(height):
        for col in range(width):
            if not land[row][col]:
                dist[row][col] = 0
                queue.append((col, row))
    while queue:
        c, r = queue.popleft()
        for nc, nr in hex_neighbours(c, r, width, height, wrap):
            if dist[nr][nc] < 0:
                dist[nr][nc] = dist[r][c] + 1
                queue.append((nc, nr))
    # An all-land world leaves everything at -1; report it as depth 1 rather
    # than as a negative, so downstream means stay finite.
    for row in range(height):
        for col in range(width):
            if dist[row][col] < 0:
                dist[row][col] = 1
    return dist


def inland_depth_stats(land, width, height, wrap=True):
    """Mean inland depth, raw and normalised by the disc-equivalent.

    Raw mean depth is resolution-dependent (double the grid and it doubles),
    so it cannot carry a fixed Earth band. The normalised form divides by the
    mean depth of a disc of the same area, which is R/3 for a disc of radius
    R = sqrt(A/pi) -- so a perfect disc scores 1.0 at any resolution and the
    band is a statement about shape alone."""
    dist = inland_depth_map(land, width, height, wrap)
    depths = [dist[r][c] for r in range(height) for c in range(width)
              if land[r][c]]
    if not depths:
        return {"mean_inland_depth": None, "inland_depth_over_disc": None}
    mean_depth = sum(depths) / len(depths)
    # Disc-equivalent computed per component and area-weighted, so a world of
    # many small islands is not judged against one big disc.
    total = 0.0
    weight = 0
    for comp in component_cells(land, width, height, wrap):
        n = len(comp)
        disc_mean = math.sqrt(n / math.pi) / 3.0
        if disc_mean <= 0:
            continue
        comp_mean = sum(dist[r][c] for c, r in comp) / n
        total += (comp_mean / disc_mean) * n
        weight += n
    return {
        "mean_inland_depth": round(mean_depth, 3),
        "inland_depth_over_disc": round(total / weight, 3) if weight else None,
    }


def isoperimetric_ratio(land, width, height, wrap=True, min_share=0.02):
    """Coastline length over the circumference of an equal-area disc.

    1.0 is a perfect disc; higher is more ragged. Restricted to components
    holding at least `min_share` of all land, because a 1-tile island scores
    6/3.5 = 1.7 by construction and a world of specks would otherwise read as
    moderately compact.

    THE EARTH BAND FOR THIS MUST BE COMPUTED, NOT CITED. Coastline length is
    ruler-dependent (Mandelbrot 1967): at fine resolution the published
    figures give Africa 1.56 but Eurasia ~3.9 and North America ~4.3, and
    rescaling to a ~200 km ruler with D ~ 1.1-1.2 shortens them 1.6-2.5x. No
    primary dataset at that ruler was found. Rasterise Natural Earth 1:110m
    onto the same grid and measure it with THIS function before setting a
    band."""
    comps = component_cells(land, width, height, wrap)
    land_cells = sum(len(c) for c in comps)
    if not land_cells:
        return {"perimeter_over_disc": None, "perimeter_components": 0}
    per_sum = 0.0
    disc_sum = 0.0
    counted = 0
    for comp in comps:
        n = len(comp)
        if n / land_cells < min_share:
            continue
        member = {(c, r) for c, r in comp}
        perim = sum(1 for c, r in comp
                    if any((nc, nr) not in member
                           for nc, nr in hex_neighbours(c, r, width, height,
                                                        wrap)))
        per_sum += perim
        disc_sum += 2.0 * math.sqrt(math.pi * n)
        counted += 1
    return {
        "perimeter_over_disc": (round(per_sum / disc_sum, 3)
                                if disc_sum else None),
        "perimeter_components": counted,
    }


def crust_mask_stats(crust, width, height, wrap=True):
    """Connectivity of the CONTINENTAL CRUST footprint, not of emergent land.

    Why this is separate from the land metrics. Two independent defects
    produce a bad map -- tectonics welding everything into one supercontinent,
    and the landform stage perforating whatever it is given -- and the
    emergent-land mask is downstream of both, so it cannot tell them apart.
    Measured on the crust mask instead, the two separate cleanly: crust
    compact + land fragmented is a landform bug; crust in one blob is a
    tectonics bug. Seed 42 measures 93 % of crust in one component while its
    emergent land reads 64 %, because the perforation splinters the
    supercontinent.

    PROXY WARNING. This is computed from the hex CSV as "every tile that is
    not deep Ocean", i.e. land + Coast + Shallow Water. That is not the
    cf >= 0.5 raster mask:
      - Coast is assigned to ring 1 regardless of crustal composition, so an
        active margin contributes a ring of false positives;
      - Shallow Water comes from a BFS capped at SHALLOW_BFS_MAX = 4 rings
        (Features.cpp), so continental crust further than 4 tiles offshore is
        invisible here.
    The truncation can only SPLIT the mask, never merge it, so a high
    largest-component share measured this way is a lower bound on the real
    one. Replace with the raster-derived figure once the generator emits it.
    """
    comps = components(crust, width, height, wrap)
    total = sum(comps)
    if not total:
        return {"crust_largest_component_share": None,
                "crust_component_count": 0,
                "crust_tile_fraction": 0.0}
    return {
        "crust_largest_component_share": round(comps[0] / total, 4),
        # Components big enough to be a continent, matching big_landmasses.
        "crust_component_count": sum(1 for s in comps if s / total > 0.01),
        "crust_tile_fraction": round(total / (width * height), 4),
    }


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
    width, height, land, mountain, crust = load_csv(csv_path)
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
        # Second-order shape statistics -- see the block above coastline_cells
        # for why the first-order set could not see the defect these measure.
        **inland_depth_stats(land, width, height, wrap),
        **isoperimetric_ratio(land, width, height, wrap),
        **crust_mask_stats(crust, width, height, wrap),
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

    # -----------------------------------------------------------------
    # shape metrics (added 2026-08-31)
    #
    # Bands for perimeter_over_disc and inland_depth_over_disc are anchored on
    # THESE synthetic controls, not on published coastline figures. Coastline
    # length is ruler-dependent (Mandelbrot 1967) and no primary dataset at
    # this grid's ~200 km ruler was found, so an Earth-cited band would be
    # guesswork. A spherical cap and a 4-tile ribbon are exactly computable
    # here, and the current generator sits at the ribbon end -- which is all
    # the acceptance criterion needs to say. Rasterise Natural Earth 1:110m
    # onto this grid and re-anchor when it is available.
    # -----------------------------------------------------------------
    print("\nshape metrics -- synthetic controls:")

    # coast_box_dimension had NO synthetic control until 2026-08-31, alone
    # among the shape metrics, and its [1.15, 1.25] band was taken from
    # published coastline dimensions. Those are measured with rulers nothing
    # like this grid's ~286 km tile, which is the same ruler-dependence error
    # documented above for perimeter_over_disc.
    #
    # What the controls show is that the estimator is not measuring a fractal
    # dimension at all. Every shape below is a SMOOTH, non-fractal curve -- a
    # circle plus a sinusoid -- and true box dimension for all of them is 1.0
    # in the limit. Over the s in {1,2,4,8,16} window on a coastline a few
    # hundred tiles long the estimator instead returns whatever the roughness
    # is at 1-16 tiles, spanning 0.89 to 1.35. A smooth circle reads 0.891,
    # not 1.0, so the estimator is biased low by ~0.11 at this size and an
    # Earth coastline of true dimension 1.15-1.25 would NOT read 1.15-1.25
    # here. Treat this as a roughness index over a 1-16 tile window.
    #
    # CORRECTION 2026-08-31: the smooth-circle bias led to an inference that
    # this band was mis-anchored too. It is NOT. Earth rasterised onto this
    # grid scores 1.169, comfortably inside [1.15, 1.25], so the band is
    # CONFIRMED and a generator reading 1.10 is genuinely too smooth. The bias
    # is real but does not transfer: a circle is one compact blob, while
    # Earth's coastline at this resolution carries enough length and structure
    # for the estimator to behave. Anchor on Earth, not on inference from a
    # synthetic control.
    def _lobed(cx, cy, radius, amp=0.0, lobes=0):
        land = [0] * (W * H)
        for r in range(H):
            for c in range(W):
                ang = math.atan2(r - cy, c - cx)
                edge = radius + (amp * math.sin(lobes * ang) if lobes else 0.0)
                if math.hypot(c - cx, r - cy) <= edge:
                    land[r * W + c] = 1
        return land

    def _coast_pts(land):
        pts = []
        for r in range(H):
            for c in range(W):
                if not land[r * W + c]:
                    continue
                for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    rr = r + dr
                    if rr < 0 or rr >= H or not land[rr * W + (c + dc) % W]:
                        pts.append((c, r))
                        break
        return pts

    for label, args, want in (
            ("smooth circle reads BELOW 1.0 (estimator bias)", (25,), 0.891),
            ("20 lobes: moderate crenulation", (25, 3, 20), 1.064),
            ("40 lobes: heavy crenulation", (25, 4, 40), 1.293)):
        d = box_count_dimension(_coast_pts(_lobed(70, 45, *args)), W, H)
        ok &= _check(f"box dimension: {label}", d, want, 0.02)

    cap = _sphere_cap(W, H, "lambert", 0.0, 0.0, 30.0)
    iso = isoperimetric_ratio(cap, W, H, True)
    dep = inland_depth_stats(cap, W, H, True)
    ok &= _check("round cap: perimeter_over_disc ~ 1", 
                 iso["perimeter_over_disc"], 1.13, 0.10)
    ok &= _check("round cap: inland_depth_over_disc ~ 1",
                 dep["inland_depth_over_disc"], 0.97, 0.10)

    # Scale invariance. coast_perimeter_over_land does NOT have this property
    # (a cap scores 0.273 at r=15 and 0.119 at r=35, purely from size), which
    # is why the gate is on the disc-normalised form and the raw coastal
    # fraction stays a reported diagnostic.
    ratios = []
    for radius in (15.0, 25.0, 35.0):
        m = _sphere_cap(W, H, "lambert", 0.0, 0.0, radius)
        ratios.append(isoperimetric_ratio(m, W, H, True)["perimeter_over_disc"])
    spread = max(ratios) - min(ratios)
    ok &= _check("perimeter_over_disc is scale-invariant (spread over r=15/25/35)",
                 spread, 0.0, 0.05)

    # A 4-tile-wide bar is the shape the generator actually produces, and it
    # is the upper edge of the gate band: anything scoring 2.0 or more is a
    # ribbon, whatever else it is.
    ribbon = _blank(W, H)
    for r in range(20, 70):
        for c in range(30, 34):
            ribbon[r][c] = True
    r_iso = isoperimetric_ratio(ribbon, W, H, True)["perimeter_over_disc"]
    r_dep = inland_depth_stats(ribbon, W, H, True)["inland_depth_over_disc"]
    ok &= _check("4x50 ribbon: perimeter_over_disc", r_iso, 2.07, 0.10)
    ok &= _check("4x50 ribbon: inland_depth_over_disc", r_dep, 0.56, 0.10)
    # A ribbon must be rejected -- but by the gate that CAN reject it.
    #
    # This control used to assert that perimeter_over_disc rejects a ribbon
    # while a cap passes. That only held while the band also excluded Earth.
    # Measured: ribbon 2.07, EARTH 2.33, cap 1.13 -- on this metric the ribbon
    # is closer to Earth than a round continent is, so every band containing
    # Earth contains the ribbon too. The old assertion encoded a false belief
    # about what this metric can separate, and re-anchoring the band on Earth
    # exposed it.
    #
    # inland_depth_over_disc is what actually separates them: ribbon 0.556
    # (below the 0.60 floor), Earth 0.631, cap 0.949. A ribbon is thin, and
    # thinness is a depth property, not a perimeter one. So the pair is pinned
    # here: perimeter does NOT discriminate, depth does, and a shape verdict
    # needs both.
    lo_d, hi_d, _ = GATES["inland_depth_over_disc"]
    ok &= _check("ribbon rejected by inland depth (the gate that separates it)",
                 1.0 if not (lo_d <= r_dep <= hi_d) else 0.0,
                 1.0, 0.0)
    ok &= _check("cap accepted by inland depth",
                 1.0 if lo_d <= dep["inland_depth_over_disc"] <= hi_d else 0.0,
                 1.0, 0.0)
    ok &= _check("perimeter alone does NOT separate ribbon from Earth (2.07 vs 2.33)",
                 1.0 if GATES["perimeter_over_disc"][0] <= r_iso
                        <= GATES["perimeter_over_disc"][1] else 0.0, 1.0, 0.0)

    # Latitude distortion, measured rather than assumed. Lambert gives every
    # tile equal AREA but not equal SHAPE -- cells stretch in longitude and
    # compress in latitude toward the poles -- so a perimeter counted by cell
    # adjacency is latitude-dependent. This is the metric's own error bar and
    # it is NOT small; it is reported so a reader knows a 1.9 and a 2.1 are
    # not reliably different for high-latitude land.
    lat_vals = []
    for lat in (0.0, 30.0, 45.0, 60.0, 70.0):
        m = _sphere_cap(W, H, "lambert", lat, 0.0, 30.0)
        lat_vals.append(isoperimetric_ratio(m, W, H, True)["perimeter_over_disc"])
    lo, hi = min(lat_vals), max(lat_vals)
    print(f"  [info] identical round caps at lat 0/30/45/60/70 score "
          f"{lat_vals} -- projection error bar is {hi - lo:.2f}")
    ok &= _check("latitude distortion stays within its documented error bar",
                 hi - lo, 0.68, 0.15)

    # crust mask: two discs must read as two components, one disc as one.
    # Guards the mask that separates a tectonics defect from a landform one.
    two = _disc(W, H, 35, 45, 12)
    for r in range(H):
        for c in range(W):
            if _disc(W, H, 105, 45, 12)[r][c]:
                two[r][c] = True
    cs = crust_mask_stats(two, W, H, True)
    ok &= _check("crust mask: two equal discs -> largest share 0.5",
                 cs["crust_largest_component_share"], 0.5, 0.02)
    ok &= _check("crust mask: two equal discs -> 2 components",
                 float(cs["crust_component_count"]), 2.0, 0.0)

    print("resource geography parser:")
    sample = ("[hypso] continental crust 41.0% of sphere, of which 30.0% submerged\n"
              "[resgeo] players=4 starts=4 luxuries=15 A=0.450 B=1 C=3 D1=1 D2=0.500 E=0.833\n"
              "[resgeo] players=6 starts=5 luxuries=15 A=0.300 B=0 C=1 D1=0 D2=0.200 E=0.400\n")
    geo = parse_resource_geography(sample)
    ok &= _check("player counts parsed", float(len(geo)), 2.0, 0.0)
    four = geo.get("4", {})
    ok &= _check("A parsed", four.get("luxury_types_absent"), 0.45, 1e-9)
    ok &= _check("C parsed", four.get("min_luxury_types"), 3.0, 0.0)
    ok &= _check("E parsed", four.get("complementary_pairs"), 0.833, 1e-9)
    ok &= _check("six starts fewer than players", geo.get("6", {}).get("starts"), 5.0, 0.0)
    ok &= _check("no line, no measurement", float(len(parse_resource_geography(""))), 0.0, 0.0)
    passes = sum(1 for k, (lo, hi, _) in RESOURCE_GATES.items() if lo <= four[k] <= hi)
    ok &= _check("4-player line passes every resource gate", float(passes),
                 float(len(RESOURCE_GATES)), 0.0)
    misses = sum(1 for k, (lo, hi, _) in RESOURCE_GATES.items()
                 if not lo <= geo["6"][k] <= hi)
    ok &= _check("6-player line misses every resource gate", float(misses),
                 float(len(RESOURCE_GATES)), 0.0)

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
        if args.players:
            cmd += ["--players", args.players]
        if args.placement:
            cmd += ["--placement", args.placement]
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
        if args.players:
            res["resource_geography"] = parse_resource_geography(proc.stderr)
            for pc, geo in sorted(res["resource_geography"].items(), key=lambda kv: int(kv[0])):
                print(f"seed {seed} [{pc} players]: A={geo['luxury_types_absent']:.2f} "
                      f"B={geo['every_luxury_on_map']} C={geo['min_luxury_types']} "
                      f"D1={geo['copper_or_iron_every_start']} "
                      f"D2={geo['horses_share']:.2f} E={geo['complementary_pairs']:.2f}")
        print(f"seed {seed}: land={res['land_fraction']:.1%} "
              f"D={res['coast_box_dimension']} "
              f"components={res['n_land_components']} "
              f"largest={res['largest_share_of_land']} "
              f"axis={res['coast_orientation']['axis_aligned_frac']}")
    metrics_path = outdir / "metrics.json"
    metrics_path.write_text(json.dumps(results, indent=2) + "\n")
    print(f"wrote {metrics_path}")
    if args.gate:
        rc = report_gates(results)
        if args.players:
            rc = rc or (1 if report_resource_gates(results) else 0)
        return rc
    if args.players:
        report_resource_gates(results)
    return 0


# Seed sweep the gate table is computed over. 24, not 6, because the aggregate
# gate score is a noisy instrument: measured over these seeds the per-seed score
# has sd 2.02 of 12, so a 6-seed sweep carries sd 4.9 on the 0-72 scale and two
# independent 6-seed runs differ by 14 gate-seeds before anything real has moved.
# The 29-37 spread once read off four edge-handling variants was inside that
# noise. Variants that perturb plate ownership re-roll the whole world, so a
# shared seed does NOT pair the comparison and the variance does not cancel.
# See SEED-SET SIZE in the module docstring for the resolution this buys.
DEFAULT_SEEDS = ("42,7,100,200,1234,777,999,13,555,2026,314,8675309,"
                 "1000,2000,3000,4000,5000,6000,7000,8000,9000,11,17,23")


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
    # Band retuned 2026-09-01 from (0.05, 0.08). The old band was Earth's
    # 0..-200 m shelf figure (5.2 % of the planet) while the value fed to it is
    # read off the 140 m cut, where Earth is ~4.0 % -- the mis-calibration was
    # recorded in the plan and never applied. The source also changed on the
    # same date, from a per-tile threshold to the unbiased sub-grid estimator
    # (see parse_crust_budget), which removed a ~2x low bias. Both corrections
    # push the same way, so the pre-2026-09-01 shelf numbers are NOT comparable
    # to anything after it.
    "shelf_share_of_planet":    (0.035, 0.055, "shelf / planet"),
    # --- second-order shape gates, added 2026-08-31 ---
    #
    # Why these exist. Every gate above is a first-order statistic -- a
    # fraction, a count, or one fractal dimension -- and not one of them can
    # tell a compact continent from a 3-tile ribbon of the same area. The
    # generator has been producing a perforated lace throughout, measured at
    # 2.7-3.9x the perimeter of an equal-area disc; seeds 7 and 2026 score
    # WORSE than a solid 120x4 bar (3.14). Note also that
    # coast_perimeter_over_land was already computed and written into every
    # metrics.json all along, reading 0.3984 on seed 42 -- the instrument was
    # never missing, only the acceptance criterion.
    #
    # Bands are anchored on the synthetic controls in `selftest`, NOT on
    # published coastline totals: coastline length is ruler-dependent
    # (Mandelbrot 1967) and no primary dataset at this grid's ~200 km ruler
    # was found. A round spherical cap scores 1.13 and a 4-tile ribbon 2.07,
    # both exactly reproducible here. Africa's fine-resolution 1.56 falls
    # inside the band, which is a consistency check rather than its source.
    # Re-anchor on Natural Earth 1:110m rasterised onto this grid when
    # available.
    # RE-ANCHORED 2026-08-31 on Earth itself. The band was [1.20, 2.00] and
    # EARTH SCORES 2.326 -- outside its own gate. tools/earth_reference.py
    # rasterises Natural Earth 1:110m onto this grid and measures it with the
    # code below (land fraction comes out 0.290 against a true 0.292, so the
    # rasterisation is sound). The old ceiling was cited from published
    # coastline figures -- exactly the ruler-dependence error warned about
    # above; the note asking for this measurement has been in this file since
    # the metric was added.
    #
    # Consequence worth stating plainly: a generator scoring 1.8 is SMOOTHER
    # than Earth, not better than Earth, and the old band rewarded moving away
    # from the reference.
    #
    # Earth-centred with room either side. Read it WITH inland_depth_over_disc:
    # a 4-tile ribbon scores 2.07 here against Earth's 2.33, so this metric
    # alone cannot tell a ribbon from a continent.
    "perimeter_over_disc":      (1.90, 2.80,  "coastline vs equal-area disc"),
    "inland_depth_over_disc":   (0.60, 1.15,  "inland depth vs disc"),
    # Measured on the CONTINENTAL CRUST footprint, not on emergent land,
    # because the emergent mask is downstream of BOTH the supercontinent
    # defect and the perforation defect and cannot separate them. Crust
    # compact + land fragmented is a landform bug; crust in one blob is a
    # tectonics bug. Earth: Afro-Eurasia is ~45 % of continental crust.
    "crust_largest_component_share": (0.35, 0.65, "largest crust component"),
    "crust_component_count":    (3,    8,     "crust components >1 %"),
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
    # Kept as a reported diagnostic only. This is the OLD gate source: a count
    # of tiles whose MEAN footprint depth clears the 140 m cut, as a share of
    # WATER. It is a biased estimator of shelf AREA -- on a concave margin a
    # tile half terrace and half slope averages below the cut and contributes
    # nothing instead of one half -- and it measured ~2x low against the
    # sub-grid figure below (seed 42: 0.056 vs 0.112 of planet). Retained so
    # the two can be compared on one run and so the pre-2026-09-01 baselines
    # stay interpretable.
    m = re.search(r"\[shelf\] by 140 m depth cut it would be \d+ \(([\d.]+)%\)", stderr)
    if m:
        out["_shelf_share_of_water"] = round(float(m.group(1)) / 100.0, 4)
    # THE GATE SOURCE since 2026-09-01. Each of the 16 sub-samples behind a
    # tile's elevation is thresholded before they are averaged, so this
    # estimates shelf area without the averaging bias. Already a share of the
    # PLANET -- it must NOT be multiplied by (1 - land_fraction).
    m = re.search(r"\[shelf\] subgrid area share of planet ([\d.]+)", stderr)
    if m:
        out["_shelf_subgrid_share_of_planet"] = round(float(m.group(1)), 4)
    return out


RESGEO_LINE = re.compile(
    r"\[resgeo\] players=(\d+) starts=(\d+) luxuries=(\d+) A=([\d.]+) B=(\d) "
    r"C=(\d+) D1=(\d) D2=([\d.]+) E=([\d.]+)")


def parse_resource_geography(stderr):
    """The [resgeo] lines aoc_mapgen --players prints, keyed by player count.

    Missing lines yield an empty dict, deliberately: a generator that does
    not print the line reads as "not measured" and fails RESOURCE_GATES.
    """
    out = {}
    for m in RESGEO_LINE.finditer(stderr):
        out[m.group(1)] = {
            "starts": int(m.group(2)),
            "luxury_types": int(m.group(3)),
            "luxury_types_absent": float(m.group(4)),
            "every_luxury_on_map": int(m.group(5)),
            "min_luxury_types": int(m.group(6)),
            "copper_or_iron_every_start": int(m.group(7)),
            "horses_share": float(m.group(8)),
            "complementary_pairs": float(m.group(9)),
        }
    return out


# Resource geography gates (money and trade plan, Part B3 and M8), measured
# within 9 tiles of each start for every player count passed to --players.
# Kept apart from GATES: an unmeasured resource gate fails here and must never
# dilute the shape score, and vice versa.
RESOURCE_GATES = {
    "luxury_types_absent":        (0.40, 1.00, "A luxury types absent per start"),
    "every_luxury_on_map":        (1,    1,    "B every luxury on the map"),
    "min_luxury_types":           (3,    99,   "C fewest luxury types at a start"),
    "copper_or_iron_every_start": (1,    1,    "D1 copper or iron at every start"),
    "horses_share":               (0.50, 1.00, "D2 starts with horses"),
    "complementary_pairs":        (0.80, 1.00, "E complementary start pairs"),
}


def report_resource_gates(results):
    """Per player count, the RESOURCE_GATES table over the seed sweep. Returns
    the number of gates missed or unmeasured."""
    counts = sorted({pc for r in results.values()
                     for pc in (r.get("resource_geography") or {})}, key=int)
    failed = 0
    if not counts:
        print("\nresource geography: NOT MEASURED (run baseline with --players)")
        return len(RESOURCE_GATES)
    for pc in counts:
        print(f"\nresource gate ({pc} players)     median   band            seeds pass")
        for key, (lo, hi, label) in RESOURCE_GATES.items():
            vals = [(r.get("resource_geography") or {}).get(pc, {}).get(key)
                    for r in results.values()]
            present = sorted(v for v in vals if v is not None)
            if not present:
                print(f"  {label:<32} NOT MEASURED -- treating as failure")
                failed += 1
                continue
            median = present[len(present) // 2]
            npass = sum(1 for v in present if lo <= v <= hi)
            ok = npass == len(present)
            failed += 0 if ok else 1
            print(f"  {'ok ' if ok else 'MISS'} {label:<30} {median:<8.3f} [{lo}, {hi}]"
                  f"      {npass}/{len(present)}")
    print(f"\n{'RESOURCE GATES PASSED' if failed == 0 else f'{failed} RESOURCE GATE(S) FAILED'}")
    return failed


def gate_values(res):
    """Flatten one seed's result into the scalars GATES names."""
    cb = res.get("crust_budget") or {}
    land = res.get("land_fraction")
    return {
        "land_fraction": land,
        "perimeter_over_disc": res.get("perimeter_over_disc"),
        "inland_depth_over_disc": res.get("inland_depth_over_disc"),
        "crust_largest_component_share": res.get("crust_largest_component_share"),
        "crust_component_count": res.get("crust_component_count"),
        "crust_share": cb.get("crust_share"),
        "crust_submerged": cb.get("crust_submerged"),
        "largest_share_of_land": res.get("largest_share_of_land"),
        "big_landmasses": res.get("big_landmasses"),
        "axis_aligned_frac": (res.get("coast_orientation") or {}).get("axis_aligned_frac"),
        "coast_box_dimension": res.get("coast_box_dimension"),
        # Already a share of planet -- no (1 - land) conversion, unlike the
        # per-tile estimator it replaced. Falls back to nothing rather than to
        # the old biased figure: a run whose generator predates the sub-grid
        # line must read as UNMEASURED, not silently score on the old ruler.
        "shelf_share_of_planet": cb.get("_shelf_subgrid_share_of_planet"),
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
    report_resolution(results)
    return 0 if failed == 0 else 1


def report_resolution(results):
    """Print the aggregate gate score WITH the sampling error it carries.

    The aggregate score is what tuning gets compared on, and it is far noisier
    than its integer look suggests. Printing the score alone invites reading a
    few gate-seeds of sampling noise as a real improvement, which has already
    happened once on a 4-variant comparison at 6 seeds. So the score never
    prints without the smallest difference the sweep can actually resolve.
    """
    per = []
    for res in results.values():
        gv = gate_values(res)
        per.append(sum(1 for k, (lo, hi, _) in GATES.items()
                       if gv.get(k) is not None and lo <= gv[k] <= hi))
    n = len(per)
    if n < 2:
        return
    total = sum(per)
    scale = 72.0 / len(GATES)
    sd_seed = statistics.stdev(per)
    sem = scale * sd_seed / math.sqrt(n)
    # Two sweeps of this size, compared: the difference carries sqrt(2) x the
    # error of one, and 2 sigma of that is the smallest honest verdict.
    resolves = 2.0 * sem * math.sqrt(2.0)
    print(f"score {total}/{n * len(GATES)} "
          f"({72.0 * total / (n * len(GATES)):.1f}/72 normalised, +/-{sem:.1f})")
    print(f"resolves differences >= {resolves:.1f}/72; anything smaller is noise")


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
    p_baseline.add_argument("--seeds", default=DEFAULT_SEEDS,
                            help="comma-separated seed sweep. The default is 24 "
                                 "seeds because 6 cannot resolve the differences "
                                 "this programme tunes against -- see SEED-SET SIZE.")
    p_baseline.add_argument("--width", type=int, default=140)
    p_baseline.add_argument("--height", type=int, default=90)
    p_baseline.add_argument("--projection", default=None,
                            help="passed through to the generator")
    p_baseline.add_argument("--metric-projection", default="lambert",
                            help="projection used for area weighting "
                                 "(must match --projection)")
    p_baseline.add_argument("--flat", action="store_true",
                            help="generate Flat instead of Cylindrical")
    p_baseline.add_argument("--players", default=None,
                            help="comma-separated player counts, e.g. 4,6: choose "
                                 "starts per count and record the resource "
                                 "geography around them (RESOURCE_GATES)")
    p_baseline.add_argument("--placement", default=None,
                            choices=["realistic", "fair", "random"],
                            help="resource placement mode; fair and random run the "
                                 "regional exclusivity pass once starts are chosen")
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
