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
    src/core/Selectable.cpp

    # Data loading
    src/data/DataLoader.cpp

    # ECS
    src/ecs/World.cpp

    # Map
    src/map/HexGrid.cpp
    src/map/MapGenerator.cpp
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
    src/simulation/map/Improvement.cpp
    src/simulation/map/Infrastructure.cpp
    src/simulation/map/TerrainModification.cpp
    src/simulation/unit/SupplyLines.cpp
    src/simulation/event/WorldEvents.cpp
    src/simulation/barbarian/BarbarianController.cpp
    src/simulation/barbarian/BarbarianClans.cpp
    src/simulation/climate/NaturalDisasters.cpp
    src/simulation/diplomacy/DiplomacyState.cpp
    src/simulation/diplomacy/EspionageSystem.cpp
    src/simulation/diplomacy/WarWeariness.cpp
    src/simulation/diplomacy/Grievance.cpp
    src/simulation/diplomacy/WorldCongress.cpp
    src/simulation/diplomacy/DealTerms.cpp
    src/simulation/climate/Climate.cpp
    src/simulation/victory/VictoryCondition.cpp
    src/simulation/government/Government.cpp
    src/simulation/ai/AIController.cpp
    src/simulation/ai/AIResearchPlanner.cpp
    src/simulation/ai/AISettlerController.cpp
    src/simulation/ai/AIBuilderController.cpp
    src/simulation/ai/AIMilitaryController.cpp
    src/simulation/ai/AIEconomicStrategy.cpp
    src/simulation/ai/LeaderPersonality.cpp
    src/simulation/ai/BehaviorTree.cpp
    src/simulation/ai/UtilityScoring.cpp
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
    src/simulation/economy/NavalTrade.cpp
    src/simulation/economy/EnergyDependency.cpp
    src/simulation/economy/StockMarket.cpp
    src/simulation/economy/WageDemands.cpp
    src/simulation/economy/EconomicEspionage.cpp
    src/simulation/economy/TradeAgreement.cpp
    src/simulation/economy/SupplyChain.cpp
    src/simulation/economy/TradeRouteSystem.cpp
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
    src/simulation/city/DistrictAdjacency.cpp
    src/simulation/unit/CombatExtensions.cpp
    src/simulation/religion/TheologicalCombat.cpp
    src/simulation/citystate/CityStateQuest.cpp
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
    src/ui/TradeScreen.cpp
    src/ui/DiplomacyScreen.cpp
    src/ui/EventLog.cpp
    src/ui/Tooltip.cpp
    src/ui/MainMenu.cpp
    src/ui/ReligionScreen.cpp
    src/ui/ScoreScreen.cpp
    src/ui/Notifications.cpp
    src/ui/MapEditor.cpp
    src/ui/Tutorial.cpp
    src/ui/Encyclopedia.cpp
    src/ui/GameNotifications.cpp

    # Networking / Server-Client
    src/net/Transport.cpp
    src/net/GameServer.cpp

    # App
    src/app/Window.cpp
    src/app/InputManager.cpp
    src/app/UnitSelection.cpp
    src/app/HotkeyManager.cpp
    src/app/Application.cpp

    # Scripting
    src/scripting/LuaEngine.cpp

    # Audio
    src/audio/AudioBackend.cpp

    # Render
    src/render/CameraController.cpp
    src/render/MapOverlays.cpp
    src/render/DrawCommandBuffer.cpp
    src/render/GameRenderer.cpp
    src/render/MapRenderer.cpp
    src/render/UnitRenderer.cpp
    src/render/Minimap.cpp
    src/render/CombatAnimation.cpp
    src/render/Particles.cpp
)

target_include_directories(aoc_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(aoc_lib
    PUBLIC
        vulkan_renderer_lib
        glfw
)

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

# --------------------------------------------------------------------------
# Headless simulation tool (AI-vs-AI for analysis)
# --------------------------------------------------------------------------

add_executable(aoc_simulate src/tools/HeadlessSimulation.cpp)
target_link_libraries(aoc_simulate PRIVATE aoc_lib)

# Provide a main() for the headless tool
target_compile_definitions(aoc_simulate PRIVATE AOC_HEADLESS_MAIN)

# --------------------------------------------------------------------------
# Tests (optional)
# --------------------------------------------------------------------------

option(AOC_BUILD_TESTS "Build unit tests" ON)
if(AOC_BUILD_TESTS)
    enable_testing()
    # Tests will be added here as modules are implemented
endif()
