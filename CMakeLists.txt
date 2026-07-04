cmake_minimum_required(VERSION 3.20)
project(AgeOfCiv VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --------------------------------------------------------------------------
# Platform detection (single source of truth for per-OS logic below)
# --------------------------------------------------------------------------
# AOC_PLATFORM_LINUX     -- desktop Linux (not Apple)
# AOC_PLATFORM_WINDOWS   -- Win32 (MSVC or MinGW)
# AOC_PLATFORM_MACOS     -- Apple/macOS (Vulkan supplied by MoltenVK)
set(AOC_PLATFORM_LINUX   FALSE)
set(AOC_PLATFORM_WINDOWS FALSE)
set(AOC_PLATFORM_MACOS   FALSE)
if(WIN32)
    set(AOC_PLATFORM_WINDOWS TRUE)
    message(STATUS "Target platform: Windows (${CMAKE_CXX_COMPILER_ID})")
elseif(APPLE)
    set(AOC_PLATFORM_MACOS TRUE)
    message(STATUS "Target platform: macOS (Vulkan via MoltenVK)")
elseif(UNIX)
    set(AOC_PLATFORM_LINUX TRUE)
    message(STATUS "Target platform: Linux/Unix")
else()
    message(FATAL_ERROR "Unsupported platform. Expected Linux, Windows, or macOS.")
endif()

# Use ccache if available (available on all three platforms; on Windows via
# choco/scoop install ccache). Skipped automatically on MSVC since CMake's
# ccache integration with the MSVC frontend is brittle -- users who want it
# should configure it explicitly.
if(NOT MSVC)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
    endif()
endif()

# --------------------------------------------------------------------------
# Windows / MSVC baseline: keep std:: headers sane and match our C++20 mode.
# --------------------------------------------------------------------------
if(MSVC)
    # /utf-8   -- treat source + execution as UTF-8 (matches Linux default).
    # /Zc:__cplusplus -- report the real C++ standard in __cplusplus.
    # /Zc:preprocessor -- conforming preprocessor (needed for some macros).
    # /EHsc    -- standard C++ exception model.
    # NOMINMAX -- kill the Windows.h min/max macros that break std::min/max.
    # WIN32_LEAN_AND_MEAN -- drop rarely-needed Win32 headers to cut build time.
    add_compile_options(/utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    # MSVC's parallel compile-per-target (separate from CMake's -j).
    add_compile_options(/MP)
endif()

# On macOS + AppleClang the default deployment target can be too low for C++20
# library features (<filesystem>, <ranges>). Require 11.0 which covers all the
# Vulkan-capable Macs (Intel Big Sur+, Apple Silicon from day one).
if(AOC_PLATFORM_MACOS AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
        "Minimum macOS version required to run the build" FORCE)
endif()

# --------------------------------------------------------------------------
# Build options
# --------------------------------------------------------------------------

# Headless build: skips Vulkan/GLFW/renderer and the interactive `age_of_civ`
# executable. Only `aoc_simulate` and `aoc_evolve` are built. Useful for
# servers, CI, and GA training on machines without a GPU / Vulkan SDK.
option(AOC_HEADLESS "Build without Vulkan/GLFW (sim + GA tools only)" OFF)

# Phase 1 diagnostic instrumentation. When ON, IndustrialRevolution.cpp emits
# throttled "IR_BLOCKED player=... ir=... reason=... detail=... turn=..."
# lines through LOG_INFO. Throttle window is 50 turns per (player, ir, reason,
# detail) tuple. Off in production -- compiled out entirely.
option(AOC_DIAG_IR "Enable IR-blocker diagnostic logging (Phase 1 only)" OFF)
if(AOC_DIAG_IR)
    add_compile_definitions(AOC_DIAG_IR=1)
endif()

# Physics-first plate-tectonic rewrite: default flipped ON 2026-05-06.
# SphereField (720x360 lat/lon) physics runs every build alongside the
# legacy per-plate pipeline. World-frame elevation still sources from
# legacy `orogenyLocal` until Phase 2 step 2 deletes the scatter passes;
# SphereField is a passenger in P2 step 1.
option(AOC_PHYSICS_ON_SPHEREFIELD
       "Run the SphereField physics-first epoch loop" ON)
if(AOC_PHYSICS_ON_SPHEREFIELD)
    add_compile_definitions(AOC_PHYSICS_ON_SPHEREFIELD=1)
endif()

# AddressSanitizer + LeakSanitizer for build_dbg. Gate via -DAOC_ENABLE_ASAN=ON
# at configure time. Combine with `ASAN_OPTIONS=suppressions=$PWD/asan_suppressions.txt`
# at runtime. Active across every target via CMAKE_*_FLAGS so static libs link in.
option(AOC_ENABLE_ASAN "Enable AddressSanitizer + LeakSanitizer" OFF)
if(AOC_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
    message(STATUS "AOC_ENABLE_ASAN=ON -- AddressSanitizer instrumentation enabled")
endif()

# --------------------------------------------------------------------------
# Dependencies
# --------------------------------------------------------------------------

if(NOT AOC_HEADLESS)
    # ---- Vulkan SDK discovery ---------------------------------------------
    # find_package(Vulkan) is called again inside third_party/vulkan_renderer
    # and vulkan_utils, but we call it here first so we can report a helpful
    # error message early if the SDK is missing on Windows / macOS.
    #
    # Platform notes:
    #   Linux   -- vulkan-loader + vulkan-headers from distro packages.
    #   Windows -- VULKAN_SDK env var points at C:\VulkanSDK\<ver>. CMake's
    #              FindVulkan reads $ENV{VULKAN_SDK} automatically.
    #   macOS   -- LunarG SDK ships MoltenVK (libvulkan.1.dylib + ICD).
    #              Homebrew's molten-vk + vulkan-headers also works.
    if(AOC_PLATFORM_WINDOWS AND NOT DEFINED ENV{VULKAN_SDK})
        message(WARNING
            "VULKAN_SDK environment variable is not set. Install the LunarG "
            "Vulkan SDK from https://vulkan.lunarg.com/sdk/home#windows and "
            "re-run cmake from a shell where VULKAN_SDK is exported.")
    endif()
    if(AOC_PLATFORM_MACOS AND NOT DEFINED ENV{VULKAN_SDK})
        message(STATUS
            "VULKAN_SDK not set -- relying on find_package(Vulkan) to locate "
            "MoltenVK via the system loader (Homebrew molten-vk) or LunarG SDK.")
    endif()
    find_package(Vulkan REQUIRED)
    message(STATUS "Vulkan library: ${Vulkan_LIBRARY}")
    message(STATUS "Vulkan includes: ${Vulkan_INCLUDE_DIR}")

    # Vulkan renderer submodule (builds as C++17 internally, that is fine).
    # This subdir calls find_package(Vulkan) again -- harmless, it caches.
    add_subdirectory(third_party/vulkan_renderer)

    # GLFW -- required for windowing. Discovery differs per platform:
    #   Linux   -- find_package(glfw3) via distro's glfw3Config.cmake.
    #   macOS   -- Homebrew installs the same config (glfw formula).
    #   Windows -- easiest path is vcpkg (vcpkg install glfw3), which drops
    #              the config files in the vcpkg CMake toolchain. Standalone
    #              builds must set glfw3_DIR to <glfw>/lib/cmake/glfw3.
    find_package(glfw3 REQUIRED)

    # macOS-specific: MoltenVK runs on top of Metal, so every binary that
    # links Vulkan transitively needs a handful of Apple frameworks. We
    # collect them here and attach them to aoc_lib once the target exists.
    if(AOC_PLATFORM_MACOS)
        find_library(AOC_COCOA_FRAMEWORK     Cocoa     REQUIRED)
        find_library(AOC_IOKIT_FRAMEWORK     IOKit     REQUIRED)
        find_library(AOC_QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
        find_library(AOC_METAL_FRAMEWORK     Metal     REQUIRED)
        find_library(AOC_FOUNDATION_FRAMEWORK Foundation REQUIRED)
        set(AOC_MACOS_FRAMEWORKS
            ${AOC_COCOA_FRAMEWORK}
            ${AOC_IOKIT_FRAMEWORK}
            ${AOC_QUARTZCORE_FRAMEWORK}
            ${AOC_METAL_FRAMEWORK}
            ${AOC_FOUNDATION_FRAMEWORK})
    endif()
endif()

# --------------------------------------------------------------------------
# Sources that pull in Vulkan/GLFW. Excluded from the headless build.
# --------------------------------------------------------------------------

set(AOC_GFX_ONLY_SOURCES
    src/core/Selectable.cpp
    src/render/CameraController.cpp
    src/render/CombatAnimation.cpp
    src/render/DrawCommandBuffer.cpp
    src/render/GameRenderer.cpp
    src/render/GlobeRenderer.cpp
    src/render/MapOverlays.cpp
    src/render/MapRenderer.cpp
    src/render/Minimap.cpp
    src/render/Particles.cpp
    src/render/SpriteRenderer.cpp
    src/render/TextureAtlas.cpp
    src/render/UnitRenderer.cpp
    src/app/Application.cpp
    src/app/DebugCommandFile.cpp
    src/debug/DebugServer.cpp
    src/app/Application_HUD.cpp
    src/app/Window.cpp
    src/app/InputManager.cpp
    src/app/HotkeyManager.cpp
    src/app/UnitSelection.cpp
    # All ui/ except GameNotifications pull in Renderer2D
    src/ui/BitmapFont.cpp
    src/ui/CityDetailTabs.cpp
    src/ui/DebugConsole.cpp
    src/ui/DiplomacyScreen.cpp
    src/ui/Encyclopedia.cpp
    src/ui/EventLog.cpp
    src/ui/LayoutBuilder.cpp
    src/ui/WidgetInspector.cpp
    src/ui/LoadingScreen.cpp
    src/ui/IconAtlas.cpp
    src/ui/UITestHarness.cpp
    src/ui/Localization.cpp
    src/ui/UIPersistence.cpp
    src/ui/GameScreens.cpp
    src/ui/MainMenu.cpp
    src/ui/MapEditor.cpp
    src/ui/Notifications.cpp
    src/ui/PauseMenu.cpp
    src/ui/ReligionScreen.cpp
    src/ui/ScoreScreen.cpp
    src/ui/ScreenRegistry.cpp
    src/ui/SettingsMenu.cpp
    src/ui/SpectatorHUD.cpp
    src/ui/Theme.cpp
    src/ui/Tooltip.cpp
    src/ui/TradeRouteSetupScreen.cpp
    src/ui/TradeScreen.cpp
    src/ui/Tutorial.cpp
    src/ui/UIManager.cpp
)

# --------------------------------------------------------------------------
# Game library  (everything except main.cpp)
# --------------------------------------------------------------------------

set(AOC_LIB_SOURCES
    # Core
    src/core/DecisionLog.cpp
    src/core/ErrorCodes.cpp
    src/core/PathGuard.cpp
    src/core/Selectable.cpp

    # Data loading
    src/data/DataLoader.cpp

    # Game object model
    src/game/GameState.cpp
    src/game/Player.cpp
    src/game/City.cpp
    src/game/Unit.cpp

    # Map
    src/map/HexGrid.cpp
    src/map/MapGenerator.cpp
    src/map/gen/Biogeography.cpp
    src/map/gen/ClimateBiome.cpp
    src/map/gen/CliffCoast.cpp
    src/map/gen/CoastalErosion.cpp
    src/map/gen/EarthSystem.cpp
    src/map/gen/HexNeighbors.cpp
    src/map/gen/IceAndRock.cpp
    src/map/gen/LakePurge.cpp
    src/map/gen/Mappability.cpp
    src/map/gen/PostSim.cpp
    src/map/gen/AtmosphereOcean.cpp
    src/map/gen/BiomeSubtypes.cpp
    src/map/gen/KoppenStructures.cpp
    src/map/gen/Lithology.cpp
    src/map/gen/CoastalLandforms.cpp
    src/map/gen/InsolationSlope.cpp
    src/map/gen/StreamRiparian.cpp
    src/map/gen/NppCarryingCapacity.cpp
    src/map/gen/DrainageLivestock.cpp
    src/map/gen/EcoAnalytics.cpp
    src/map/gen/Thresholds.cpp
    src/map/gen/Noise.cpp
    src/map/gen/Resources.cpp
    src/map/gen/Features.cpp
    src/map/gen/Rivers.cpp
    src/map/gen/SphereField.cpp
    src/map/gen/SphereFieldPhysics.cpp
    src/map/gen/SphereGeometry.cpp
    src/map/Pathfinding.cpp
    src/map/FogOfWar.cpp
    src/map/RiverGameplay.cpp

    # Simulation
    src/simulation/turn/TurnManager.cpp
    src/simulation/turn/TurnProcessor.cpp
    src/simulation/unit/Movement.cpp
    src/simulation/unit/Combat.cpp
    src/simulation/unit/Naval.cpp
    src/simulation/unit/UnitUpgrade.cpp
    src/simulation/tech/TechTree.cpp
    src/simulation/tech/CivicTree.cpp
    src/simulation/tech/EurekaBoost.cpp
    src/simulation/tech/TechGating.cpp
    src/simulation/tech/EraScore.cpp
    src/simulation/city/CityGrowth.cpp
    src/simulation/city/CityScience.cpp
    src/simulation/city/Happiness.cpp
    src/simulation/city/ProductionSystem.cpp
    src/simulation/city/BorderExpansion.cpp
    src/simulation/city/CityBombardment.cpp
    src/simulation/culture/Tourism.cpp
    src/simulation/map/Improvement.cpp
    src/simulation/map/Infrastructure.cpp
    src/simulation/map/TerrainModification.cpp
    src/simulation/map/Chokepoint.cpp
    src/simulation/map/GoodyHuts.cpp
    src/simulation/unit/SupplyLines.cpp
    src/simulation/unit/ZoneOfControl.cpp
    src/simulation/unit/Promotion.cpp
    src/simulation/event/WorldEvents.cpp
    src/simulation/event/VisibilityEvents.cpp
    src/simulation/event/GameNotifications.cpp
    src/simulation/barbarian/BarbarianController.cpp
    src/simulation/barbarian/BarbarianClans.cpp
    src/simulation/climate/NaturalDisasters.cpp
    src/simulation/diplomacy/BorderViolation.cpp
    src/simulation/diplomacy/DiplomacyState.cpp
    src/simulation/diplomacy/EspionageSystem.cpp
    src/simulation/diplomacy/WarWeariness.cpp
    src/simulation/diplomacy/Grievance.cpp
    src/simulation/diplomacy/DiplomaticFavor.cpp
    src/simulation/diplomacy/WorldCongress.cpp
    src/simulation/diplomacy/DealTerms.cpp
    src/simulation/diplomacy/NavalPassage.cpp
    src/simulation/diplomacy/AllianceObligations.cpp
    src/simulation/climate/Climate.cpp
    src/simulation/victory/VictoryCondition.cpp
    src/simulation/victory/Prestige.cpp
    src/simulation/victory/SpaceRace.cpp
    src/simulation/government/Government.cpp
    src/simulation/ai/AIController.cpp
    src/simulation/ai/AIDiplomacyController.cpp
    src/simulation/ai/AIGovernmentController.cpp
    src/simulation/ai/AITradeRoutesController.cpp
    src/simulation/ai/AIResearchPlanner.cpp
    src/simulation/ai/AISettlerController.cpp
    src/simulation/ai/AIBuilderController.cpp
    src/simulation/ai/AIMilitaryController.cpp
    src/simulation/ai/AIEconomicStrategy.cpp
    src/simulation/ai/LeaderPersonality.cpp
    src/simulation/ai/BehaviorTree.cpp
    src/simulation/ai/UtilityScoring.cpp
    src/simulation/ai/UtilityAI.cpp
    src/simulation/ai/AIAdvisors.cpp
    src/simulation/ai/AIEventChoice.cpp
    src/simulation/ai/AIInvestmentController.cpp
    src/simulation/ai/AIFuturesTrading.cpp
    src/simulation/ai/AICommodityHoarding.cpp
    src/simulation/resource/ResourceTypes.cpp
    src/simulation/resource/ProductionChain.cpp
    src/simulation/resource/EconomySimulation.cpp
    src/simulation/economy/Market.cpp
    src/simulation/economy/ComparativeAdvantage.cpp
    src/simulation/economy/ResourceCurse.cpp
    src/simulation/monetary/Inflation.cpp
    src/simulation/monetary/CentralBank.cpp
    src/simulation/monetary/FiscalPolicy.cpp
    src/simulation/monetary/CurrencyTrust.cpp
    src/simulation/monetary/CurrencyCrisis.cpp
    src/simulation/monetary/CurrencyWar.cpp
    src/simulation/monetary/Bonds.cpp
    src/simulation/monetary/ForexMarket.cpp
    src/simulation/greatpeople/GreatPeople.cpp
    src/simulation/wonder/Wonder.cpp
    src/simulation/citystate/CityState.cpp
    src/simulation/religion/Religion.cpp
    src/simulation/economy/Maintenance.cpp
    src/simulation/economy/AdvancedEconomics.cpp
    src/simulation/economy/EnvironmentModifier.cpp
    src/simulation/economy/InternalTrade.cpp
    src/simulation/economy/Sanctions.cpp
    src/simulation/economy/Speculation.cpp
    src/simulation/economy/ColonialEconomics.cpp
    src/simulation/economy/IndustrialRevolution.cpp
    src/simulation/economy/EconomicDepth.cpp
    src/simulation/economy/CommodityExchange.cpp
    src/simulation/economy/NavalTrade.cpp
    src/simulation/economy/EnergyDependency.cpp
    src/simulation/economy/StockMarket.cpp
    src/simulation/economy/TradeAgreement.cpp
    src/simulation/economy/SupplyChain.cpp
    src/simulation/economy/TradeRouteSystem.cpp
    src/simulation/economy/DomesticCourier.cpp
    src/simulation/economy/SpeculationBubble.cpp
    src/simulation/economy/MonopolyPricing.cpp
    src/simulation/economy/TechUnemployment.cpp
    src/simulation/economy/BlackMarket.cpp
    src/simulation/economy/HumanCapital.cpp
    src/simulation/city/Governor.cpp
    src/simulation/automation/Automation.cpp
    src/simulation/empire/CommunicationSpeed.cpp
    src/simulation/city/CityConnection.cpp

    # Production systems
    src/simulation/production/QualityTier.cpp
    src/simulation/production/Waste.cpp
    src/simulation/production/PowerGrid.cpp
    src/simulation/production/Automation.cpp
    src/simulation/city/CityLoyalty.cpp
    src/simulation/city/Secession.cpp
    src/balance/BalanceParams.cpp
    src/simulation/city/DistrictAdjacency.cpp
    src/simulation/unit/CombatExtensions.cpp
    src/simulation/religion/TheologicalCombat.cpp
    src/simulation/diplomacy/DiplomacyExtensions.cpp
    src/simulation/greatpeople/GreatPeopleExpanded.cpp
    src/simulation/tech/CivicEffects.cpp

    # Replay
    src/replay/ReplayRecorder.cpp

    # Mod
    src/mod/ModLoader.cpp

    # Save
    src/save/Serializer.cpp

    # UI
    src/ui/BitmapFont.cpp
    src/ui/UIManager.cpp
    src/ui/GameScreens.cpp
    src/ui/CityDetailTabs.cpp
    src/ui/TradeScreen.cpp
    src/ui/TradeRouteSetupScreen.cpp
    src/ui/DiplomacyScreen.cpp
    src/ui/EventLog.cpp
    src/ui/LayoutBuilder.cpp
    src/ui/WidgetInspector.cpp
    src/ui/LoadingScreen.cpp
    src/ui/IconAtlas.cpp
    src/ui/UITestHarness.cpp
    src/ui/Localization.cpp
    src/ui/UIPersistence.cpp
    src/ui/Tooltip.cpp
    src/ui/MainMenu.cpp
    src/ui/ReligionScreen.cpp
    src/ui/ScoreScreen.cpp
    src/ui/ScreenRegistry.cpp
    src/ui/SettingsMenu.cpp
    src/ui/Theme.cpp
    src/ui/Notifications.cpp
    src/ui/MapEditor.cpp
    src/ui/Tutorial.cpp
    src/ui/Encyclopedia.cpp
    src/ui/DebugConsole.cpp
    src/ui/SpectatorHUD.cpp
    src/ui/PauseMenu.cpp

    # Networking / Server-Client
    src/net/Transport.cpp
    src/net/GameServer.cpp
    src/net/GameDBus.cpp

    # App
    src/app/Window.cpp
    src/app/InputManager.cpp
    src/app/UnitSelection.cpp
    src/app/HotkeyManager.cpp
    src/app/Application.cpp
    src/app/DebugCommandFile.cpp
    src/debug/DebugServer.cpp
    src/app/Application_HUD.cpp
    src/app/ScreenshotEncoder.cpp

    # Scripting
    src/scripting/LuaEngine.cpp

    # Audio
    src/audio/AudioBackend.cpp

    # Render
    src/render/CameraController.cpp
    src/render/MapOverlays.cpp
    src/render/DrawCommandBuffer.cpp
    src/render/GameRenderer.cpp
    src/render/GlobeRenderer.cpp
    src/render/MapRenderer.cpp
    src/render/UnitRenderer.cpp
    src/render/Minimap.cpp
    src/render/CombatAnimation.cpp
    src/render/Particles.cpp
    src/render/TextureAtlas.cpp
    src/render/SpriteRenderer.cpp
)

# Strip gfx-only sources in headless mode.
if(AOC_HEADLESS)
    list(LENGTH AOC_GFX_ONLY_SOURCES _gfx_count)
    foreach(_gfx_src IN LISTS AOC_GFX_ONLY_SOURCES)
        list(REMOVE_ITEM AOC_LIB_SOURCES "${_gfx_src}")
    endforeach()
    message(STATUS "Headless build: Vulkan/GLFW and ${_gfx_count} gfx sources excluded")
endif()

add_library(aoc_lib STATIC ${AOC_LIB_SOURCES})

# cpp-httplib worker threads. Audit WP7: define centrally so DebugServer.cpp
# does not need a per-source `#define`. cpp-httplib defaults to 8/hwconc;
# 4 is enough for the debug API surface (concurrent /sim/step + /tile reads).
target_compile_definitions(aoc_lib PRIVATE CPPHTTPLIB_THREAD_POOL_COUNT=4)

target_include_directories(aoc_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# Precompiled header: only ubiquitous, stable standard-library headers
# (each included by dozens-to-hundreds of TUs; see the include census).
# PRIVATE so consumers of aoc_lib are not forced to share it. Deliberately
# excludes project headers -- those change often and would defeat the cache.
option(AOC_ENABLE_PCH "Precompile common stdlib headers for aoc_lib" ON)
if(AOC_ENABLE_PCH)
    target_precompile_headers(aoc_lib PRIVATE
        <cstdint>
        <vector>
        <string>
        <string_view>
        <array>
        <algorithm>
        <cmath>
        <memory>
        <unordered_map>
        <utility>
    )
endif()

if(NOT AOC_HEADLESS)
    target_link_libraries(aoc_lib
        PUBLIC
            vulkan_renderer_lib
            glfw
    )
    # macOS: MoltenVK sits on top of Metal/QuartzCore/Cocoa/IOKit.
    # These frameworks are needed at link time for any binary that ends up
    # touching the Vulkan loader.
    if(AOC_PLATFORM_MACOS)
        target_link_libraries(aoc_lib PUBLIC ${AOC_MACOS_FRAMEWORKS})
    endif()
else()
    target_compile_definitions(aoc_lib PUBLIC AOC_HEADLESS_BUILD)
endif()

# --------------------------------------------------------------------------
# Platform system libraries needed by aoc_lib's own code
# --------------------------------------------------------------------------
# Linux/macOS: dlopen/dlsym for Lua and runtime shader compiler loading
# (the vulkan_utils submodule already pulls ${CMAKE_DL_LIBS} for itself, but
# our own translation units may also need it in the future).
#
# Windows: link ws2_32/iphlpapi defensively so the net/ stub compiles now
# and real socket code added later doesn't require a second CMake change.
if(AOC_PLATFORM_WINDOWS)
    target_link_libraries(aoc_lib PUBLIC ws2_32 iphlpapi)
elseif(AOC_PLATFORM_LINUX)
    target_link_libraries(aoc_lib PUBLIC ${CMAKE_DL_LIBS})
endif()

# std::thread, std::jthread, std::counting_semaphore
find_package(Threads REQUIRED)
target_link_libraries(aoc_lib PUBLIC Threads::Threads)

# OpenMP — used for parallel per-tile passes in MapGenerator post-pass.
# Optional: builds without OpenMP fall back to serial loops via the
# AOC_HAS_OPENMP guard in the generator.
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(aoc_lib PUBLIC OpenMP::OpenMP_CXX)
    target_compile_definitions(aoc_lib PUBLIC AOC_HAS_OPENMP=1)
    message(STATUS "OpenMP enabled for parallel map generation")
endif()

target_compile_features(aoc_lib PUBLIC cxx_std_20)

# LuaJIT scripting (preferred, 10-50x faster), falls back to Lua 5.4
find_package(PkgConfig QUIET)

# Try LuaJIT first
set(LUA_IMPL_FOUND FALSE)
if(PkgConfig_FOUND)
    pkg_check_modules(LUAJIT QUIET luajit)
    if(LUAJIT_FOUND)
        set(LUA_IMPL_FOUND TRUE)
        set(LUA_IMPL_LIBS ${LUAJIT_LIBRARIES})
        set(LUA_IMPL_DIRS ${LUAJIT_INCLUDE_DIRS})
        set(LUA_IMPL_NAME "LuaJIT")
    endif()
endif()
if(NOT LUA_IMPL_FOUND)
    find_library(LUAJIT_LIB NAMES luajit-5.1 luajit)
    find_path(LUAJIT_INC NAMES lua.h PATH_SUFFIXES luajit-2.1 luajit)
    if(LUAJIT_LIB AND LUAJIT_INC)
        set(LUA_IMPL_FOUND TRUE)
        set(LUA_IMPL_LIBS ${LUAJIT_LIB})
        set(LUA_IMPL_DIRS ${LUAJIT_INC})
        set(LUA_IMPL_NAME "LuaJIT")
    endif()
endif()

# Fall back to Lua 5.4
if(NOT LUA_IMPL_FOUND)
    if(PkgConfig_FOUND)
        pkg_check_modules(LUA54 QUIET lua5.4 lua lua-5.4)
        if(LUA54_FOUND)
            set(LUA_IMPL_FOUND TRUE)
            set(LUA_IMPL_LIBS ${LUA54_LIBRARIES})
            set(LUA_IMPL_DIRS ${LUA54_INCLUDE_DIRS})
            set(LUA_IMPL_NAME "Lua 5.4")
        endif()
    endif()
endif()
if(NOT LUA_IMPL_FOUND)
    find_library(LUA54_LIB NAMES lua5.4 lua lua54 REQUIRED)
    find_path(LUA54_INC NAMES lua.h PATH_SUFFIXES lua5.4 lua REQUIRED)
    set(LUA_IMPL_FOUND TRUE)
    set(LUA_IMPL_LIBS ${LUA54_LIB})
    set(LUA_IMPL_DIRS ${LUA54_INC})
    set(LUA_IMPL_NAME "Lua 5.4 (fallback)")
endif()

target_compile_definitions(aoc_lib PUBLIC AOC_HAS_LUA)
target_include_directories(aoc_lib PUBLIC ${LUA_IMPL_DIRS})
target_link_libraries(aoc_lib PUBLIC ${LUA_IMPL_LIBS})
message(STATUS "Scripting: ${LUA_IMPL_NAME} (${LUA_IMPL_LIBS})")

# sd-bus (libsystemd) -- optional DBus service for screenshot/remote control.
# Only enabled on Linux where libsystemd is present. On Windows/macOS the
# GameDBus class compiles to a no-op shim.
if(UNIX AND NOT APPLE AND PkgConfig_FOUND)
    pkg_check_modules(SDBUS QUIET libsystemd)
    if(SDBUS_FOUND)
        target_compile_definitions(aoc_lib PUBLIC AOC_HAS_SDBUS)
        target_include_directories(aoc_lib PUBLIC ${SDBUS_INCLUDE_DIRS})
        target_link_libraries(aoc_lib PUBLIC ${SDBUS_LIBRARIES})
        message(STATUS "DBus service: libsystemd ${SDBUS_VERSION}")
    else()
        message(STATUS "DBus service: libsystemd not found, disabled")
    endif()
endif()

# WP-F: profile-guided optimization. Two-stage GCC workflow.
#   -DAOC_PGO_PHASE=off      (default) — normal build.
#   -DAOC_PGO_PHASE=generate — instrument. Run a training sim after.
#   -DAOC_PGO_PHASE=use      — rebuild using the collected profiles.
# Only applied for Clang/GNU; MSVC ignores silently.
set(AOC_PGO_PHASE "off" CACHE STRING "PGO phase: off | generate | use")
set_property(CACHE AOC_PGO_PHASE PROPERTY STRINGS off generate use)
set(AOC_PGO_PROFILE_DIR "${CMAKE_BINARY_DIR}/pgo_profiles"
    CACHE PATH "Directory where PGO training profiles live")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    if(AOC_PGO_PHASE STREQUAL "generate")
        file(MAKE_DIRECTORY "${AOC_PGO_PROFILE_DIR}")
        add_compile_options(-fprofile-generate=${AOC_PGO_PROFILE_DIR})
        add_link_options(-fprofile-generate=${AOC_PGO_PROFILE_DIR})
        message(STATUS "PGO: instrumented build, profiles -> ${AOC_PGO_PROFILE_DIR}")
    elseif(AOC_PGO_PHASE STREQUAL "use")
        add_compile_options(
            -fprofile-use=${AOC_PGO_PROFILE_DIR}
            -fprofile-correction
        )
        add_link_options(-fprofile-use=${AOC_PGO_PROFILE_DIR})
        message(STATUS "PGO: optimized build, using profiles from ${AOC_PGO_PROFILE_DIR}")
    endif()
endif()

# Compiler warnings + SIMD autovectorization for Release builds.
# -ftree-vectorize (default at -O2/-O3) lets the compiler auto-SIMD the
# tight per-tile post-pass loops in MapGenerator. -march=native enables
# AVX2/AVX-512 on the build host so noise sampling + Voronoi distance
# loops use wide vector registers. Skip in Debug builds for visibility.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(aoc_lib PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        -Wnon-virtual-dtor -Woverloaded-virtual
        -Wnull-dereference -Wdouble-promotion
        -Wformat=2
        -pipe
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:RelWithDebInfo>:-O3>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:RelWithDebInfo>:-march=native>
        $<$<CONFIG:Release>:-ftree-vectorize>
        $<$<CONFIG:RelWithDebInfo>:-ftree-vectorize>
    )
elseif(MSVC)
    # /W4 is the practical max; /Wall floods with noise from system headers.
    # /permissive- enforces standard-conforming C++ (two-phase name lookup etc).
    # /wd4244 /wd4267: narrowing warnings that map to -Wconversion, already
    #     reported by Clang/GCC builds; avoid double-noise on MSVC dev boxes.
    target_compile_options(aoc_lib PRIVATE /W4 /permissive- /wd4244 /wd4267)
endif()

# --------------------------------------------------------------------------
# Shader toolchain (glslangValidator / glslc) -- informational check
# --------------------------------------------------------------------------
# The renderer compiles GLSL -> SPIR-V at runtime via libshaderc
# (vulkan_utils/ShaderBuilder.cpp loads it via dlopen/LoadLibrary). No build-
# time shader compilation is wired up, so we only emit a status message if
# the SDK-provided CLI tools are visible. Find_program looks on PATH, which
# the LunarG SDK installers populate on all three platforms.
find_program(AOC_GLSLANG_VALIDATOR glslangValidator)
find_program(AOC_GLSLC glslc)
if(AOC_GLSLANG_VALIDATOR)
    message(STATUS "Found glslangValidator: ${AOC_GLSLANG_VALIDATOR}")
endif()
if(AOC_GLSLC)
    message(STATUS "Found glslc: ${AOC_GLSLC}")
endif()

# --------------------------------------------------------------------------
# Main executable (interactive; requires Vulkan + GLFW)
# --------------------------------------------------------------------------

if(NOT AOC_HEADLESS)
add_executable(age_of_civ src/main.cpp)
target_link_libraries(age_of_civ PRIVATE aoc_lib)

# Copy GLSL shader sources to the build directory for runtime compilation.
# Also keep the symlink as a fallback for pre-compiled .spv files.
set(AOC_SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/vulkan_renderer/shaders")
set(AOC_SHADER_OUTPUT_DIR "$<TARGET_FILE_DIR:age_of_civ>/shaders")

add_custom_command(TARGET age_of_civ POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${AOC_SHADER_OUTPUT_DIR}
    COMMENT "Creating shader output directory"
)

# Copy all .glsl source files so runtime compilation works from the build dir
file(GLOB AOC_GLSL_SOURCES "${AOC_SHADER_SOURCE_DIR}/*.glsl")
foreach(GLSL_FILE ${AOC_GLSL_SOURCES})
    get_filename_component(GLSL_FILENAME "${GLSL_FILE}" NAME)
    add_custom_command(TARGET age_of_civ POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GLSL_FILE}"
            "${AOC_SHADER_OUTPUT_DIR}/${GLSL_FILENAME}"
        COMMENT "Copying shader source: ${GLSL_FILENAME}"
    )
endforeach()

# Copy any pre-compiled .spv files as fallback
file(GLOB AOC_SPV_FILES "${AOC_SHADER_SOURCE_DIR}/*.spv")
foreach(SPV_FILE ${AOC_SPV_FILES})
    get_filename_component(SPV_FILENAME "${SPV_FILE}" NAME)
    add_custom_command(TARGET age_of_civ POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SPV_FILE}"
            "${AOC_SHADER_OUTPUT_DIR}/${SPV_FILENAME}"
        COMMENT "Copying pre-compiled shader: ${SPV_FILENAME}"
    )
endforeach()

# Copy game-specific GLSL shader sources (sprite shaders, etc.)
set(AOC_GAME_SHADER_DIR "${CMAKE_SOURCE_DIR}/shaders")
file(GLOB AOC_GAME_GLSL_SOURCES "${AOC_GAME_SHADER_DIR}/*.glsl")
foreach(GLSL_FILE ${AOC_GAME_GLSL_SOURCES})
    get_filename_component(GLSL_FILENAME "${GLSL_FILE}" NAME)
    add_custom_command(TARGET age_of_civ POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GLSL_FILE}"
            "${AOC_SHADER_OUTPUT_DIR}/${GLSL_FILENAME}"
        COMMENT "Copying game shader source: ${GLSL_FILENAME}"
    )
endforeach()

# Copy game data files (JSON definitions) to the build directory
set(AOC_DATA_SOURCE_DIR "${CMAKE_SOURCE_DIR}/data")
set(AOC_DATA_OUTPUT_DIR "$<TARGET_FILE_DIR:age_of_civ>/data")

add_custom_command(TARGET age_of_civ POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${AOC_DATA_OUTPUT_DIR}/definitions"
    COMMENT "Creating data output directory"
)

file(GLOB AOC_DATA_JSON_FILES "${AOC_DATA_SOURCE_DIR}/definitions/*.json")
foreach(JSON_FILE ${AOC_DATA_JSON_FILES})
    get_filename_component(JSON_FILENAME "${JSON_FILE}" NAME)
    add_custom_command(TARGET age_of_civ POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${JSON_FILE}"
            "${AOC_DATA_OUTPUT_DIR}/definitions/${JSON_FILENAME}"
        COMMENT "Copying data file: ${JSON_FILENAME}"
    )
endforeach()

# Also copy YAML config files
file(GLOB AOC_DATA_YAML_FILES "${AOC_DATA_SOURCE_DIR}/*.yaml")
foreach(YAML_FILE ${AOC_DATA_YAML_FILES})
    get_filename_component(YAML_FILENAME "${YAML_FILE}" NAME)
    add_custom_command(TARGET age_of_civ POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${YAML_FILE}"
            "${AOC_DATA_OUTPUT_DIR}/${YAML_FILENAME}"
        COMMENT "Copying config file: ${YAML_FILENAME}"
    )
endforeach()

endif() # NOT AOC_HEADLESS

# --------------------------------------------------------------------------
# Headless simulation tool (AI-vs-AI for analysis)
# --------------------------------------------------------------------------

add_executable(aoc_simulate src/tools/HeadlessSimulation.cpp)
target_link_libraries(aoc_simulate PRIVATE aoc_lib)

# Provide a main() for the headless tool
target_compile_definitions(aoc_simulate PRIVATE AOC_HEADLESS_MAIN)

# --------------------------------------------------------------------------
# Decision-log dump tool (binary AOCL trace -> CSV)
# --------------------------------------------------------------------------

add_executable(aoc_trace_dump src/tools/TraceDump.cpp)
target_link_libraries(aoc_trace_dump PRIVATE aoc_lib)

# --------------------------------------------------------------------------
# Standalone map generator (Continents pipeline only). Writes ASCII / CSV
# dumps for offline review without launching the full simulation. 2026-05-03.
# --------------------------------------------------------------------------

add_executable(aoc_mapgen
    src/tools/MapGenCli.cpp
    src/debug/DebugServer.cpp
)
target_link_libraries(aoc_mapgen PRIVATE aoc_lib)
target_include_directories(aoc_mapgen PRIVATE third_party)
# Match aoc_lib: cpp-httplib pool size — see comment above on aoc_lib.
target_compile_definitions(aoc_mapgen PRIVATE CPPHTTPLIB_THREAD_POOL_COUNT=4)

# --------------------------------------------------------------------------
# C++ Genetic Algorithm for Utility AI optimization
# --------------------------------------------------------------------------

add_subdirectory(ml/cpp)

# --------------------------------------------------------------------------
# Tests (optional)
# --------------------------------------------------------------------------

option(AOC_BUILD_TESTS "Build unit tests" ON)
if(AOC_BUILD_TESTS)
    enable_testing()

    set(AOC_SMOKE_TESTS
        test_unemployment
        test_building_capacity
        test_production_efficiency
        test_json_parser_depth
        test_save_roundtrip
        test_combat_kills
        test_economy_invariants
        test_path_guard
        test_save_hostile
        test_lua_sandbox
        test_behavior_tree
        test_deal_atomicity
        test_diplomatic_favor
        test_iou_term_expiry
        test_save_finite
        test_decision_log_io
    )

    # The debug preset builds with ASan/LSan; known suppressions live in
    # asan_suppressions.txt (same file run.sh uses).
    set(AOC_TEST_ENV
        "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/asan_suppressions.txt")

    foreach(aoc_test ${AOC_SMOKE_TESTS})
        add_executable(${aoc_test} tests/${aoc_test}.cpp)
        target_link_libraries(${aoc_test} PRIVATE aoc_lib)
        # doctest: header-only; CHECK() stays live under -DNDEBUG, unlike
        # assert(), so release-preset test runs actually assert.
        target_include_directories(${aoc_test} PRIVATE third_party/doctest)
        add_test(NAME ${aoc_test} COMMAND ${aoc_test})
        set_tests_properties(${aoc_test} PROPERTIES ENVIRONMENT "${AOC_TEST_ENV}")
    endforeach()

    # Known-good save corpus lives in-tree; regenerate via AOC_WRITE_CORPUS
    # after a deliberate format bump.
    target_compile_definitions(test_save_roundtrip PRIVATE
        AOC_TEST_CORPUS_DIR="${CMAKE_SOURCE_DIR}/tests/data/saves")

    # Turn-loop smoke: drive aoc_simulate for a handful of turns. Non-zero
    # exit means the headless loop crashed or asserted.
    if(TARGET aoc_simulate)
        add_test(NAME test_turn_loop_smoke
                 COMMAND aoc_simulate --turns 5 --players 2 --output ${CMAKE_BINARY_DIR}/smoke.csv)
        set_tests_properties(test_turn_loop_smoke PROPERTIES ENVIRONMENT "${AOC_TEST_ENV}")
    endif()

    # Sim behavior gates -- release builds only (under ASan these multi-run
    # sims take many minutes and add no memory-safety value beyond the smoke).
    #   test_determinism : same seed twice -> byte-identical CSVs. Never
    #                      re-blessed; red always means hidden entropy.
    #   test_golden_sim  : seed-42 500-turn run hashes to the blessed value
    #                      in tests/golden/. Behavior-changing commits
    #                      re-bless deliberately (script --bless) and say
    #                      why. Excludable during a fix series via
    #                      `ctest -LE golden`.
    if(TARGET aoc_simulate AND CMAKE_BUILD_TYPE STREQUAL "Release")
        add_test(NAME test_determinism
                 COMMAND ${CMAKE_SOURCE_DIR}/scripts/test_determinism.sh
                         $<TARGET_FILE:aoc_simulate> ${CMAKE_BINARY_DIR}
                 WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
        add_test(NAME test_golden_sim
                 COMMAND ${CMAKE_SOURCE_DIR}/scripts/test_golden_sim.sh
                         $<TARGET_FILE:aoc_simulate> ${CMAKE_BINARY_DIR}
                         ${CMAKE_SOURCE_DIR}/tests/golden/seed42_turns500.sha256
                 WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
        set_tests_properties(test_golden_sim PROPERTIES LABELS "golden")
    endif()
endif()

# --------------------------------------------------------------------------
# Fuzzers (opt-in, local; NOT part of CI or the normal build)
# --------------------------------------------------------------------------
# Requires a Clang toolchain with the libFuzzer runtime. Build with:
#   cmake --preset debug -DAOC_BUILD_FUZZERS=ON && cmake --build --preset debug
# Run (seeded from the in-tree corpus):
#   ./build/debug/fuzz_save_loader -max_len=65536 tests/data/saves
option(AOC_BUILD_FUZZERS "Build libFuzzer harnesses (Clang only)" OFF)
if(AOC_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "AOC_BUILD_FUZZERS requires Clang (libFuzzer).")
    endif()
    add_executable(fuzz_save_loader tests/fuzz/fuzz_save_loader.cpp)
    target_link_libraries(fuzz_save_loader PRIVATE aoc_lib)
    target_compile_options(fuzz_save_loader PRIVATE
        -fsanitize=fuzzer,address,undefined)
    target_link_options(fuzz_save_loader PRIVATE
        -fsanitize=fuzzer,address,undefined)
endif()
