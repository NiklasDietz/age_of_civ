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
#include "aoc/core/Types.hpp"

#include <unordered_map>

namespace aoc::ecs {
class World;
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
    void executeTurn(aoc::ecs::World& world, const aoc::map::HexGrid& grid);

    /// Access the market (for UI display / trade decisions).
    [[nodiscard]] Market& market() { return this->m_market; }
    [[nodiscard]] const Market& market() const { return this->m_market; }

    [[nodiscard]] const ProductionChain& productionChain() const { return this->m_productionChain; }

private:
    void harvestResources(aoc::ecs::World& world, const aoc::map::HexGrid& grid);
    void executeProduction(aoc::ecs::World& world);
    void reportToMarket(aoc::ecs::World& world);
    void executeTradeRoutes(aoc::ecs::World& world);
    void executeMonetaryPolicy(aoc::ecs::World& world);

    ProductionChain m_productionChain;
    Market          m_market;

    /// Previous-turn GDP per player (for inflation delta calculation).
    std::unordered_map<PlayerId, CurrencyAmount> m_previousGDP;
    std::unordered_map<PlayerId, CurrencyAmount> m_previousMoneySupply;
};

} // namespace aoc::sim
