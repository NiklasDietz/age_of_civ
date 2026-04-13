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
 * @brief Gold cost to purchase a production item instantly.
 *
 * Uses a base cost + scaled multiplier. The 8x multiplier means purchasing
 * costs roughly 8 turns of equivalent production in gold, making it a
 * meaningful investment without being impossibly expensive.
 *
 * Examples (with typical mid-game treasury 500-2000):
 *   Warrior (40 prod):       100 + 40*8 =  420 gold
 *   Swordsman (90 prod):     100 + 90*8 =  820 gold
 *   Market (50 prod):        100 + 50*8 =  500 gold
 *   Factory (120 prod):      100 + 120*8 = 1,060 gold
 *   Research Lab (480 prod): 100 + 480*8 = 3,940 gold
 */
[[nodiscard]] inline int32_t purchaseCost(float productionCost) {
    constexpr float BASE_COST = 100.0f;
    constexpr float MULTIPLIER = 8.0f;
    return static_cast<int32_t>(BASE_COST + productionCost * MULTIPLIER);
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
