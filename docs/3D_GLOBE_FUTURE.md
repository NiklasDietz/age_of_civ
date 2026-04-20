# 3D Globe View — Future Implementation

Scoped 2026-04-20. Deferred — notes preserved for later.

## Goal

Render the hex map as a 3D globe (spinnable, orbit camera) instead of a flat plane. Civ 7-style presentation.

## Key Finding — Topology Constraints

Closed spherical surfaces must satisfy Euler characteristic `V − E + F = 2`. This rules out naive hex-only tilings.

### Shape tiling matrix

| Shape | Flat plane | Sphere (closed) |
|---|---|---|
| Triangle | ✅ | ✅ (icosahedron, 20 faces) |
| Square | ✅ | ❌ pure. Cube sphere = 6 panels works |
| Pentagon | ❌ regular | ✅ only 12 (dodecahedron) |
| Hexagon | ✅ | ❌ needs 12 pentagons mixed in (Goldberg) |
| Octagon | ❌ | ❌ |

### Why pure hex fails on sphere

Each hex has 6 edges, each edge shared by 2 faces → `E = 3F`.
Each vertex belongs to 3 hexes → `V = 2F`.
Then `V − E + F = 2F − 3F + F = 0 ≠ 2`. Closed sphere impossible with hex only.

### Closure options

- **12 pentagons + hexes** — Goldberg polyhedron (soccer ball / buckminsterfullerene). True uniform sphere tiling. Used by Catan globe edition.
- **Cylinder topology with pole caps** — hex grid wrapped east-west only, clamped north-south. Visual cap (ice / ocean / terrain disc) at each pole. Civ 7 uses this.

## North-South Crossing

Separate issue from cap rendering. With cylinder topology, **hex tiles cannot wrap over pole** regardless of cap size. Top row has no north neighbor.

Three workarounds:

- **A. Polar edges** — top-row tile `(q, 0)` neighbors `((q + W/2) mod W, 0)`. Fake adjacency in `HexGrid::neighbors()`. Units visually teleport or fade across pole. Minimal pathfinding change.
- **B. Goldberg polyhedron** — true sphere, uniform travel. Requires rewrite of map gen + neighbor topology + coordinate system. Some tiles pentagons with 5 neighbors. Breaks game balance (combat/unit types tuned for 6-neighbor hex).
- **C. Cylinder + no-cross** — standard Civ behavior. No pole crossing. Zero game-logic changes.

Recommended: **C** for initial implementation. **A** as optional follow-up (small pathfinding change, purely visual globe work stays untouched).

## Recommended Approach — Path B (Full 3D Hex Tiles on Sphere)

Skipped Path A (sprite-trick perspective). Full 3D, reuse existing `Renderer3D` + `forward3d.vert/frag` shaders.

### Architecture

- Keep existing `HexGrid` axial coordinate system untouched
- `GlobeRenderer` (new): per-frame, project each tile `(q, r)` onto sphere surface, submit flat hex disc mesh with tangent-plane transform + terrain material to `Renderer3D`
- `UIManager` / 2D overlay pass unchanged — renders on top
- Hotkey `G` toggles flat/globe mode in `Application`

### Projection math

```
longitude = (q / W) * 2π                         // east-west wrap
latitude  = clamp((r / H − 0.5) * π, [-70°, +70°])  // leave polar gap for ice
sphere_pos = R * (cos(lat)cos(lon), sin(lat), cos(lat)sin(lon))
normal     = normalize(sphere_pos)
tangent    = normalize(cross(world_up, normal))
bitangent  = cross(normal, tangent)
transform  = mat4(tangent, bitangent, normal, sphere_pos)
```

Hex disc mesh = 7 vertices (center + 6 rim), 6 triangles, normal = +Y.
Scale per-tile to match hex circumradius so adjacent hexes nearly touch near equator. Pole distortion accepted (narrow slivers).

### Ice cap

Two polar cones (or flattened spheres). Radius covers latitude gap `> 70°`. Marked impassable in map gen (`TerrainType::Ice`).

### Files

**Submodule edit** (`third_party/vulkan_renderer`):
- Raise `Renderer3D::MAX_INSTANCES` from 4096 to 8192 (80×52 = 4160 exceeds current limit)

**New files**:
- `include/aoc/render/GlobeRenderer.hpp`
- `src/render/GlobeRenderer.cpp`
- `include/aoc/render/OrbitCamera.hpp`
- `src/render/OrbitCamera.cpp`

**Modified**:
- `src/app/Application.cpp` — init `Renderer3D`, `GlobeRenderer`, `OrbitCamera`; G-key toggle; branch render loop on flat/globe mode
- `include/aoc/render/GameRenderer.hpp` — accept globe mode flag, route to Globe vs MapRenderer
- `CMakeLists.txt` — add GlobeRenderer/OrbitCamera

### Orbit camera

- State: `yaw`, `pitch`, `distance`, `target = (0,0,0)`
- `position = target + distance * (cos(pitch)cos(yaw), sin(pitch), cos(pitch)sin(yaw))`
- Mouse drag → yaw/pitch deltas
- Scroll → distance zoom (clamp min/max)
- Same `Camera3D` struct, perspective mode

### Render pipeline considerations

- `Renderer3D` pipeline has depth test + back-face culling enabled. `RenderPipeline::createDepthResources()` already exists — render pass has depth attachment. Compatible.
- 3D pass runs first (writes depth), 2D UI overlay pass runs after (disabled depth). Matches existing design comment in `Renderer3D.hpp`.
- `MAX_FRAMES_IN_FLIGHT = 2` aligns between Renderer2D, Renderer3D, RenderPipeline.

### Estimated effort

1-2 days. Risk: none major. Renderer3D is tested but unused — may surface init-order issues on first integration.

## Known Limitations of Path B

- Hex tiles float as discs on sphere — gaps between tiles visible if zoomed close
- Gaps can be masked by tinting sphere mesh underneath (ocean blue for water tiles, neutral for land)
- Pole distortion (hexes become narrow slivers at high latitude) — accepted tradeoff

## Alternative Paths Considered

- **Path A — 2.5D sprite projection** (~4 hours) — fake 3D by distorting flat map with curvature shader. Cheap, looks obviously flat near edges. Rejected for quality.
- **Cube sphere** — 4-connectivity grid, wrong for hex-based game. Rejected.
- **Goldberg rewrite** — correct but massive refactor. Deferred forever unless gameplay demands uniform sphere travel.
