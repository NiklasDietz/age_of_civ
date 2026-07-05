# Component: render

## Responsibility

Drives all Vulkan rendering: the hex map, unit sprites, combat animations, globe view,
minimap, overlays, and the 2D sprite/particle layer. Excluded entirely in headless builds
(`AOC_HEADLESS=ON`).

## Key files

- [include/aoc/render/GameRenderer.hpp](../../../include/aoc/render/GameRenderer.hpp) —
  `GameRenderer`: top-level orchestrator. Holds references to `MapRenderer`,
  `UnitRenderer`, `Minimap`, `CombatAnimation`, `Particles`, `UIManager`, and `Tooltip`.
  `render()` takes a `VkCommandBuffer`, `CameraController`, `HexGrid`, `GameState`, and
  `FogOfWar` and sequences all sub-renderers.
- [include/aoc/render/MapRenderer.hpp](../../../include/aoc/render/MapRenderer.hpp) —
  Per-tile terrain and improvement rendering. Reads `HexGrid` terrain types, improvements,
  and fog visibility to select sprite atlas frames.
- [include/aoc/render/UnitRenderer.hpp](../../../include/aoc/render/UnitRenderer.hpp) —
  Draws unit sprites at their current tile positions, with move/attack highlight overlays.
- [include/aoc/render/GlobeRenderer.hpp](../../../include/aoc/render/GlobeRenderer.hpp) —
  Optional globe view (Mollweide projection of the full map for strategic zoom).
- [include/aoc/render/MapOverlays.hpp](../../../include/aoc/render/MapOverlays.hpp) —
  Overlay layers: yields, political borders, appeal, religion spread, climate.
- [include/aoc/render/Minimap.hpp](../../../include/aoc/render/Minimap.hpp) — Minimap
  widget drawn into a corner of the screen; updates on territory and fog changes.
- [include/aoc/render/CombatAnimation.hpp](../../../include/aoc/render/CombatAnimation.hpp)
  — Interpolates unit positions and hit-flash effects during combat resolution.
- [include/aoc/render/Particles.hpp](../../../include/aoc/render/Particles.hpp) —
  Simple CPU particle system (smoke, explosion sparks, rally banners).
- [include/aoc/render/SpriteRenderer.hpp](../../../include/aoc/render/SpriteRenderer.hpp)
  — Low-level 2D sprite batching into the `Renderer2D` draw call.
- [include/aoc/render/TextureAtlas.hpp](../../../include/aoc/render/TextureAtlas.hpp) —
  Manages the single texture atlas uploaded to GPU at startup; maps sprite names to UV
  rects.
- [include/aoc/render/DrawCommandBuffer.hpp](../../../include/aoc/render/DrawCommandBuffer.hpp)
  — Batches 2D draw calls before flushing to the GPU pipeline.
- [include/aoc/render/CameraController.hpp](../../../include/aoc/render/CameraController.hpp)
  — Tracks pan/zoom state; converts screen coordinates to hex tile coordinates for input.

## Public surface

- `GameRenderer::initialize(pipeline, renderer2d)` — called once by `Application` after
  Vulkan and GLFW are set up.
- `GameRenderer::render(renderer2d, commandBuffer, frameIndex, camera, grid, gameState, fog, …)`
  — called every frame by `Application`.
- `CameraController` — read by both `GameRenderer` and `InputManager` (click-to-tile
  mapping for unit selection).

## Internal structure

Flat directory: one class per file, no sub-directories. All rendering goes through
`vulkan_app::RenderPipeline` and `vulkan_app::renderer::Renderer2D` types defined in the
`vulkan_renderer` submodule (`third_party/vulkan_renderer/`). The render subsystem reads
`HexGrid` and `GameState` as const references; it never writes game data.
