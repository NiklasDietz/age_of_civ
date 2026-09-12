#pragma once

/**
 * @file Maintenance.hpp
 * @brief Gold income, unit maintenance, and building maintenance processing.
 *
 * All functions operate on the GameState object model (Player/City/Unit).
 * Per-unit maintenance scales with era (era/2 + 1) and the price level.
 * Building maintenance includes the per-city sprawl cost. Bills the treasury
 * cannot pay are arrears; five turns of them disband the costliest unit.
 */

#include "aoc/core/Types.hpp"

namespace aoc::map {
class HexGrid;
}

namespace aoc::game {
class Player;
class GameState;
class City;
class Unit;
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



/// The treasury's turn, read-only: what it takes in and what it owes.
///
/// Income is a tax on the civ's private money (plan B1): the state reaches a
/// share of the flow `privateSpecie x taxable share` at its tax rate, and the
/// share it can reach is the collection efficiency below. Buildings create no
/// gold; a Market makes the people's money easier to tax. Seigniorage and the
/// external sector arrive on their own and are reported from the turn's ledger.
struct EconomicBreakdown {
    // Income: what reaches the treasury this turn
    CurrencyAmount incomeTax         = 0;  ///< The tax the treasury keeps (goldAllocation share)
    CurrencyAmount incomeSeigniorage = 0;  ///< The Mint's share of coin struck this turn (ledger)
    CurrencyAmount incomeTariffs     = 0;  ///< Customs on foreign deliveries last turn (Player::tariffsLastTurn)
    CurrencyAmount incomeExternal    = 0;  ///< City-states, ruins, camps, endowments this turn (ledger)
    CurrencyAmount incomeTradeRoutes = 0;  ///< Customs share of coin the civ's Traders landed this turn
                                           ///< (moved by the Trader system on arrival; counted here)
    CurrencyAmount totalIncome       = 0;  ///< tax + seigniorage + tariffs + external + trade routes
    CurrencyAmount effectiveIncome   = 0;  ///< What processGoldIncome moves: the tax
    CurrencyAmount taxBase           = 0;  ///< Private money x taxable share: the flow a rate applies to
    float collectionEfficiency       = 0.0f; ///< Share of that flow the state can reach, [0, 1]

    // Expense sinks, nominal at this turn's price level
    CurrencyAmount expenseUnits      = 0;  ///< Unit maintenance
    CurrencyAmount expenseBuildings  = 0;  ///< Building + city sprawl maintenance
    CurrencyAmount expenseScience    = 0;  ///< Science funding cost
    CurrencyAmount totalExpense      = 0;  ///< Sum of all expenses

    // Net
    CurrencyAmount netFlow           = 0;  ///< effectiveIncome - totalExpense

    // Goods economy
    int32_t goodsProduced    = 0;  ///< Total goods produced this turn
    int32_t goodsConsumed    = 0;  ///< Total goods consumed this turn
    int32_t goodsStockpiled  = 0;  ///< Total goods in all city stockpiles
};

/// Gold charged per point of science generated, at price level 1: the
/// research budget. Spend-back, not destruction (the scholars are our own
/// people), and 0.1 rather than the old 0.2 so adopting coinage is not a
/// science penalty (plan 2.7).
inline constexpr float SCIENCE_FUNDING_COST = 0.1f;

/// What a state with no commerce at all still reaches of its people's money.
inline constexpr float BASE_COLLECTION_EFFICIENCY = 0.50f;

/// What one unit costs its owner a turn: its era upkeep less the
/// government's flat reduction per unit (Conscription), never below zero.
[[nodiscard]] int32_t unitUpkeep(const aoc::game::Player& player, const aoc::game::Unit& unit);

/// A building's contribution to collection efficiency: Market 0.08, Bank
/// 0.12, Stock Exchange 0.18, Telecom Hub 0.10, any other 0.01 per point of
/// its goldBonus. The AI's purchase ROI reads this table.
[[nodiscard]] float buildingCollectionBonus(BuildingId building);

/// Share of the taxable flow the state reaches, [0, 1]: the base plus the
/// population-weighted commerce of each city (Palace, hubs, harbors,
/// buildings, tile and adjacency gold, wonders, Big Ben doubling the market
/// buildings, industrialisation), each city after its corruption and governor,
/// plus road connections and civ route abilities, times the government's and
/// the alliance's gold multipliers.
[[nodiscard]] float collectionEfficiency(const aoc::game::Player& player,
                                         const aoc::map::HexGrid& grid,
                                         float allianceGoldMult = 1.0f);

/// The player's economy this turn, read-only. processGoldIncome moves what
/// this returns, so the HUD, the CSV and the treasury cannot disagree.
[[nodiscard]] EconomicBreakdown computeEconomicBreakdown(const aoc::game::Player& player,
                                                         const aoc::map::HexGrid& grid,
                                                         float allianceGoldMult = 1.0f);

/// Draw the breakdown's tax out of the civ's private money into the treasury
/// and record the turn's income for display. Returns the breakdown's total.
CurrencyAmount processGoldIncome(aoc::game::Player& player, const aoc::map::HexGrid& grid,
                                 float allianceGoldMult = 1.0f);

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
