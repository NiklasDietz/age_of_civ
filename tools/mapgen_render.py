#!/usr/bin/env python3
"""Render an aoc_mapgen CSV dump as a PNG so map SHAPE can be judged by eye.

Why this exists. The continent-realism programme is measured with
tools/mapgen_metrics.py, and numbers found four real defects -- but the two
things numbers did NOT catch (a single blob landmass, dead-straight coasts
running most of the map height) were both obvious the moment anyone looked at
the map. Elevation percentiles cannot tell you that a coastline is a straight
line; a picture can.

Panels:
  terrain  -- game terrain colours, the map as a player sees it
  plates   -- plate id as a hue, land shaded darker than ocean, plate
              boundaries drawn black. This is the panel that answers "is the
              coastline following a plate boundary?", which per-plate aggregate
              stats (--dump-plates gives bbox + centroid only) cannot.

Usage:
    python3 tools/mapgen_render.py MAP.csv [-o OUT.png] [--scale N]
    python3 tools/mapgen_render.py MAP.csv --panel plates

The CSV is the `aoc_mapgen --format csv` dump and must carry the PlateId
column. Odd rows are shifted half a tile to honour the hex offset layout, so
diagonal features do not read as staircases that are not there.
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("error: Pillow required -- pip install --user Pillow")

# Transcribed from aoc::map::terrainColor (include/aoc/map/Terrain.hpp) so a
# rendered map and an in-game screenshot are directly comparable. Coast and
# ShallowWater share one colour in the game; they are split by one shade here
# because telling the shelf from the adjacent-to-land ring is exactly what this
# diagnostic is for.
TERRAIN_RGB = {
    "Ocean":         (0x16, 0x32, 0x4A),  # abyssal
    "Coast":         (0x3E, 0x7A, 0x9E),  # shelf
    "Shallow Water": (0x35, 0x6B, 0x8C),  # shelf, one shade deeper
    "Desert":        (0xC7, 0xB1, 0x83),  # sand
    "Plains":        (0x9A, 0xA4, 0x5F),  # olive
    "Grassland":     (0x6D, 0x9A, 0x4E),  # meadow
    "Tundra":        (0x8E, 0x96, 0x89),  # lichen
    "Snow":          (0xDD, 0xE3, 0xE6),  # snow
    "Mountain":      (0x6B, 0x64, 0x59),  # rock
}
UNKNOWN_RGB = (0xFF, 0x00, 0xFF)

# 20 well-separated hues for plate ids; ids repeat past 20, which is fine
# because adjacency is what matters and plate counts run 10-13.
PLATE_RGB = [
    (0xE6, 0x19, 0x4B), (0x3C, 0xB4, 0x4B), (0xFF, 0xE1, 0x19), (0x43, 0x63, 0xD8),
    (0xF5, 0x82, 0x31), (0x91, 0x1E, 0xB4), (0x46, 0xF0, 0xF0), (0xF0, 0x32, 0xE6),
    (0xBF, 0xEF, 0x45), (0xFA, 0xBE, 0xD4), (0x46, 0x99, 0x90), (0xDC, 0xBE, 0xFF),
    (0x9A, 0x63, 0x24), (0xFF, 0xFA, 0xC8), (0x80, 0x00, 0x00), (0xAA, 0xFF, 0xC3),
    (0x80, 0x80, 0x00), (0xFF, 0xD8, 0xB1), (0x00, 0x00, 0x75), (0xA9, 0xA9, 0xA9),
]
WATER_NAMES = {"Ocean", "Coast", "Shallow Water"}


def load(path):
    rows = {}
    width = height = 0
    with open(path, newline="") as fh:
        for r in csv.DictReader(fh):
            c, y = int(r["Col"]), int(r["Row"])
            rows[(c, y)] = r
            width = max(width, c + 1)
            height = max(height, y + 1)
    if not rows:
        sys.exit(f"error: {path} has no rows")
    return rows, width, height


def hex_neighbours(col, row, width, height):
    """Offset ("odd-r"-style) hex neighbours with longitude wrap, matching
    aoc::hex::neighbors so a boundary drawn here is the boundary the game sees."""
    shift = 1 if (row & 1) else -1
    for dc, dr in ((-1, 0), (1, 0), (0, -1), (0, 1), (shift, -1), (shift, 1)):
        nc, nr = (col + dc) % width, row + dr
        if 0 <= nr < height:
            yield nc, nr


def render(rows, width, height, panel, scale):
    # Odd rows are offset half a tile, so work at 2x internally and shift by
    # one half-tile; otherwise the hex stagger reads as jagged vertical edges
    # and every diagonal coast looks like a staircase artifact that is not real.
    sub = max(2, scale)
    img = Image.new("RGB", (width * sub, height * sub), (0, 0, 0))
    px = img.load()

    for (col, row), r in rows.items():
        terrain = r["Terrain"]
        is_water = terrain in WATER_NAMES
        if panel == "terrain":
            rgb = TERRAIN_RGB.get(terrain, UNKNOWN_RGB)
        else:
            pid = int(r.get("PlateId", 255))
            if pid == 255:
                rgb = (0x20, 0x20, 0x20)
            else:
                base = PLATE_RGB[pid % len(PLATE_RGB)]
                # Ocean washed toward blue-dark, land at full saturation: the
                # eye needs to separate "which plate" from "land or sea" at a
                # glance, and hue alone cannot carry both.
                rgb = tuple(v // 3 for v in base) if is_water else base
        x0 = col * sub + (sub // 2 if (row & 1) else 0)
        for dy in range(sub):
            for dx in range(sub):
                px[(x0 + dx) % (width * sub), row * sub + dy] = rgb

    if panel == "plates":
        # Outline boundary tiles by writing pixels directly rather than with
        # ImageDraw rectangles: a tile straddling the antimeridian wraps to
        # x1 < x0, which the rectangle primitive rejects.
        for (col, row), r in rows.items():
            pid = int(r.get("PlateId", 255))
            if all(int(rows[(nc, nr)].get("PlateId", 255)) == pid
                   for nc, nr in hex_neighbours(col, row, width, height)):
                continue
            x0 = col * sub + (sub // 2 if (row & 1) else 0)
            for dy in range(sub):
                for dx in range(sub):
                    if dx in (0, sub - 1) or dy in (0, sub - 1):
                        px[(x0 + dx) % (width * sub), row * sub + dy] = (0, 0, 0)
    return img


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv_path")
    ap.add_argument("-o", "--out", default=None)
    ap.add_argument("--panel", choices=("terrain", "plates", "both"), default="both")
    ap.add_argument("--scale", type=int, default=4, help="pixels per tile (min 2)")
    args = ap.parse_args()

    rows, width, height = load(args.csv_path)
    out = Path(args.out) if args.out else Path(args.csv_path).with_suffix(".png")

    if args.panel == "both":
        a = render(rows, width, height, "terrain", args.scale)
        b = render(rows, width, height, "plates", args.scale)
        img = Image.new("RGB", (a.width, a.height + b.height + 4), (0, 0, 0))
        img.paste(a, (0, 0))
        img.paste(b, (0, a.height + 4))
    else:
        img = render(rows, width, height, args.panel, args.scale)

    img.save(out)
    print(f"wrote {out} ({img.width}x{img.height})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
