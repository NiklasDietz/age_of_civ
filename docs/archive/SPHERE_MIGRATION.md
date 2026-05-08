# Sphere-based Plate Tectonic Simulation

Date: 2026-05-05
Status: implemented (hybrid sphere-truth + 2D-derived for legacy consumers)

## Motivation

Pre-2026-05-05 the plate-tectonic simulation operated entirely in unit-square
2D space. Plate positions, motion, Voronoi distances, polygon vertices, and
boundary classification all used `(cx, cy)` in `[0, 1]^2` with optional
cylindrical X-wrap. This produced two artefacts:

1. **Aspect-ratio dependence.** Continent shape and plate size scaled with
   the rasterised map's W/H, not with real planet curvature. A 200x80 grid
   stretched plates horizontally; a 100x100 grid produced abnormally
   compact continents.
2. **No polar coverage degradation.** All tiles were treated as equal-area
   on a flat surface, so polar regions accumulated stress and orogeny at
   the same rate as the equator. Real planets converge meridians at the
   poles -- polar tiles cover much smaller area and host less mountain-
   forming activity.

## What changed

The plate centre's authoritative position is now `(latDeg, lonDeg)` on a
unit sphere. Motion is **Euler-pole rotation**: each plate spins around a
fixed axis `(eulerPoleLatDeg, eulerPoleLonDeg)` at `angularVelDeg` per
epoch, using Rodrigues' formula in `rotateAroundEulerPole`. The plate
also rotates around its local up-axis by the projection of the Euler
vector onto vertical -- this propagates into `p.rot` so polygon vertices
and other plate-local features ride with true sphere orientation, not
just translation.

Tile-to-plate distance is now **haversine distance in radians**, computed
in the elevation pass, the orogeny scatter pass, and `runPlateIdStash`.
Plate-local tangent-plane coordinates `(lx, ly)` for `scatterPL` /
`samplePlateOrogeny` use the small-angle approximation
`lx = dLon * cos(meanLat); ly = dLat` (both radians), preserving the
existing local-frame semantics.

The rasterised map is the **Mollweide equal-area projection** of the
sphere onto `[0, 1]^2`. Tiles whose Mollweide-inverse position falls
outside the ellipse are **polar voids** -- they remain ocean and skip
plate assignment, producing natural ice caps at the top and bottom of
the rendered map (~22 % of the unit-square area). Latitude coverage is
the full ellipse, roughly +-90 deg, with the visible inhabitable band
narrower due to Mollweide's pole pinch.

## Hybrid state -- by design, not laziness

After attempting the full switch on 2026-05-05, the conclusion was that
the remaining "2D" data structures are NOT decoupled from the sphere --
they are either derived from sphere state, or they represent
plate-LOCAL body structure that has no natural sphere equivalent.

### Derived from sphere truth (recomputed each epoch)

- `cx, cy` -- one-line `mollweideForward(latDeg, lonDeg)` after every
  motion step. Pure projection of the sphere position.
- `p.rot` -- updated by the Euler-pole local-vertical projection
  `stepRad * cos(haversineRadians(plate, pole))`. Polygon vertices,
  `extraSeeds`, and `hotspotTrail` ride with this so they all rotate
  with true sphere orientation.

### Plate-local body structure (no sphere equivalent)

- `p.aspect` -- 1:1 to 4:1 elongation parameter that distorts plate
  Voronoi cells (Pacific 1:1, Andean ~4:1). Applied via plate-local
  coord scaling. Tried sphere-Voronoi without it; mountain% dropped
  4.37 -> 3.56 because plates became uniformly circular and boundaries
  shifted. Reverted.
- `boundaryVertices` -- polygon ring in plate-local 2D frame. Position
  on the map = `(p.cx + v*cos(p.rot) - v*sin(p.rot), ...)`. Already
  rides Euler rotation via p.rot. Storing as lat/lon would require
  reprojection through Mollweide every PIP test (50+ sites), no
  accuracy gain.
- `extraSeeds`, `hotspotTrail` -- same plate-local 2D convention as
  `boundaryVertices`. Same conclusion.

### Field that could not be migrated cleanly

- `vx, vy` -- legacy heuristic dynamics field. Tried setting to
  `eulerVelocityAt(plate, pole, angularVelDeg) * 8.0` each epoch.
  Heuristic mutations (slab pull, collision bounce, rift impulse)
  that write to vx/vy got immediately overwritten the following
  epoch -- all dynamic perturbation lost. Continent diversity
  collapsed: mean continent count 8.0 -> 5.5, largest/total
  0.575 -> 0.733, single-continent worst case. Reverted -- proper
  fix is a base-plus-perturbation split routed through every
  mutation site (~70 of them); deferred as multi-day surgery.

### Cylindrical X-wrap on (cx, cy) -- KEPT

`cx = 0.99` and `cx = 0.01` represent `lon = 180` and `lon = -180`,
the same antimeridian point on the sphere. The Mollweide projection
reproduces this as a unit-square wrap-around. Plate-distance and
polygon-overlap tests in `(cx, cy)` space MUST wrap, or every
seam-adjacent comparison gets the wrong answer. The cylindrical
flag does this correctly and is not a 2D-vs-sphere artefact.

## Audit results (200x80, 40 epochs, 20 seeds)

|Metric                 |Pre-sphere|Post-sphere (after recal)|
|-----------------------|----------|-------------------------|
|mountain%              |4.29      |4.37                     |
|continents (mean)      |6.95      |8.00                     |
|largest/total          |0.601     |0.575                    |
|cluster aspect (median)|1.29      |1.33                     |
|FLAGS                  |none      |none                     |
|determinism            |preserved |preserved                |

Polar voids removed ~22 % of orogeny scatter sites, which dropped the
mountain coverage from 4.29 -> 3.88. Recalibration in `ClimateBiome.cpp`
raised the per-continent mountain quota from 8 % to 12 % and lowered
the seed/chain/ridge orogeny thresholds slightly. Mean mountain coverage
recovered into the 4-7 % target range.

## Files

- `include/aoc/map/gen/SphereGeometry.hpp`
- `src/map/gen/SphereGeometry.cpp` -- lat/lon vec3, haversine,
  rotateAroundEulerPole, mollweideForward / mollweideInverse,
  tileToLatLon helper.
- `include/aoc/map/gen/Plate.hpp` -- added latDeg/lonDeg/eulerPoleLatDeg/
  eulerPoleLonDeg/angularVelDeg fields.
- `src/map/MapGenerator.cpp` -- pushPlate (sphere init), motion loop
  (Euler rotation + p.rot propagation), elevation Voronoi (haversine),
  orogeny scatter (haversine + tangent-plane local).
- `src/map/gen/PlateIdStash.cpp` -- haversine fallback Voronoi.
- `src/map/gen/ClimateBiome.cpp` -- recalibrated thresholds + per-
  continent mountain quota.

## Test coverage

`SphereGeometry.cpp` has six round-trip unit tests gated under
`AOC_SPHERE_TESTS`. Determinism is verified by running `aoc_mapgen` twice
with the same seed and comparing CSV output -- bit-identical output
across all 20 audit seeds at 200x80, 40 epochs.
