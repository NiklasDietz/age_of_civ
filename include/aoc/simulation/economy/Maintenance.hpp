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

/// Detailed per-turn economic breakdown for diagnostic analysis.
struct EconomicBreakdown {
    // Income sources
    CurrencyAmount incomeTax         = 0;  ///< Population-based taxation
    CurrencyAmount incomeCommercial  = 0;  ///< Commercial districts + buildings (Market/Bank/etc)
    CurrencyAmount incomeIndustrial  = 0;  ///< Industrial revolution per-citizen bonus
    CurrencyAmount incomeTileGold    = 0;  ///< Gold from worked tiles
    CurrencyAmount incomeGoodsEcon   = 0;  ///< Taxable economic activity from goods stockpiles
    CurrencyAmount incomeCapital     = 0;  ///< Palace bonus (+5)
    CurrencyAmount totalIncome       = 0;  ///< Sum of all income (before goldAllocation split)
    CurrencyAmount effectiveIncome   = 0;  ///< After goldAllocation (what goes to treasury)

    // Expense sinks
    CurrencyAmount expenseUnits      = 0;  ///< Unit maintenance
    CurrencyAmount expenseBuildings  = 0;  ///< Building + district + city sprawl maintenance
    CurrencyAmount expenseScience    = 0;  ///< Science funding cost
    CurrencyAmount totalExpense      = 0;  ///< Sum of all expenses

    // Net
    CurrencyAmount netFlow           = 0;  ///< effectiveIncome - totalExpense

    // Goods economy
    int32_t goodsProduced    = 0;  ///< Total goods produced this turn
    int32_t goodsConsumed    = 0;  ///< Total goods consumed this turn
    int32_t goodsStockpiled  = 0;  ///< Total goods in all city stockpiles
};

/// Compute economic breakdown for a player (read-only, no side effects).
[[nodiscard]] EconomicBreakdown computeEconomicBreakdown(
    const aoc::game::Player& player, const aoc::map::HexGrid& grid);

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
