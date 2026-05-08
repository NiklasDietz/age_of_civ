# Browser Port Feasibility (idea, not scheduled)

Captured 2026-05-08. Do not work on this without explicit go-ahead.

## Goal
Run Age of Civ in a browser tab via WebAssembly. Same simulation,
same content, no native install.

## Required changes

### Build
- CMake -> `emcmake cmake` (Emscripten toolchain).
- Output `.html` + `.js` + `.wasm` + asset pack.
- Asset bundling via `--preload-file data/` and shaders.

### Windowing / input
- Drop GLFW for browser target.
- Use SDL2-emscripten (`-s USE_SDL=2`) or raw HTML5 input hooks.
- Mouse / keyboard events translate cleanly. No raw input quirks.
- Window resize via `Module.canvas.resize` + emscripten resize callback.

### Renderer (biggest item)
- `third_party/vulkan_renderer` is Vulkan-only. Browsers do not
  expose Vulkan.
- Two backend options:
  1. **WebGPU**: closest to Vulkan model. Compute shaders, bind
     groups, command buffers. Available in Chrome / Edge stable, Safari
     17+, Firefox behind flag. Future-proof.
  2. **WebGL2**: universal, but no compute, fewer features, more
     glue to fake what Vulkan does for free.
- Shader rewrite: GLSL 450 -> WGSL (WebGPU) or GLSL ES 3.00 (WebGL2).
  `forward3d.{vert,frag}.glsl` and `renderer2d.*` need ports.
- Recommended: switch to a portable backend (wgpu-native via C ABI,
  or SDL3-GPU) so native + browser share one path. Avoids two
  hand-maintained renderer trees. ~1-2 months FT.

### Threading
- pthreads work in browsers via Web Workers + `SharedArrayBuffer`,
  but the page must serve with `Cross-Origin-Opener-Policy: same-origin`
  + `Cross-Origin-Embedder-Policy: require-corp` headers.
- OpenMP partial; `#pragma omp parallel for` works with
  `-pthread`. The big loops (SphereFieldPhysics, MapGenerator) keep
  scaling.
- No multi-process. No `fork()`. CLI tools (`aoc_mapgen`, `aoc_simulate`)
  drop on the browser target.

### File I/O
- Emscripten virtual FS for read-only assets (preloaded).
- Saves: IndexedDB or `localStorage` via emscripten FS sync.
- No direct filesystem; users save to browser storage, optional
  download as file.

### Audio
- SDL_mixer-emscripten or raw Web Audio API. Same sample assets.

## Performance estimate

| Layer            | Native baseline    | Browser (WebGPU)   | Browser (WebGL2)   |
|------------------|--------------------|--------------------|--------------------|
| Sim (CPU only)   | 100 %              | 70-90 %            | 70-90 %            |
| Renderer         | 100 %              | 80-95 %            | 50-70 %            |
| Mesh upload      | fast               | fine               | sluggish           |
| 3D globe (259K quads) | smooth        | smooth             | borderline / chop  |
| Map gen 3 Gy / 60 epochs | 3-8 s       | 5-15 s             | 5-15 s             |
| First-load size  | 5-10 MB            | 50-100 MB WASM+assets | same            |

Worst-case browser FPS lands at ~40-50 % of native on WebGL2 hardware,
~70 % on WebGPU. Sim throughput barely changes.

## Bottlenecks

- Per-scrub mesh rebuild (33 MB GPU upload): fine on WebGPU, slow on
  WebGL2.
- `vkDeviceWaitIdle` semantics are stricter in browser drivers; would
  need replacing with frame fences.
- 720x360 SphereField allocations + per-epoch advection are CPU-bound
  and unaffected.
- Bandwidth: first load 50-100 MB. Subsequent visits cache.

## Practical paths

1. **Portable backend** (recommended)
   - Switch render layer to wgpu (via dawn-cpp) or SDL3-GPU.
   - One codebase, native + browser.
   - Cost: 1-2 months FT to port `third_party/vulkan_renderer`,
     1-2 weeks for shader translation, 2-3 weeks for build + glue +
     testing.

2. **Two backends**
   - Keep Vulkan native, write WebGPU shim for browser.
   - Maintenance burden 2x.
   - Total cost similar to (1) but ongoing duplication.

3. **Cloud streaming**
   - Stream native binary via Moonlight / Sunshine / GeForce-Now-style
     server.
   - Zero port effort. Works today.
   - Costs server hardware + per-user GPU time. Latency depends on
     client geography.

## Risks

- WebGPU still maturing on mobile browsers (Safari iOS spotty).
- COOP/COEP headers required for threads -- limits embedding into
  third-party sites.
- Large initial download deters casual visitors.
- IndexedDB save quotas vary per browser (typically 50 MB - 1 GB).

## Realistic verdict

Achievable. ~1-3 months FT for the portable-backend path. Performance
in WebGPU browsers ~70 % of native, in WebGL2 fallback ~50 %.
Simulation logic untouched. Biggest single item is the Vulkan ->
WebGPU/wgpu rewrite of `third_party/vulkan_renderer`.

## Not in scope right now

This is a captured idea only. Do not start work without an explicit
go-ahead and a sized milestone.
