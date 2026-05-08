#!/usr/bin/env python3
"""
Plate-evolution frame renderer -- runs aoc_mapgen with --frames at a fixed
size (200x80, 40 epochs) for a fixed seed set, then turns each per-epoch
ASCII glyph dump into a colour PNG strip and an animated GIF for visual
diagnosis of plate / mountain / coastline emergence over the tectonic sim.

Frame ASCII format (see src/tools/MapGenCli.cpp::writeFrame):
- Two `#` header lines; data rows follow.
- Odd rows are prefixed with one space character (hex offset).
- Glyph alphabet:
    * Uppercase A-Z = land tile, plate id mod 26
    * Lowercase a-z = ocean tile, plate id mod 26
    * '^'           = mountain
    * '?'           = orphan / unassigned plate (plateId == 0xFF)

Colour mapping:
- ocean (lowercase letter): dark blue (#2060c0)
- coast (any '?' / unknown glyph that is non-water non-land): light blue
  (#80a0e0). aoc_mapgen does not currently emit a dedicated coast glyph;
  we still reserve the colour for orphans / future glyphs.
- land plate: deterministic per-letter palette colour
- mountain '^': brown (#604020)
- fallback land: green (#608040)

Outputs, written under tools/plate_data/diagnostic_frames/:
- seed_NN_strip.png  -- 40 frames stitched horizontally with epoch labels
- seed_NN_anim.gif   -- looping animation, 150 ms per frame

If Pillow cannot be imported and `pip install --user Pillow` also fails,
falls back to a text-only summary line per seed and exits 0.

Usage:
    OMP_NUM_THREADS=1 python3 tools/plate_data/render_frames.py
"""

from __future__ import annotations

import os
import string
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
BINARY = REPO_ROOT / "build" / "aoc_mapgen"
OUT_DIR = REPO_ROOT / "tools" / "plate_data" / "diagnostic_frames"

WIDTH = 200
HEIGHT = 80
EPOCHS = 40
SEEDS = [1, 5, 10, 15, 20]
PIXEL_SCALE = 4               # 4x4 px per tile -> 800x320 per frame
LABEL_HEIGHT = 16             # vertical band above each frame for the epoch
GIF_FRAME_MS = 150

OCEAN_RGB    = (0x20, 0x60, 0xC0)
COAST_RGB    = (0x80, 0xA0, 0xE0)
MOUNTAIN_RGB = (0x60, 0x40, 0x20)
LAND_RGB     = (0x60, 0x80, 0x40)

LAND_GLYPHS  = set(string.ascii_uppercase)
WATER_GLYPHS = set(string.ascii_lowercase)


# --------------------------------------------------------------------------- #
# Pillow bootstrap                                                            #
# --------------------------------------------------------------------------- #

def try_import_pillow():
    """Import Pillow, attempting `pip install --user` once if missing.

    Returns the (Image, ImageDraw, ImageFont) tuple or None if unavailable.
    """
    try:
        from PIL import Image, ImageDraw, ImageFont  # type: ignore
        return Image, ImageDraw, ImageFont
    except ImportError:
        pass
    sys.stderr.write("Pillow not found; attempting `pip install --user Pillow` ...\n")
    rc = subprocess.call(
        [sys.executable, "-m", "pip", "install", "--user", "Pillow"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT,
    )
    if rc != 0:
        return None
    try:
        from PIL import Image, ImageDraw, ImageFont  # type: ignore
        return Image, ImageDraw, ImageFont
    except ImportError:
        return None


# --------------------------------------------------------------------------- #
# aoc_mapgen invocation                                                       #
# --------------------------------------------------------------------------- #

def run_mapgen(seed: int) -> str | None:
    """Runs aoc_mapgen --frames for `seed`. Returns the output base path or
    None on failure / missing binary."""
    if not BINARY.exists():
        sys.stderr.write(f"missing binary: {BINARY}\n")
        return None
    out_base = f"/tmp/aoc_frames_s{seed}"
    cmd = [
        str(BINARY),
        "--width", str(WIDTH),
        "--height", str(HEIGHT),
        "--seed", str(seed),
        "--epochs", str(EPOCHS),
        "--output", out_base,
        "--format", "ascii",
        "--frames",
    ]
    proc = subprocess.run(
        cmd,
        env={**os.environ, "OMP_NUM_THREADS": "1"},
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    if proc.returncode != 0:
        sys.stderr.write(f"seed {seed} aoc_mapgen failed rc={proc.returncode}:\n"
                         f"{proc.stderr}\n")
        return None
    return out_base


# --------------------------------------------------------------------------- #
# Frame parsing                                                               #
# --------------------------------------------------------------------------- #

def parse_frame(path: Path) -> list[list[str]] | None:
    """Reads an ASCII frame into a 2D row-major list of glyph chars.

    Returns None if the file is empty or malformed. Pads short / over-long
    rows to WIDTH so downstream rendering never indexes out of bounds.
    """
    if not path.exists():
        return None
    rows: list[list[str]] = []
    with open(path) as f:
        for line in f:
            if line.startswith("#"):
                continue
            line = line.rstrip("\n")
            if line.startswith(" "):
                line = line[1:]   # strip odd-row hex offset
            if not line:
                continue
            row = list(line)
            if len(row) < WIDTH:
                row.extend(" " * (WIDTH - len(row)))
            elif len(row) > WIDTH:
                row = row[:WIDTH]
            rows.append(row)
    if not rows:
        return None
    while len(rows) < HEIGHT:
        rows.append([" "] * WIDTH)
    return rows[:HEIGHT]


# --------------------------------------------------------------------------- #
# Colour helpers                                                              #
# --------------------------------------------------------------------------- #

def plate_colour(letter: str) -> tuple[int, int, int]:
    """Deterministic per-plate-letter colour. Avoids near-water and
    near-mountain hues by clamping channel ranges into a midtone band."""
    h = (ord(letter) * 2654435761) & 0xFFFFFFFF
    r = 80 + (h        & 0x7F)         # 80..207
    g = 80 + ((h >> 8) & 0x7F)
    b = 60 + ((h >> 16) & 0x5F)        # 60..155 (keep blue dimmer than ocean)
    return (r, g, b)


def glyph_to_rgb(ch: str) -> tuple[int, int, int]:
    if ch == "^":
        return MOUNTAIN_RGB
    if ch in WATER_GLYPHS:
        return OCEAN_RGB
    if ch in LAND_GLYPHS:
        return plate_colour(ch)
    if ch == "?":
        return COAST_RGB
    return LAND_RGB


def count_mountains(grid: list[list[str]]) -> int:
    return sum(row.count("^") for row in grid)


# --------------------------------------------------------------------------- #
# Image rendering                                                             #
# --------------------------------------------------------------------------- #

def render_frame(grid: list[list[str]], Image):
    """Renders one frame as a `WIDTH*PIXEL_SCALE` x `HEIGHT*PIXEL_SCALE`
    RGB image."""
    img = Image.new("RGB", (WIDTH, HEIGHT))
    px = img.load()
    for r, row in enumerate(grid):
        for c, ch in enumerate(row):
            px[c, r] = glyph_to_rgb(ch)
    if PIXEL_SCALE != 1:
        img = img.resize(
            (WIDTH * PIXEL_SCALE, HEIGHT * PIXEL_SCALE),
            resample=Image.NEAREST,
        )
    return img


def label_frame(frame_img, epoch: int, Image, ImageDraw, ImageFont):
    """Returns a new image with a small label band above `frame_img`."""
    fw, fh = frame_img.size
    out = Image.new("RGB", (fw, fh + LABEL_HEIGHT), (16, 16, 16))
    out.paste(frame_img, (0, LABEL_HEIGHT))
    draw = ImageDraw.Draw(out)
    try:
        font = ImageFont.load_default()
    except Exception:
        font = None
    draw.text((4, 1), f"epoch {epoch:02d}/{EPOCHS}",
              fill=(220, 220, 220), font=font)
    return out


def build_strip(labelled_frames, Image):
    """Stitches all labelled frames into one horizontal PNG."""
    if not labelled_frames:
        return None
    fw, fh = labelled_frames[0].size
    strip = Image.new("RGB", (fw * len(labelled_frames), fh), (0, 0, 0))
    for i, im in enumerate(labelled_frames):
        strip.paste(im, (i * fw, 0))
    return strip


# --------------------------------------------------------------------------- #
# Per-seed driver                                                             #
# --------------------------------------------------------------------------- #

def process_seed(seed: int, Image, ImageDraw, ImageFont) -> str | None:
    """Generates frames for `seed`, renders strip + GIF.

    Returns a one-line summary on success or None on failure (missing
    output files, unreadable frames, etc.).
    """
    out_base = run_mapgen(seed)
    if out_base is None:
        return None

    grids: list[list[list[str]]] = []
    missing = 0
    for k in range(1, EPOCHS + 1):
        path = Path(f"{out_base}.frame{k:03d}.txt")
        grid = parse_frame(path)
        if grid is None:
            missing += 1
            continue
        grids.append(grid)

    if not grids:
        sys.stderr.write(f"seed {seed}: no readable frames\n")
        return None
    if missing:
        sys.stderr.write(f"seed {seed}: {missing} frame(s) missing or empty; "
                         f"continuing with {len(grids)}\n")

    if Image is None:
        # Text-only fallback (Pillow unavailable).
        return (f"seed {seed}: {len(grids)} frames, "
                f"{count_mountains(grids[-1])} mountain tiles in final frame "
                f"(no images: Pillow unavailable)")

    rendered = [render_frame(g, Image) for g in grids]
    labelled = [label_frame(im, k + 1, Image, ImageDraw, ImageFont)
                for k, im in enumerate(rendered)]

    strip = build_strip(labelled, Image)
    strip_path = OUT_DIR / f"seed_{seed:02d}_strip.png"
    if strip is not None:
        strip.save(strip_path, format="PNG", optimize=True)

    gif_path = OUT_DIR / f"seed_{seed:02d}_anim.gif"
    # Quantize to keep the GIF compact; without this Pillow ships an
    # 8 MB-ish animation per seed.
    quantised = [im.convert("P", palette=Image.ADAPTIVE, colors=128)
                 for im in labelled]
    quantised[0].save(
        gif_path,
        save_all=True,
        append_images=quantised[1:],
        duration=GIF_FRAME_MS,
        loop=0,
        optimize=True,
        disposal=2,
    )

    return (f"seed {seed}: {len(grids)} frames, "
            f"{count_mountains(grids[-1])} mountain tiles in final frame")


# --------------------------------------------------------------------------- #
# main                                                                        #
# --------------------------------------------------------------------------- #

def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    pillow = try_import_pillow()
    if pillow is None:
        Image = ImageDraw = ImageFont = None  # type: ignore
        sys.stderr.write("Pillow unavailable -- text-only summaries.\n")
    else:
        Image, ImageDraw, ImageFont = pillow

    summaries: list[str] = []
    for seed in SEEDS:
        line = process_seed(seed, Image, ImageDraw, ImageFont)
        if line is None:
            sys.stderr.write(f"seed {seed}: skipped\n")
            continue
        summaries.append(line)
        print(line, flush=True)

    if not summaries:
        sys.stderr.write("no seeds produced output\n")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
