# Coding Standards

- C++20 for all code.
- The claude-code-tweaks standards skills (core/cpp/testing/etc.) auto-load per task;
  follow them.

## Project Context

- Civ-like 4X game: hex map, plate-tectonics worldgen, economy/AI sim, Vulkan + GLFW
  renderer (headless build via `-DAOC_HEADLESS=ON`), LuaJIT scripting.
- Layout: headers in `include/aoc/<subsystem>/`, sources mirrored in `src/<subsystem>/`.
  One static lib `aoc_lib` + executables `age_of_civ`, `aoc_simulate`, `aoc_trace_dump`,
  `aoc_mapgen`.
- Build: CMake presets — `cmake --preset debug` (ASan/UBSan) or `release`, then
  `cmake --build --preset <name>`. Binaries land in `build/<preset>/`.
- Tests: `ctest --test-dir build/<preset> --output-on-failure`.
- Headless smoke: `./build/release/aoc_simulate --turns 100 --players 4 --seed 42`.
- Dependencies: see `DEPENDENCIES.txt`. Vendored: cpp-httplib (pinned deliberately),
  stb_truetype (bundled fonts ONLY — unpatched CVE-2026-5314 OOB read on hostile
  fonts), stb_image_write, vulkan_renderer (git submodule).
