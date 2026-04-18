# Cross-Platform Build Guide

This project builds natively on Linux, Windows, and macOS. The renderer is
Vulkan everywhere; on macOS Vulkan calls are translated to Apple Metal by
MoltenVK, shipped as part of the LunarG Vulkan SDK.

All three platforms share the same `CMakeLists.txt`. Platform-specific logic
is guarded by `AOC_PLATFORM_LINUX` / `AOC_PLATFORM_WINDOWS` /
`AOC_PLATFORM_MACOS` set at the top of the root `CMakeLists.txt`.

---

## Prerequisites (all platforms)

- CMake >= 3.20
- A C++20-capable compiler
- Git (for submodules)
- `git submodule update --init --recursive` after cloning

For the headless build (`-DAOC_HEADLESS=ON`) you do **not** need Vulkan or
GLFW. That variant is intended for CI, servers, and GA training boxes.

---

## Linux

### Install deps

openSUSE:

    sudo zypper install cmake gcc-c++ clang lld pkgconf ccache git \
        luajit-devel vulkan-devel glfw-devel

Debian/Ubuntu:

    sudo apt install cmake clang lld pkg-config ccache git \
        libluajit-5.1-dev libvulkan-dev libglfw3-dev

Arch:

    sudo pacman -S cmake clang lld pkgconf ccache git \
        luajit vulkan-icd-loader vulkan-headers glfw

### Build

Using CMake presets (Clang + Ninja + lld, canonical dev setup):

    cmake --preset debug
    cmake --build --preset debug -j

Manual configure (GCC is also fine):

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j

The `debug`, `release`, `perf`, and `tsan` presets are Linux-only
(they pin `clang`/`clang++` and `-fuse-ld=lld`). Other presets:
`macos-debug`, `macos-release`, `windows-debug`, `windows-release`.

---

## Windows (MSVC, recommended)

MSVC is the recommended compiler. MinGW-w64 should also work but is not the
primary supported path -- the root `CMakeLists.txt` treats everything
`if(WIN32)` uniformly, with additional `if(MSVC)` blocks for MSVC-specific
flags (`/utf-8`, `/Zc:__cplusplus`, `/Zc:preprocessor`, `/EHsc`,
`NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `/MP`).

### Install deps

1. **Visual Studio 2022** with the "Desktop development with C++" workload.
2. **Vulkan SDK** from <https://vulkan.lunarg.com/sdk/home#windows>. The
   installer sets `VULKAN_SDK`, adds SDK bin to `PATH`, and provides
   `glslangValidator`, `glslc`, and the loader. Open a fresh shell after
   install so the env vars are visible.
3. **vcpkg** (easiest route for glfw3 + luajit):

        git clone https://github.com/microsoft/vcpkg
        .\vcpkg\bootstrap-vcpkg.bat
        .\vcpkg\vcpkg install glfw3:x64-windows luajit:x64-windows

### Configure and build (preset)

From a "x64 Native Tools Command Prompt for VS 2022" or PowerShell:

    set VCPKG_ROOT=C:\path\to\vcpkg
    cmake --preset windows-release ^
        -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
    cmake --build --preset windows-release

### Configure and build (manual)

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
        -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
    cmake --build build --config Release -j

Output: `build\Release\age_of_civ.exe`. CMake copies shaders and
`data/definitions/*.json` next to the binary via `POST_BUILD` steps.

### Notes

- If `find_package(glfw3 REQUIRED)` fails without vcpkg, pass
  `-Dglfw3_DIR=<glfw-install>/lib/cmake/glfw3`.
- Build warns (not errors) if `VULKAN_SDK` is unset -- the subsequent
  `find_package(Vulkan REQUIRED)` is what actually fails.
- Networking stubs in `src/net/` get linked against `ws2_32` and
  `iphlpapi` already; no further action needed when real socket code lands.

---

## macOS (Apple Silicon or Intel, via MoltenVK)

There is no native Vulkan driver on macOS. The LunarG SDK bundles MoltenVK,
which implements the Vulkan API on top of Metal. Minimum deployment target
is 11.0 (Big Sur).

### Install deps

Option A -- LunarG SDK (recommended, matches Windows/Linux flow):

1. Download the macOS SDK from <https://vulkan.lunarg.com/sdk/home#mac>.
2. Run the installer and follow the LunarG setup script, which exports
   `VULKAN_SDK`, `VK_ICD_FILENAMES`, `VK_LAYER_PATH`, and prepends the SDK
   `bin/` to `PATH` in your shell rc file.
3. `brew install cmake ninja pkg-config ccache glfw luajit`

Option B -- Homebrew-only (works, less tooling):

    brew install cmake ninja pkg-config ccache luajit glfw \
        molten-vk vulkan-headers vulkan-loader glslang shaderc

With Option B, `find_package(Vulkan)` resolves MoltenVK via the Homebrew
Vulkan loader. `VULKAN_SDK` can stay unset -- CMake just prints a
status message.

### Build

    cmake --preset macos-release
    cmake --build --preset macos-release

Or manually:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j

Output: `build/macos-release/age_of_civ`. MoltenVK is loaded at runtime via
the Vulkan loader. The binary links Cocoa, IOKit, QuartzCore, Metal, and
Foundation frameworks (configured in the root `CMakeLists.txt`).

### Notes

- On Apple Silicon (`arm64`) all of this is native. Rosetta 2 is not needed.
- If you get `VK_ERROR_INCOMPATIBLE_DRIVER` at startup, the ICD file isn't
  being found -- export `VK_ICD_FILENAMES` pointing at MoltenVK's JSON
  (LunarG SDK does this automatically; Homebrew users may need to source
  `$(brew --prefix molten-vk)/share/vulkan/icd.d/MoltenVK_icd.json`).

---

## Headless build (any platform)

No Vulkan / GLFW / rendering code:

    cmake -S . -B build-headless -DAOC_HEADLESS=ON
    cmake --build build-headless -j --target aoc_simulate aoc_evolve

This produces `aoc_simulate` and `aoc_evolve` only; the interactive
`age_of_civ` target is skipped.

---

## Verifying the Vulkan toolchain is visible

CMake prints:

    -- Target platform: <Linux|Windows|macOS>
    -- Vulkan library: /path/to/libvulkan.{so,dylib} or vulkan-1.lib
    -- Vulkan includes: /path/to/include
    -- Found glslangValidator: ...   (informational)
    -- Found glslc: ...              (informational)

The `glslang` / `glslc` status lines are non-fatal. Shader compilation is
done at runtime via libshaderc (loaded via `dlopen` / `LoadLibrary` from
`ShaderBuilder.cpp`), so those binaries are only needed for offline
workflows.

---

## Known limitations

- **vcpkg integration on Windows is documented but not automated.** The
  CMake file does not invoke `vcpkg integrate install`; the user passes
  `CMAKE_TOOLCHAIN_FILE` explicitly.
- **MinGW on Windows is untested.** The `if(MSVC)` branch sets MSVC-only
  flags; MinGW falls through to the Clang/GCC branch cleanly, but nobody
  has verified a full build.
- **No universal binary on macOS.** The build defaults to the host
  architecture. For an Apple Silicon + Intel universal build, set
  `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.
- **Shader runtime compilation on Windows** relies on `shaderc_shared.dll`
  from the Vulkan SDK being on `PATH`. Installer does this by default.
