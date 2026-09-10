# Third-Party Dependencies

All confirmed by tracing import sites in the source tree. Listed in order: system
libraries, vendored libraries, the Vulkan submodule, and the Python tooling.

---

## LuaJIT 2.1+ (preferred) / Lua 5.4 (fallback)

**What the code uses:** `lua_State` lifetime via `LuaEngine` PIMPL;
`lua_pcall` / `luaL_loadfile` / `lua_pushlstring` / `lua_register` for script execution
and C-function registration. The engine also calls `lua_newtable` and `lua_setfield` to
build the read-only game-state table exposed to scripts.

**Why pulled in:** Moddable game logic — victory conditions, world events, AI personality
overrides, building/unit special effects, and map generation rules are authored in Lua
and loaded at runtime. LuaJIT is preferred for ~10-50× faster script execution over stock
Lua 5.4.

**Compile guard:** `AOC_HAS_LUA` defined by CMake when either LuaJIT or Lua 5.4 headers
are found. `LuaEngine` is a no-op stub when the define is absent.

Import site: [src/scripting/LuaEngine.cpp](../../src/scripting/LuaEngine.cpp) — the only
file that references `LuaEngine`; no executable constructs one today.

---

## Vulkan loader + headers

**What the code uses:** `VkCommandBuffer`, `VkDevice`, `VkSwapchainKHR`, `vkCreateBuffer`,
`vkQueueSubmit`, and the full Vulkan 1.3 API via `<vulkan/vulkan.h>`. Usage is
concentrated in `third_party/vulkan_renderer/` (the submodule) and in
`src/render/DrawCommandBuffer.cpp`, `src/app/Window.cpp`.

**Why pulled in:** The sole graphics API for all platforms (MoltenVK on macOS).

**Guard:** `NOT AOC_HEADLESS` — the entire render subsystem is excluded from headless
builds. `find_package(Vulkan REQUIRED)` at `CMakeLists.txt:135`.

---

## GLFW 3.x

**What the code uses:** `glfwCreateWindow`, `glfwPollEvents`, `glfwGetFramebufferSize`,
`glfwSetKeyCallback` / `glfwSetMouseButtonCallback` / `glfwSetScrollCallback`,
`glfwCreateWindowSurface` (Vulkan surface creation).

**Why pulled in:** Cross-platform window creation and input event loop.

**Guard:** `NOT AOC_HEADLESS`; `find_package(glfw3 REQUIRED)` at `CMakeLists.txt:165`.
Import site: [src/app/Window.cpp](../../src/app/Window.cpp)

---

## OpenMP (optional)

**What the code uses:** `#pragma omp parallel for` over per-tile passes.

**Why pulled in:** Parallel per-tile work in the map generator's post-processing and physics
passes; without OpenMP the same loops run serially.

**Guard:** `find_package(OpenMP)` at `CMakeLists.txt:618`; when found, `aoc_lib` links
`OpenMP::OpenMP_CXX` and defines `AOC_HAS_OPENMP=1`.

Import sites: [src/map/MapGenerator.cpp](../../src/map/MapGenerator.cpp),
[src/map/gen/SphereFieldPhysics.cpp](../../src/map/gen/SphereFieldPhysics.cpp),
[src/map/gen/PostSim.cpp](../../src/map/gen/PostSim.cpp),
[src/map/gen/Features.cpp](../../src/map/gen/Features.cpp),
[src/tools/MapGenCli.cpp](../../src/tools/MapGenCli.cpp)

---

## pthreads (system)

**What the code uses:** `std::thread` (C++ stdlib wraps pthreads on Linux/macOS).
Used in `LocalTransport` (owner-thread assertion via `std::this_thread::get_id()`),
`aoc::log::g_minSeverity` (atomic), the debug server's request thread, and the ML thread
pool. `find_package(Threads REQUIRED)` at `CMakeLists.txt:612`.

**Why pulled in:** System library; required for any multi-threaded code.

---

## cpp-httplib 0.18.5 (vendored, pinned)

**What the code uses:** `httplib::Server::Get()` / `Post()`, `set_pre_routing_handler`,
`bind_to_port`, `listen_after_bind`, `stop`.

**Why pulled in:** The development debug server (`DebugServer`) that exposes game state and
the game-control API via HTTP on localhost, and the `aoc_mapgen` inspector. Pinned to
0.18.5 deliberately: the server binds `127.0.0.1` only with a Host-header allowlist and is
off by default, so the server-side CVEs patched in newer releases (all
untrusted-network-client class) are out of reach in this deployment.

Import site: [src/debug/DebugServer.cpp](../../src/debug/DebugServer.cpp)

---

## stb_truetype v1.26 (vendored, build-time only)

**What the code uses:** `stbtt_InitFont`, `stbtt_GetCodepointBitmap`,
`stbtt_GetCodepointHMetrics` — rasterisation of the bundled fonts into the glyph atlas.

**Why pulled in:** Font rasterisation. Since the atlas bake it is linked exclusively by the
`aoc_font_bake` target (`CMakeLists.txt:900`); the game binary contains no TrueType parser
and reads the baked, bounds-checked blob described by `FontAtlasFormat.hpp` instead.

**Security note:** Unpatched CVE-2026-5314 OOB read on hostile font files. The baker's font
path list is a fixed set of trusted system paths; never feed it mod or user-supplied fonts.
Verify the runtime has no parser with `nm build/release/libaoc_lib.a | grep -c stbtt`
(expect 0), per `DEPENDENCIES.txt`.

Import site: [src/tools/FontBake.cpp](../../src/tools/FontBake.cpp); runtime reader
[src/ui/BitmapFont.cpp](../../src/ui/BitmapFont.cpp)

---

## stb_image_write v1.16 (vendored)

**What the code uses:** `stbi_write_png_to_func` — encodes a framebuffer capture to PNG.

**Why pulled in:** Screenshot export (`ScreenshotEncoder`, also reachable through
`POST /debug/screenshot`).

Import site: [src/app/ScreenshotEncoder.cpp](../../src/app/ScreenshotEncoder.cpp)

---

## doctest v2.4.12 (vendored, test-only)

**What the code uses:** `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`, `TEST_CASE`, `REQUIRE`,
`CHECK` macros.

**Why pulled in:** Unit test harness for all `tests/*.cpp` test files.

Import site: test sources under [tests/](../../tests/)

---

## vulkan_renderer (git submodule)

**What the code uses:** `vulkan_app::RenderPipeline`, `vulkan_app::renderer::Renderer2D`,
`vulkan_app::GraphicsDevice` — the complete Vulkan abstraction layer providing pipeline
management, 2D sprite batching, and frame synchronization.

**Why pulled in:** Encapsulates the Vulkan boilerplate (swapchain, render passes,
command pool, descriptor sets) so `render/` and `app/` work at a higher abstraction
level.

Location: `third_party/vulkan_renderer/` (git submodule).
Import site: [src/app/Application.cpp](../../src/app/Application.cpp),
[src/render/GameRenderer.cpp](../../src/render/GameRenderer.cpp)

---

## sdbus-cpp (optional, Linux only)

**What the code uses:** D-Bus object registration and signal emission for desktop
integration (taskbar progress, Unity launcher rich presence).

**Why pulled in:** Linux-specific UX polish; compiled only when sdbus-cpp is detected.
The feature is guarded by `AOC_HAS_SDBUS`.

Import site: [src/net/GameDBus.cpp](../../src/net/GameDBus.cpp)

---

## mcp (Python, tooling only)

**What the code uses:** `mcp.server.MCPServer` (mcp 2.0 API; the older `FastMCP` import is
gone) to register the 60 `aoc_*` tools and serve them over stdio; `mcp.ClientSession`,
`StdioServerParameters` and `stdio_client` in the end-to-end test.

**Why pulled in:** Lets an LLM client drive a running game through the debug server's REST
routes. Not part of the C++ build.

Import sites: [tools/mcp_server.py](../../tools/mcp_server.py),
[tools/mcp_e2e_test.py](../../tools/mcp_e2e_test.py). The other Python tools
(`scripts/sim_health.py`, `tools/mapgen_metrics.py`, `tools/mapgen_render.py`,
`tools/earth_reference.py`, `scripts/check_grid_layers.py`) use the standard library only.

---

## ccache (build-time, optional)

Used as `CMAKE_C_COMPILER_LAUNCHER` / `CMAKE_CXX_COMPILER_LAUNCHER` when found.
No code imports; pure build-time acceleration.
