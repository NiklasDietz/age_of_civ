/**
 * @file EconomySimulation.cpp
 * @brief Per-turn economic simulation implementation.
 */

#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/economy/ResourceCurse.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/simulation/monetary/FiscalPolicy.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/ecs/World.hpp"

#include <algorithm>

namespace aoc::sim {

EconomySimulation::EconomySimulation() = default;

void EconomySimulation::initialize() {
    this->m_productionChain.build();
    this->m_market.initialize();

    LOG_INFO("Initialized: %zu recipes in production chain, %u goods on market",
             this->m_productionChain.executionOrder().size(),
             static_cast<unsigned>(goodCount()));
}

void EconomySimulation::executeTurn(aoc::ecs::World& world, const aoc::map::HexGrid& grid) {
    this->harvestResources(world, grid);
    this->executeProduction(world);
    this->reportToMarket(world);
    this->m_market.updatePrices();
    this->executeTradeRoutes(world);
    this->executeMonetaryPolicy(world);
}

// ============================================================================
// Step 1: Harvest raw resources from worked tiles into city stockpiles
// ============================================================================

void EconomySimulation::harvestResources(aoc::ecs::World& world,
                                          const aoc::map::HexGrid& grid) {
    // For each city, iterate its worked tiles and collect resource yields.
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        EntityId cityEntity = cityPool->entities()[i];
        const CityComponent& city = cityPool->data()[i];

        // Get or create the city's stockpile
        if (!world.hasComponent<CityStockpileComponent>(cityEntity)) {
            world.addComponent<CityStockpileComponent>(cityEntity, CityStockpileComponent{});
        }
        CityStockpileComponent& stockpile = world.getComponent<CityStockpileComponent>(cityEntity);

        // Harvest from each worked tile
        for (const hex::AxialCoord& tileCoord : city.workedTiles) {
            if (!grid.isValid(tileCoord)) {
                continue;
            }
            int32_t tileIndex = grid.toIndex(tileCoord);

            // Check if this tile has a resource (by checking the grid's resource field)
            ResourceId resId = grid.resource(tileIndex);
            if (!resId.isValid()) {
                continue;
            }

            // Map the ResourceId to a good ID. For simplicity, the ResourceId value
            // directly corresponds to the good ID for raw resources.
            uint16_t goodId = resId.value;
            if (goodId < goodCount()) {
                int32_t yield = 1;  // Base yield per worked tile with resource
                stockpile.addGoods(goodId, yield);
            }
        }
    }
}

// ============================================================================
// Step 2: Execute production recipes in topological order
// ============================================================================

void EconomySimulation::executeProduction(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    // Process recipes in dependency order (raw first, advanced last)
    for (const ProductionRecipe* recipe : this->m_productionChain.executionOrder()) {
        // For each city, try to execute this recipe if the city has the required building
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            EntityId cityEntity = cityPool->entities()[i];

            // Check if city has the required building
            const CityDistrictsComponent* districts =
                world.tryGetComponent<CityDistrictsComponent>(cityEntity);
            if (districts == nullptr || !districts->hasBuilding(recipe->requiredBuilding)) {
                continue;
            }

            // Check if city has sufficient input goods
            CityStockpileComponent* stockpile =
                world.tryGetComponent<CityStockpileComponent>(cityEntity);
            if (stockpile == nullptr) {
                continue;
            }

            bool hasAllInputs = true;
            for (const RecipeInput& input : recipe->inputs) {
                if (stockpile->getAmount(input.goodId) < input.amount) {
                    hasAllInputs = false;
                    break;
                }
            }

            if (!hasAllInputs) {
                continue;
            }

            // Consume inputs that are marked as consumed (availability verified above)
            for (const RecipeInput& input : recipe->inputs) {
                if (input.consumed) {
                    [[maybe_unused]] bool ok = stockpile->consumeGoods(input.goodId, input.amount);
                }
            }

            // Produce output
            stockpile->addGoods(recipe->outputGoodId, recipe->outputAmount);
        }
    }
}

// ============================================================================
// Step 3: Report supply/demand to the market
// ============================================================================

void EconomySimulation::reportToMarket(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<CityStockpileComponent>* stockpilePool =
        world.getPool<CityStockpileComponent>();
    if (stockpilePool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < stockpilePool->size(); ++i) {
        const CityStockpileComponent& stockpile = stockpilePool->data()[i];

        for (const auto& [goodId, amount] : stockpile.goods) {
            if (amount > 0) {
                this->m_market.reportSupply(goodId, amount);
            } else if (amount < 0) {
                this->m_market.reportDemand(goodId, -amount);
            }
        }

        // Cities always demand basic goods for population consumption
        EntityId cityEntity = stockpilePool->entities()[i];
        const CityComponent* city = world.tryGetComponent<CityComponent>(cityEntity);
        if (city != nullptr) {
            // Food demand based on population
            this->m_market.reportDemand(goods::WHEAT, city->population);
            // Consumer goods demand
            this->m_market.reportDemand(goods::CONSUMER_GOODS, city->population / 3 + 1);
            // Processed food demand scales with population (cities > 5 pop)
            if (city->population > 5) {
                this->m_market.reportDemand(goods::PROCESSED_FOOD, (city->population - 5) / 2 + 1);
            }
            // Advanced consumer goods demand (cities > 10 pop)
            if (city->population > 10) {
                this->m_market.reportDemand(goods::ADV_CONSUMER_GOODS, (city->population - 10) / 3 + 1);
            }
        }
    }
}

// ============================================================================
// Step 4: Execute active trade routes
// ============================================================================

void EconomySimulation::executeTradeRoutes(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<TradeRouteComponent>* tradePool =
        world.getPool<TradeRouteComponent>();
    if (tradePool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < tradePool->size(); ++i) {
        TradeRouteComponent& route = tradePool->data()[i];

        if (route.turnsRemaining > 0) {
            --route.turnsRemaining;
            continue;  // Cargo still in transit
        }

        // Deliver cargo to destination city
        CityStockpileComponent* destStockpile =
            world.tryGetComponent<CityStockpileComponent>(route.destCityId);
        if (destStockpile == nullptr) {
            continue;
        }

        for (const TradeOffer& offer : route.cargo) {
            destStockpile->addGoods(offer.goodId, offer.amountPerTurn);
        }

        // Reset for next delivery cycle
        int32_t baseTurns = static_cast<int32_t>(route.path.size()) / 5 + 1;

        // Computers tech (-1 turn transit) and Telecom Hub check
        bool hasComputersTech = false;
        world.forEach<PlayerTechComponent>(
            [&hasComputersTech, &route](EntityId, const PlayerTechComponent& tech) {
                if (tech.owner == route.sourcePlayer && tech.hasResearched(TechId{16})) {
                    hasComputersTech = true;
                }
            });
        if (hasComputersTech) {
            baseTurns = std::max(1, baseTurns - 1);
        }

        route.turnsRemaining = baseTurns;
    }
}

// ============================================================================
// Step 5: Monetary policy (inflation, fiscal)
// ============================================================================

void EconomySimulation::executeMonetaryPolicy(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        MonetaryStateComponent& state = monetaryPool->data()[i];

        // Compute GDP: sum of all city stockpile values at market prices
        CurrencyAmount currentGDP = 0;
        aoc::ecs::ComponentPool<CityStockpileComponent>* stockpilePool =
            world.getPool<CityStockpileComponent>();
        if (stockpilePool != nullptr) {
            for (uint32_t j = 0; j < stockpilePool->size(); ++j) {
                // Check city belongs to this player
                EntityId cityEntity = stockpilePool->entities()[j];
                const CityComponent* city = world.tryGetComponent<CityComponent>(cityEntity);
                if (city == nullptr || city->owner != state.owner) {
                    continue;
                }

                const CityStockpileComponent& stockpile = stockpilePool->data()[j];
                for (const auto& [goodId, amount] : stockpile.goods) {
                    if (amount > 0) {
                        currentGDP += static_cast<CurrencyAmount>(amount)
                                    * static_cast<CurrencyAmount>(this->m_market.price(goodId));
                    }
                }
            }
        }

        // Retrieve previous-turn values
        CurrencyAmount prevGDP = this->m_previousGDP[state.owner];
        CurrencyAmount prevMoney = this->m_previousMoneySupply[state.owner];

        // Run fiscal policy first (affects money supply)
        executeFiscalPolicy(state, currentGDP);

        // Compute and apply inflation
        computeInflation(state, prevGDP, currentGDP, prevMoney);
        applyInflationEffects(state);

        // Store current values for next turn's delta calculation
        this->m_previousGDP[state.owner] = currentGDP;
        this->m_previousMoneySupply[state.owner] = state.moneySupply;
    }
}

} // namespace aoc::sim
