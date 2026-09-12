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
- `third_party/vulkan_renderer/src/Renderer3D.cpp` -- NO LONGER UNUSED.
  `src/render/GlobeRenderer.cpp` (444 lines) owns one Renderer3D instance,
  `Application` constructs and initialises it, and there is a `m_creatorGlobe`
  path. Audited 2026-09-08: this item is partly built, not greenfield.
- `shaders/forward3d.vert.glsl` and `forward3d.frag.glsl` (pre-compiled SPV available)
- Simulation is decoupled from rendering (HeadlessSimulation proves this)

**Effort estimate:** ~2 weeks focused work

**Why this differentiates:**
- No major 4X game has a true globe view
- Makes geography feel real (pole-to-pole climate, natural ocean barriers)
- Eliminates the immersion-breaking "edge of the world"
- Visually striking for marketing/screenshots

**2D toggle requirement:** when the 3D mode lands it should become the
default, but a 2D flat-hex view must remain selectable (toggle button
near the minimap, plus `--flat` CLI flag). 2D stays useful for
AI/GA/headless workflows that do not need the camera cost, and for
players who prefer the flat overview.

---

## Screenshot via Vulkan Swapchain Readback -- DONE

Audited 2026-09-08: shipped. `Application::captureScreenshot` calls
`m_renderPipeline->readSwapchainPixels(...)` and hands the result to
`writeScreenshotPng` (`src/app/ScreenshotEncoder.cpp`, which also owns the one
`STB_IMAGE_WRITE_IMPLEMENTATION` expansion). No `spectacle` call remains
anywhere in the tree, so the compositor-focus problem below is history. Kept
for the rationale.

The original problem: `GameDBus::TakeScreenshot` forked `spectacle -a` to grab
the active window. On Wayland compositors (KDE/GNOME) an app without an
xdg-activation token cannot raise itself over another focused window
(e.g., an editor), so the screenshot captures whatever is frontmost rather
than the game. The advisory calls (`glfwRequestWindowAttention`,
`glfwFocusWindow`) do nothing in that scenario.

**Clean fix:** read the swapchain image directly inside `VulkanRenderer`
(mirrors the existing offscreen readback at `VulkanRenderer.cpp:570`).
Steps:
1. Ensure the swapchain is created with `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`.
2. After `vkQueuePresentKHR` completes (wait on the present fence),
   transition the last-presented image to `TRANSFER_SRC_OPTIMAL`.
3. `vkCmdCopyImageToBuffer` into a host-visible staging buffer.
4. Map, PNG-encode (stb_image_write), return bytes.

This is compositor-independent, deterministic, and captures the exact game
pixels regardless of window focus. Estimated effort: ~1 day.

Until this lands, DBus callers must make the game window frontmost before
calling `TakeScreenshot`.

---

## Spectator Mode Visual Fix

The spectator mode HUD overlay (`SpectatorHUD`) breaks terrain rendering when `m_spectatorMode = true`. With the flag set to false, terrain renders perfectly. The issue is in one of the `!m_spectatorMode` guards in `Application::run()` that skips something the renderer needs, or in the HUD's `resetCamera()`/`setZoom(1.0)` call interfering with the world-space camera. Needs further debugging to isolate which guard causes the problem.

---

## Influence Maps

Audited 2026-09-08: genuinely unbuilt. No `InfluenceMap` type exists.

Overlay influence data on the hex grid showing territory control, threat zones, and strategic value. Propagate unit/city influence with exponential decay. Use for military positioning, settler placement, border detection. See `docs/AI_MULTI_SYSTEM_COORDINATION.md` for design details.

---

## Strategic AI Improvements

Audited 2026-09-08. Budget allocation and nuclear strategy exist
(`AIController.cpp`, `AIMilitaryController.cpp`, `UtilityScoring.cpp`,
`LeaderPersonality.cpp`). The rest found no implementation:

- ~~Budget allocation system~~ -- built
- Deeper diplomatic AI (alliance networks, trade leverage, war coalitions)
- Wonder race AI (track what other players are building) -- UNBUILT
- Religion victory path AI -- UNBUILT
- Naval invasion planning -- UNBUILT
- ~~Nuclear weapon strategy~~ -- built

One finding of its own, from the golden re-bless of 2026-09-08:
`AIMilitaryController.cpp:895` chooses its war target by raw
`militaryUnitCount()` with no strength weighting, and re-declares after every
peace. A 6%-per-era-step combat modifier -- which the AI never even reads,
since it applies only inside combat resolution -- produced a 2.3x war rate
(64 -> 145 declarations over 350 turns on seed 42). Any combat buff will be
amplified this way until the target choice weighs strength.

Phase 4 of the money and trade programme (2026-09-12) changed the declaration
rather than the choice, and the distinction matters for this finding.
`requestDeclareWar` now refuses an AI actor whose trade with the target is
worth more than `WAR_COST_VETO_POINTS` (plan 4.2), and because all three AI
war paths go through that request, this one included, a partner worth keeping
is skipped before any target is committed to. The choice itself still reads
raw `militaryUnitCount()`, so the amplification described above is unchanged
for every civ we do not trade with, and weighing strength there is still the
fix. Measured at 200 turns and 4 players on the native preset: 18
declarations on seed 42 and 14 on seed 43.

---

## Gameplay Features

- Map editor -- there is NO `MapEditor.cpp`; the stub this claimed does not
  exist (audited 2026-09-08)
- Multiplayer networking (GameServer infrastructure exists)
- Mod support via Lua scripting (LuaEngine exists)
- Replay system (ReplayRecorder exists)
- Encyclopedia/Civilopedia (Encyclopedia.cpp exists)
- Sound effects and music (audio system exists; still no `assets/` directory)
