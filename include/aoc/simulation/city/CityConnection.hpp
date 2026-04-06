#pragma once

/**
 * @file CityConnection.hpp
 * @brief City road connection detection and gold bonuses.
 *
 * Cities connected to the capital via roads receive a gold bonus each turn.
 */

#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Types.hpp"

namespace aoc::ecs {
class World;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

/**
 * @brief Check if two positions are connected via road tiles using BFS.
 *
 * Only steps through tiles where the grid has a road (hasRoad() == true).
 * Both the start and end positions must themselves have roads (or be city tiles).
 *
 * @param grid       The hex grid.
 * @param cityPos    Source city position.
 * @param capitalPos Capital city position.
 * @return true if a road path exists between the two positions.
 */
[[nodiscard]] bool isCityConnected(const aoc::map::HexGrid& grid,
                                    hex::AxialCoord cityPos,
                                    hex::AxialCoord capitalPos);

/**
 * @brief Process city connections and award gold bonuses.
 *
 * Finds the player's capital, then for each other city owned by the player,
 * checks if it is connected via roads. Connected cities grant +3 gold to
 * the player's treasury.
 *
 * @param world  The ECS world.
 * @param grid   The hex grid.
 * @param player The player to process.
 * @return Total gold bonus awarded this turn.
 */
int32_t processCityConnections(aoc::ecs::World& world,
                                const aoc::map::HexGrid& grid,
                                PlayerId player);

} // namespace aoc::sim
