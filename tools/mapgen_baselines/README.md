# Worldgen metric baselines

Each file is a `tools/mapgen_metrics.py baseline` snapshot over the standard
six seeds (42, 7, 100, 200, 1234, 777). Compare two of them to see what a
change moved.

## The `phase*-post.json` / `phase0-pre.json` / `platefix-post.json` files are INVALID

**Do not compare anything against them.** They were all produced by an
instrument with two independent measurement defects, both fixed 2026-07-27:

1. **Continental shelf counted as land.** `WATER_TERRAINS` held
   `"ShallowWater"` while `aoc::map::terrainName()` emits `"Shallow Water"`
   (with a space), so no shelf tile ever matched. Land fraction was inflated
   by roughly 14 percentage points, and shelf tiles bridged separate
   landmasses into a single connected component.
2. **No antimeridian wrap.** `hex_neighbours()` clamped columns instead of
   wrapping them, so on a Cylindrical map every landmass crossing the seam was
   reported as two, with correspondingly wrong shape statistics. Measured on a
   synthetic disc: 1 component / elongation 1.33 when centred, 2 components /
   elongation 2.70 when straddling the seam.

They were additionally generated with **Flat** topology while the game always
runs **Cylindrical** (`src/app/Application.cpp`), and their `land_fraction` was
a raw tile fraction over a rectangle of which ~21.4 % is outside the Mollweide
ellipse and force-set to ocean — so it was not a planet-area fraction either.

These files are kept only as a record of what the earlier phase programme
believed it was measuring. The constants chosen against them are suspect;
notably the initial craton stock was raised to 26–32 % of the sphere because
growth "reliably undershot", a conclusion drawn from inflated land numbers.

## Current baselines

`corrected-pre.json` is the first honest measurement of the pre-rebuild
generator: Cylindrical topology, area-weighted land fraction, seam-aware
components and shape statistics, and a robust (weighted-median) elongation
estimator. It is the "before" that later phases compare against.

Regenerate with:

```
python3 tools/mapgen_metrics.py baseline \
    --binary build/release/aoc_mapgen --outdir <dir> \
    --projection mollweide --metric-projection mollweide
```

Once the projection switches to Lambert cylindrical equal-area, drop both
projection flags (equal-area is the default) — and expect a second
discontinuity in the numbers at that point. Never compare across it.
