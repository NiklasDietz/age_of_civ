# Future Improvements

## Globe Map (3D Spherical World)

**Concept:** Replace the flat hex grid with a 3D globe rendered as a sphere, differentiating from Civ 6's flat map.

**Technical approach:**
- Use a Goldberg polyhedron (subdivided icosphere) to create hex/pentagon tiles on a sphere
- Leverage the existing `Renderer3D` (708 lines) and `forward3d` shaders already in the Vulkan renderer
- Orbit camera that rotates around the globe, zooms in to street level
- Natural map wrapping (no edge of the world)
- Pathfinding changes from 2D hex A* to geodesic distance on sphere surface
- Map generation using spherical plate tectonics, climate zones by latitude
- Game logic (GameState/Player/City/Unit) is mostly position-independent

**Existing infrastructure:**
- `third_party/vulkan_renderer/src/Renderer3D.cpp` (708 lines, currently unused)
- `shaders/forward3d.vert.glsl` and `forward3d.frag.glsl` (pre-compiled SPV available)
- Simulation is decoupled from rendering (HeadlessSimulation proves this)

**Effort estimate:** ~2 weeks focused work

**Why this differentiates:**
- No major 4X game has a true globe view
- Makes geography feel real (pole-to-pole climate, natural ocean barriers)
- Eliminates the immersion-breaking "edge of the world"
- Visually striking for marketing/screenshots

---

## Spectator Mode Visual Fix

The spectator mode HUD overlay (`SpectatorHUD`) breaks terrain rendering when `m_spectatorMode = true`. With the flag set to false, terrain renders perfectly. The issue is in one of the `!m_spectatorMode` guards in `Application::run()` that skips something the renderer needs, or in the HUD's `resetCamera()`/`setZoom(1.0)` call interfering with the world-space camera. Needs further debugging to isolate which guard causes the problem.

---

## Influence Maps

Overlay influence data on the hex grid showing territory control, threat zones, and strategic value. Propagate unit/city influence with exponential decay. Use for military positioning, settler placement, border detection. See `docs/AI_MULTI_SYSTEM_COORDINATION.md` for design details.

---

## Strategic AI Improvements

- Budget allocation system (divide production capacity between military/expansion/infrastructure)
- Deeper diplomatic AI (alliance networks, trade leverage, war coalitions)
- Wonder race AI (track what other players are building)
- Religion victory path AI
- Naval invasion planning
- Nuclear weapon strategy

---

## Gameplay Features

- Map editor (already has `MapEditor.cpp` stub)
- Multiplayer networking (GameServer infrastructure exists)
- Mod support via Lua scripting (LuaEngine exists)
- Replay system (ReplayRecorder exists)
- Encyclopedia/Civilopedia (Encyclopedia.cpp exists)
- Sound effects and music (audio system exists but no assets)
