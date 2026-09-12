#pragma once

/**
 * @file Maintenance.hpp
 * @brief Gold income, unit maintenance, and building maintenance processing.
 *
 * All functions operate on the GameState object model (Player/City/Unit).
 * Per-unit maintenance scales with era (era/2 + 1). Building maintenance
 * includes the per-city sprawl cost. Below the -500 hard floor, or after five
 * turns below -200, the most expensive military unit is disbanded.
 */

#include "aoc/core/Types.hpp"

namespace aoc::map {
class HexGrid;
}

namespace aoc::game {
class Player;
class GameState;
class City;
}

namespace aoc::sim {

/// Taxable economic activity from the goods circulating in a city.
///
/// One function, because there were two copies of this sum with a comment
/// begging them to stay in sync -- and they had already drifted once, taxing
/// SURFACE_PLATE, CHARCOAL and SEMICONDUCTORS instead of CONSUMER_GOODS,
/// CLOTHING and ELECTRONICS.
///
/// It counted four goods and capped at a flat 15 per city, so the marginal unit
/// of a finished good was worth nothing almost immediately, and the most
/// valuable goods in the game earned nothing at all: Software (price 200) and
/// Microchips (160) were not in the list. It now counts every finished good
/// worth taxing, and the cap scales with the city -- a bigger market bears more
/// trade than a village.
[[nodiscard]] CurrencyAmount cityGoodsTax(const aoc::game::City& city);

/// Ceiling on the goods tax for a city of `population`.
[[nodiscard]] CurrencyAmount goodsTaxCap(int32_t population);



/// Detailed per-turn economic breakdown for diagnostic analysis.
struct EconomicBreakdown {
    // Income sources
    CurrencyAmount incomeTax         = 0;  ///< Population-based taxation
    CurrencyAmount incomeCommercial  = 0;  ///< Districts, buildings, adjacency, wonders, civ route bonus
    CurrencyAmount incomeIndustrial  = 0;  ///< Industrial revolution per-citizen bonus
    CurrencyAmount incomeTileGold    = 0;  ///< Gold from worked tiles
    CurrencyAmount incomeGoodsEcon   = 0;  ///< Taxable economic activity from goods stockpiles
    CurrencyAmount incomeCapital     = 0;  ///< Palace bonus (+10)
    CurrencyAmount incomeMoneyTax    = 0;  ///< Coin stock x taxable share x tax rate x collection efficiency
    CurrencyAmount incomeTradeRoutes = 0;  ///< Coin the civ's Traders brought home this turn. Credited by
                                           ///< the Trader system on arrival, so reported beside
                                           ///< totalIncome, never inside it
    CurrencyAmount totalIncome       = 0;  ///< Sum of the seven channels above (before goldAllocation split)
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

/// Gold charged per point of science generated: the research budget.
inline constexpr float SCIENCE_FUNDING_COST = 0.2f;

/// The player's economy this turn, read-only. Every rule the treasury applies
/// lives here (Palace, head tax, effective tile yields, districts, buildings,
/// adjacency, wonders, goods, corruption, governor, money-supply tax,
/// government multiplier, civ route bonus); processGoldIncome credits what
/// this returns, so the HUD, the CSV and the treasury cannot disagree.
[[nodiscard]] EconomicBreakdown computeEconomicBreakdown(
    const aoc::game::Player& player, const aoc::map::HexGrid& grid);

/// Credit the breakdown's effective income to the treasury and record the
/// gross income for display. Returns the gross income (before the
/// goldAllocation split), which is what the breakdown calls totalIncome.
CurrencyAmount processGoldIncome(aoc::game::Player& player,
                                  const aoc::map::HexGrid& grid);

/**
 * @brief Pay unit maintenance out of the treasury into the hands of whoever
 *        holds the province each unit stands on (a garrison abroad pays the
 *        locals: the Germania effect).
 *
 * Each military unit costs era/2 + 1 gold per turn (Ancient 1 .. Information 4).
 * Civilian units (settlers, builders, traders, scouts) are free. The treasury
 * never overdraws: an unpaid bill is arrears, and five consecutive turns of
 * arrears disband the most expensive unit above the garrison minimum.
 * @return The part of the bill that went unpaid this turn.
 */
CurrencyAmount processUnitMaintenance(aoc::game::GameState& gameState,
                                      const aoc::map::HexGrid& grid, aoc::game::Player& player);

/**
 * @brief Pay building, district, and city maintenance to the civ's own people.
 *
 * Costs: per-building from BuildingDef.maintenanceCost, +1 per city beyond
 * the capital. Scaled by inflation price level. Never overdraws.
 * @return The part of the bill that went unpaid this turn.
 */
CurrencyAmount processBuildingMaintenance(aoc::game::Player& player);

/**
 * @brief WP-P: drain food from owner's stockpile per military unit / turn.
 *
 * Per-unit cost: foot 1, cavalry/heli 2, armor/air/naval 3 (UnitTypeDef::foodPerTurn).
 * Drains priority order: nearby Encampment (5 hex) first, then PROCESSED_FOOD,
 * WHEAT, CATTLE, FISH, RICE from owner's city stockpiles.
 * Units that cannot be fed accumulate `turnsStarving`. Combat strength
 * derates 10%/turn (capped at 50%); after 5 starving turns the oldest
 * starving unit auto-disbands.
 */
void processMilitaryFoodConsumption(aoc::game::GameState& gameState,
                                     const aoc::map::HexGrid& grid,
                                     aoc::game::Player& player);

} // namespace aoc::sim
