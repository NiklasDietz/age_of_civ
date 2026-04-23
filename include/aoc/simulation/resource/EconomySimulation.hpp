#pragma once

/**
 * @file EconomySimulation.hpp
 * @brief Per-turn economic simulation: harvest, produce, trade, price update.
 *
 * This is the core system that ties together resources, production chains,
 * markets, and city stockpiles. Runs during the ResourceProduction and
 * TradeExecution turn phases.
 */

#include "aoc/simulation/resource/ProductionChain.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/Sanctions.hpp"
#include "aoc/simulation/economy/ColonialEconomics.hpp"
#include "aoc/simulation/monetary/CurrencyWar.hpp"
#include "aoc/core/Types.hpp"

#include <unordered_map>

namespace aoc::game {
class GameState;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

class EconomySimulation {
public:
    EconomySimulation();

    /// Initialize production chain DAG and market. Call once at game start.
    void initialize();

    /**
     * @brief Run one turn of economic simulation.
     *
     * Execution order:
     *   1. Harvest raw resources from worked tiles into city stockpiles.
     *   2. Execute production recipes in topological order.
     *   3. Report supply/demand to market.
     *   4. Update market prices.
     *   5. Execute active trade routes.
     *   6. Compute resource curse effects.
     *   7. Run monetary policy (inflation, fiscal).
     */
    void executeTurn(aoc::game::GameState& gameState, aoc::map::HexGrid& grid);

    /// Access the market (for UI display / trade decisions).
    [[nodiscard]] Market& market() { return this->m_market; }
    [[nodiscard]] const Market& market() const { return this->m_market; }

    [[nodiscard]] const ProductionChain& productionChain() const { return this->m_productionChain; }

private:
    void harvestResources(aoc::game::GameState& gameState, aoc::map::HexGrid& grid);
    void processInternalTradeForAllPlayers(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid);
    void consumeBuildingFuel(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid);
    void executeProduction(aoc::game::GameState& gameState, aoc::map::HexGrid& grid);
    void computePlayerNeeds(aoc::game::GameState& gameState);
    void applyResourceDepletion(aoc::game::GameState& gameState, aoc::map::HexGrid& grid);
    void reportToMarket(aoc::game::GameState& gameState);
    void executeTradeRoutes(aoc::game::GameState& gameState);
    void settleTradeInCoins(aoc::game::GameState& gameState);
    void updateCoinReservesFromStockpiles(aoc::game::GameState& gameState);
    void tickMonetaryMechanics(aoc::game::GameState& gameState);
    void executeMonetaryPolicy(aoc::game::GameState& gameState);
    void processCrisisAndBonds(aoc::game::GameState& gameState);
    void processEconomicZonesAndSpeculation(aoc::game::GameState& gameState, aoc::map::HexGrid& grid);

    ProductionChain m_productionChain;
    Market          m_market;

    /// Previous-turn GDP per player (for inflation delta calculation).
    std::unordered_map<PlayerId, CurrencyAmount> m_previousGDP;
    std::unordered_map<PlayerId, CurrencyAmount> m_previousMoneySupply;

    /// Counter for deterministic resource depletion checks.
    uint32_t m_depletionTurnCounter = 0;

    /// Global state trackers for the new economic systems.
    GlobalSanctionTracker       m_sanctions;
    GlobalEconomicZoneTracker   m_economicZones;
    GlobalCurrencyWarState      m_currencyWarState;

    /// Per-(city,building) recipe preference.  Key = (cityOwner << 32) | (
    /// cityLocationHash << 8) | buildingId.  Value = recipe id that building
    /// should run in priority on that city.  When set, the ranked loop
    /// still considers ALL candidate recipes for the building but the
    /// preferred one wins ties + gets a large score bonus so it runs first
    /// if its inputs are available.  No entry = auto (ranked-by-profit).
    /// Not persisted across save/load — UI is expected to set this from the
    /// city screen each session.
    std::unordered_map<uint64_t, uint16_t> m_recipePreference;

public:
    [[nodiscard]] GlobalSanctionTracker& sanctions() { return this->m_sanctions; }
    [[nodiscard]] const GlobalSanctionTracker& sanctions() const { return this->m_sanctions; }
    [[nodiscard]] GlobalEconomicZoneTracker& economicZones() { return this->m_economicZones; }
    [[nodiscard]] const GlobalEconomicZoneTracker& economicZones() const { return this->m_economicZones; }
    [[nodiscard]] GlobalCurrencyWarState& currencyWar() { return this->m_currencyWarState; }
    [[nodiscard]] const GlobalCurrencyWarState& currencyWar() const { return this->m_currencyWarState; }

    /// Record a per-city, per-building recipe preference.  0xFFFF clears.
    void setRecipePreference(PlayerId owner, uint32_t cityLocHash,
                             uint16_t buildingId, uint16_t recipeId);

    /// Look up the preferred recipe for (owner, cityLocHash, buildingId).
    /// Returns 0xFFFF when no preference is set.
    [[nodiscard]] uint16_t recipePreference(PlayerId owner, uint32_t cityLocHash,
                                            uint16_t buildingId) const;
};

} // namespace aoc::sim
