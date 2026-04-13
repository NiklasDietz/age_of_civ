#pragma once

/**
 * @file ProductionSystem.hpp
 * @brief City production queue processing.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"

namespace aoc::game {
class GameState;
class Player;
class City;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

/**
 * @brief Process production queues for all cities of a player.
 */
void processProductionQueues(aoc::game::GameState& gameState,
                              const aoc::map::HexGrid& grid,
                              PlayerId player);

/**
 * @brief Gold cost to purchase a production item instantly (4x production cost).
 */
[[nodiscard]] inline int32_t purchaseCost(float productionCost) {
    return static_cast<int32_t>(productionCost * 4.0f);
}

/**
 * @brief Purchase a unit or building instantly with gold.
 *
 * Deducts gold from the player's treasury and creates the item immediately.
 * In Civ 6, purchasing costs 4x the production cost.
 *
 * @return ErrorCode::Ok on success, InsufficientResources if not enough gold.
 */
[[nodiscard]] ErrorCode purchaseInCity(aoc::game::GameState& gameState,
                                       aoc::game::Player& player,
                                       aoc::game::City& city,
                                       ProductionItemType type,
                                       uint16_t itemId);

} // namespace aoc::sim
