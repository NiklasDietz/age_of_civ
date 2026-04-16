#pragma once

/**
 * @file TerrainModification.hpp
 * @brief Terrain modification projects: canals, tunnels, land reclamation.
 *
 * === Canals ===
 * Connect two bodies of water through land tiles.
 * Requirements: tile must be between two water/coast/canal tiles (or a city).
 * Effect: naval units and trade ships can pass through. Owner collects tolls.
 * Cost: 600 production, requires Steam Power tech. Multi-tile canals supported.
 *
 * === Tunnels ===
 * Connect two tiles separated by a mountain.
 * Requirements: both endpoints must be land tiles adjacent to the same mountain.
 * Effect: units can move through the mountain as if it were flat terrain.
 * Cost: 300 production, requires Computers tech.
 *
 * === Land Reclamation ===
 * Convert a shallow coast tile to land.
 * Requirements: must be adjacent to existing land.
 * Effect: creates a plains tile where coast used to be.
 * Cost: 250 production, requires Industrial tech.
 * Risk: reclaimed land is vulnerable to flooding/tsunami.
 *
 * === Deforestation / Reforestation ===
 * Remove or plant forests as deliberate strategic choice.
 * Deforestation: instant +20 production, removes forest feature, +CO2.
 * Reforestation: costs 50 production, adds forest in 5 turns, -CO2.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>

namespace aoc::map { class HexGrid; }

namespace aoc::sim {

enum class TerrainProjectType : uint8_t {
    Canal,
    Tunnel,
    LandReclamation,
    Deforestation,
    Reforestation,

    Count
};

/// Production cost for each terrain project.
[[nodiscard]] constexpr int32_t terrainProjectCost(TerrainProjectType type) {
    switch (type) {
        case TerrainProjectType::Canal:           return 600;
        case TerrainProjectType::Tunnel:          return 300;
        case TerrainProjectType::LandReclamation: return 250;
        case TerrainProjectType::Deforestation:   return 0;   // Instant, gives production
        case TerrainProjectType::Reforestation:   return 50;
        default:                                  return 0;
    }
}

/**
 * @brief Check if a terrain project can be built at a location.
 */
[[nodiscard]] bool canBuildTerrainProject(const aoc::map::HexGrid& grid,
                                          int32_t tileIndex,
                                          TerrainProjectType type);

/**
 * @brief Execute a terrain modification project.
 *
 * Changes the terrain/feature of the target tile.
 * For canals: marks tile as passable by ships.
 * For tunnels: creates a link between two tiles on opposite sides of a mountain.
 * For land reclamation: changes coast to plains.
 *
 * @return Ok if successful.
 */
[[nodiscard]] ErrorCode executeTerrainProject(aoc::map::HexGrid& grid,
                                              int32_t tileIndex,
                                              TerrainProjectType type);

} // namespace aoc::sim
