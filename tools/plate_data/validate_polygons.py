#!/usr/bin/env python3
"""
Polygon-tectonic plate validator -- runs aoc_mapgen with --frames at a fixed
size (200x80, 40 epochs) and inspects the final-frame plate-glyph dump to
infer plate-id health.

Plate IDs are not (yet) emitted in the per-tile CSV. Frame mode writes a
character grid where each glyph is `A + (plate_id % 26)` for land,
lowercase for water, `^` for mountain, and `?` for orphan tiles
(plate_id == 0xFF). We use that to verify:

- unique plate-letter count (proxy for plate count; <= 13 expected)
- no orphan `?` glyphs neighboured by valid plate glyphs
- plate count cap is respected (max 13 per MapGenerator.cpp:1576)

Caveat: a plate count > 26 would alias under `% 26`. The simulator caps at
13, so aliasing cannot currently happen and the proxy is exact.

Usage:
    python3 tools/plate_data/validate_polygons.py [N_SEEDS]
"""

import os
import statistics
import string
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
BINARY = REPO_ROOT / "build" / "aoc_mapgen"

WIDTH = 200
HEIGHT = 80
EPOCHS = 40
DEFAULT_N = 20
PLATE_CAP = 13  # MapGenerator.cpp ~line 1576

VALID_LAND_GLYPHS = set(string.ascii_uppercase)
VALID_WATER_GLYPHS = set(string.ascii_lowercase)
ORPHAN = "?"
MOUNTAIN = "^"


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


def parse_frame(path: Path):
    """Returns dict[(col,row)] = glyph for the final frame file."""
    grid = {}
    with open(path) as f:
        row_idx = 0
        for line in f:
            if line.startswith("#"):
                continue
            line = line.rstrip("\n")
            # Odd rows are prefixed with a single space for visual alignment.
            if line.startswith(" "):
                line = line[1:]
            if not line:
                continue
            for col, ch in enumerate(line):
                grid[(col, row_idx)] = ch
            row_idx += 1
    return grid


def run_seed(seed: int):
    out_base = f"/tmp/validate_polygons_{seed}"
    cmd = [
        str(BINARY),
        "--width", str(WIDTH),
        "--height", str(HEIGHT),
        "--seed", str(seed),
        "--epochs", str(EPOCHS),
        "--output", out_base,
        "--format", "csv",
        "--frames",
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

    final_frame = Path(f"{out_base}.frame{EPOCHS:03d}.txt")
    if not final_frame.exists():
        sys.stderr.write(f"seed {seed}: missing {final_frame}\n")
        return None

    grid = parse_frame(final_frame)
    if not grid:
        return None

    # Count distinct plate letters case-insensitively.
    letters = set()
    orphans = 0
    orphan_with_neighbours = 0
    glyph_counts = {}
    for (c, r), ch in grid.items():
        glyph_counts[ch] = glyph_counts.get(ch, 0) + 1
        if ch in VALID_LAND_GLYPHS:
            letters.add(ch)
        elif ch in VALID_WATER_GLYPHS:
            letters.add(ch.upper())
        elif ch == ORPHAN:
            orphans += 1
            # Treat as orphan-with-valid-neighbour if at least one neighbour
            # carries a valid plate glyph (letter, not ^ or ?).
            for nb in hex_neighbors(c, r):
                ng = grid.get(nb)
                if ng is None:
                    continue
                if ng in VALID_LAND_GLYPHS or ng in VALID_WATER_GLYPHS:
                    orphan_with_neighbours += 1
                    break
    plate_count = len(letters)
    return {
        "seed": seed,
        "elapsed_ms": elapsed_ms,
        "plate_count": plate_count,
        "exceeds_cap": plate_count > PLATE_CAP,
        "letters": sorted(letters),
        "orphans": orphans,
        "orphan_with_neighbours": orphan_with_neighbours,
        "tiles": sum(glyph_counts.values()),
        "mountain_glyphs": glyph_counts.get(MOUNTAIN, 0),
    }


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_N
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

    plate_counts = [r["plate_count"] for r in rows]
    orphans = [r["orphans"] for r in rows]
    orphan_islands = [r["orphan_with_neighbours"] for r in rows]
    times = [r["elapsed_ms"] for r in rows]

    print(f"=== validate_polygons.py ({WIDTH}x{HEIGHT}, {EPOCHS} epochs, "
          f"{len(rows)} seeds) ===")
    print(f"plate_count: mean={statistics.mean(plate_counts):.2f} "
          f"median={int(statistics.median(plate_counts))} "
          f"min-max={min(plate_counts)}-{max(plate_counts)} "
          f"cap={PLATE_CAP}")
    over = [r for r in rows if r["exceeds_cap"]]
    print(f"cap-violations: {len(over)}/{len(rows)}")
    print(f"orphan_tiles: mean={statistics.mean(orphans):.2f} "
          f"median={int(statistics.median(orphans))} "
          f"max={max(orphans)} "
          f"total={sum(orphans)}")
    print(f"orphans-bordering-plate (true 'orphan' bug): "
          f"total={sum(orphan_islands)} max-per-seed={max(orphan_islands)}")
    print(f"gen_time_ms: mean={int(statistics.mean(times))} "
          f"median={int(statistics.median(times))} "
          f"min-max={int(min(times))}-{int(max(times))}")

    flags = []
    if over:
        flags.append(f"{len(over)} seeds exceeded plate cap of {PLATE_CAP}")
    if sum(orphan_islands) > 0:
        flags.append(f"{sum(orphan_islands)} orphan tiles found inside plates")
    if statistics.mean(plate_counts) < 4:
        flags.append(f"plate_count mean {statistics.mean(plate_counts):.2f} "
                     f"unusually low")
    if flags:
        print("FLAGS: " + "; ".join(flags))
    else:
        print("FLAGS: none (plate IDs healthy)")


if __name__ == "__main__":
    main()
