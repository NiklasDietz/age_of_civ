#pragma once

/**
 * @file Maintenance.hpp
 * @brief Gold income, unit maintenance, and building maintenance processing.
 *
 * All functions operate on the GameState object model (Player/City/Unit).
 * Per-unit maintenance scales with era. Building maintenance includes
 * district and per-city sprawl costs. If treasury drops below -20,
 * the most expensive military unit is disbanded.
 */

#include "aoc/core/Types.hpp"

namespace aoc::map {
class HexGrid;
}

namespace aoc::game {
class Player;
}

namespace aoc::sim {

/**
 * @brief Compute gold income for a player from their cities and add to treasury.
 *
 * Income sources: capital Palace bonus (+5), worked tile gold yields,
 * Commercial Hub (+4) and Harbor (+2) district bonuses, building gold bonuses.
 *
 * @return The total gold income added.
 */
CurrencyAmount processGoldIncome(aoc::game::Player& player,
                                  const aoc::map::HexGrid& grid);

/**
 * @brief Deduct unit maintenance from a player's treasury.
 *
 * Each military unit costs gold per turn based on its era (Ancient=1 .. Information=8).
 * Civilian units (settlers, builders, traders, scouts) are free.
 * Disbands the most expensive unit if treasury falls below -20.
 */
void processUnitMaintenance(aoc::game::Player& player);

/**
 * @brief Deduct building, district, and city maintenance from treasury.
 *
 * Costs: per-building from BuildingDef.maintenanceCost, +1 per non-CityCenter
 * district, +2 per city beyond the capital. Scaled by inflation price level.
 */
void processBuildingMaintenance(aoc::game::Player& player);

} // namespace aoc::sim
