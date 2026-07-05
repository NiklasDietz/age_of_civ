# Component: app

## Responsibility

The interactive entry point. Creates the GLFW window, manages Vulkan context lifetime,
runs the main game loop, handles all user input (mouse, keyboard, hotkeys), routes unit
selection, and drives the server/client tick and the render pipeline. Built only when
`AOC_HEADLESS=OFF`.

## Key files

- `src/app/Application.cpp` /
  [include/aoc/app/Application.hpp](../../../include/aoc/app/Application.hpp) —
  `Application`: owns `Window`, `GameServer`, `GameClient`, `LocalTransport`,
  `GameRenderer`, `UIManager`, `ScreenRegistry`, `LuaEngine`, `DataLoader`,
  `InputManager`, `HotkeyManager`, `DebugServer` (optional). `run()` is the main loop:
  poll GLFW events → `GameClient::pollUpdates()` → per-frame logic → `GameServer::tick()`
  → `GameRenderer::render()`.
- `src/app/Application_HUD.cpp` — HUD overlay rendering helpers split from the main
  `Application.cpp` for file-size management.
- `src/app/Window.cpp` — GLFW window creation, Vulkan surface setup, swapchain resize
  callbacks.
- `src/app/InputManager.cpp` — Mouse click → tile coordinate conversion via
  `CameraController`, dispatches move/attack/found-city commands to `GameClient`.
- `src/app/HotkeyManager.cpp` — Key binding table; dispatches hotkey actions to the
  active screen.
- `src/app/UnitSelection.cpp` — Tracks the currently selected unit and city, drives
  move-preview overlays.
- `src/app/DebugCommandFile.cpp` — Reads a text file of debug commands at startup for
  automated testing/replay scripting.
- `src/app/ScreenshotEncoder.cpp` — Captures the swapchain framebuffer and encodes it
  to PNG via `stb_image_write`.

## Public surface

`Application` is instantiated from `src/main.cpp` (the `age_of_civ` executable entry
point). All other subsystems are created and owned by `Application`. There is no public
header surface into `app` from other subsystems — the dependency arrow is strictly
outward.

## Internal structure

Flat directory. `Application.cpp` is deliberately large (the main loop is a single
translation unit) but delegates visual HUD work to `Application_HUD.cpp`. Input and
selection are separated into their own files to keep concerns distinguishable.
