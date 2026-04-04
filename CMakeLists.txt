cmake_minimum_required(VERSION 3.20)
project(AgeOfCiv VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use ccache if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
endif()

# --------------------------------------------------------------------------
# Dependencies
# --------------------------------------------------------------------------

# Vulkan renderer submodule (builds as C++17 internally, that is fine)
add_subdirectory(third_party/vulkan_renderer)

# GLFW -- required for windowing (vulkan_utils links it optionally;
# we need it unconditionally)
find_package(glfw3 REQUIRED)

# --------------------------------------------------------------------------
# Game library  (everything except main.cpp)
# --------------------------------------------------------------------------

add_library(aoc_lib STATIC
    # Core
    src/core/ErrorCodes.cpp

    # ECS
    src/ecs/World.cpp

    # Map
    src/map/HexGrid.cpp
    src/map/MapGenerator.cpp
    src/map/Pathfinding.cpp
    src/map/FogOfWar.cpp

    # Simulation
    src/simulation/turn/TurnManager.cpp
    src/simulation/unit/Movement.cpp
    src/simulation/unit/Combat.cpp
    src/simulation/tech/TechTree.cpp
    src/simulation/tech/CivicTree.cpp
    src/simulation/city/CityGrowth.cpp
    src/simulation/city/Happiness.cpp
    src/simulation/diplomacy/DiplomacyState.cpp
    src/simulation/ai/AIController.cpp
    src/simulation/resource/ResourceTypes.cpp
    src/simulation/resource/ProductionChain.cpp
    src/simulation/resource/EconomySimulation.cpp
    src/simulation/economy/Market.cpp
    src/simulation/economy/ComparativeAdvantage.cpp
    src/simulation/economy/ResourceCurse.cpp
    src/simulation/monetary/Inflation.cpp
    src/simulation/monetary/CentralBank.cpp
    src/simulation/monetary/FiscalPolicy.cpp

    # Save
    src/save/Serializer.cpp

    # UI
    src/ui/BitmapFont.cpp
    src/ui/UIManager.cpp

    # App
    src/app/Window.cpp
    src/app/InputManager.cpp
    src/app/Application.cpp

    # Render
    src/render/CameraController.cpp
    src/render/GameRenderer.cpp
    src/render/MapRenderer.cpp
    src/render/UnitRenderer.cpp
)

target_include_directories(aoc_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(aoc_lib
    PUBLIC
        vulkan_renderer_lib
        glfw
)

target_compile_features(aoc_lib PUBLIC cxx_std_20)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(aoc_lib PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        -Wnon-virtual-dtor -Woverloaded-virtual
        -Wnull-dereference -Wdouble-promotion
        -Wformat=2
        -pipe
    )
elseif(MSVC)
    target_compile_options(aoc_lib PRIVATE /W4 /permissive-)
endif()

# --------------------------------------------------------------------------
# Main executable
# --------------------------------------------------------------------------

add_executable(age_of_civ src/main.cpp)
target_link_libraries(age_of_civ PRIVATE aoc_lib)

# --------------------------------------------------------------------------
# Tests (optional)
# --------------------------------------------------------------------------

option(AOC_BUILD_TESTS "Build unit tests" ON)
if(AOC_BUILD_TESTS)
    enable_testing()
    # Tests will be added here as modules are implemented
endif()
