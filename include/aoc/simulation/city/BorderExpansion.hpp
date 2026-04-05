#pragma once

/**
 * @file BorderExpansion.hpp
 * @brief Culture-based border expansion for cities.
 *
 * Cities accumulate culture points each turn. When the accumulated culture
 * exceeds a growing threshold, the city claims an adjacent unowned tile.
 * The threshold formula makes early tiles cheap and later tiles progressively
 * more expensive.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::ecs {
class World;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::hex {
struct AxialCoord;
}

namespace aoc::sim {

/**
 * @brief Compute the culture cost to claim the Nth tile via border expansion.
 *
 * Formula: 10 + 5*N + N^1.5
 *
 * @param tilesAlreadyClaimed Number of tiles already claimed by this city.
 * @return Culture points needed for the next tile.
 */
[[nodiscard]] float borderExpansionThreshold(int32_t tilesAlreadyClaimed);

/**
 * @brief Process border expansion for all cities of a player.
 *
 * Adds each city's culture yield to its cultureBorderProgress. If progress
 * exceeds the threshold, claims the nearest unowned tile adjacent to
 * already-owned territory via BFS.
 *
 * @param world   ECS world with city components.
 * @param grid    Hex grid for ownership data.
 * @param player  Player whose cities to process.
 */
void processBorderExpansion(aoc::ecs::World& world,
                             aoc::map::HexGrid& grid,
                             PlayerId player);

/**
 * @brief Claim the initial 7 tiles (city center + 6 neighbors) when founding.
 *
 * @param grid          Hex grid for ownership data.
 * @param cityLocation  Axial coordinate of the city center.
 * @param owner         Player who founded the city.
 */
void claimInitialTerritory(aoc::map::HexGrid& grid,
                            hex::AxialCoord cityLocation,
                            PlayerId owner);

} // namespace aoc::sim
