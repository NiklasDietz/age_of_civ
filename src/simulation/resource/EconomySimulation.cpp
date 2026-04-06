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
#include "aoc/simulation/economy/InternalTrade.hpp"
#include "aoc/simulation/economy/EnvironmentModifier.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/simulation/monetary/FiscalPolicy.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/economy/AdvancedEconomics.hpp"
#include "aoc/simulation/monetary/CentralBank.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/CurrencyWar.hpp"
#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/simulation/economy/Sanctions.hpp"
#include "aoc/simulation/economy/Speculation.hpp"
#include "aoc/simulation/economy/ColonialEconomics.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/simulation/production/ProductionEfficiency.hpp"
#include "aoc/simulation/production/BuildingCapacity.hpp"
#include "aoc/simulation/production/QualityTier.hpp"
#include "aoc/simulation/production/Waste.hpp"
#include "aoc/simulation/production/PowerGrid.hpp"
#include "aoc/simulation/production/Automation.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/ecs/World.hpp"

#include <algorithm>
#include <unordered_set>

namespace aoc::sim {

EconomySimulation::EconomySimulation() = default;

void EconomySimulation::initialize() {
    this->m_productionChain.build();
    this->m_market.initialize();

    LOG_INFO("Initialized: %zu recipes in production chain, %u goods on market",
             this->m_productionChain.executionOrder().size(),
             static_cast<unsigned>(goodCount()));
}

void EconomySimulation::executeTurn(aoc::ecs::World& world, aoc::map::HexGrid& grid) {
    this->harvestResources(world, grid);
    this->applyResourceDepletion(world, grid);
    this->processInternalTradeForAllPlayers(world, grid);
    this->executeProduction(world, grid);
    this->reportToMarket(world);
    this->m_market.updatePrices();
    this->executeTradeRoutes(world);
    this->settleTradeInCoins(world);
    this->updateCoinReservesFromStockpiles(world);
    this->tickMonetaryMechanics(world);
    this->processCrisisAndBonds(world);
    this->processEconomicZonesAndSpeculation(world, grid);
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
//
// Worker capacity limit: each city can execute at most (population / 2)
// recipes per turn (minimum 1). Small cities specialize in 1-2 recipes;
// large cities run many. This creates natural specialization pressure.
// ============================================================================

void EconomySimulation::executeProduction(aoc::ecs::World& world,
                                          const aoc::map::HexGrid& grid) {
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    // Pre-compute power state and update automation for all cities
    std::unordered_map<uint32_t, float> cityPowerEfficiency;
    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        EntityId cityEntity = cityPool->entities()[i];

        // Compute power grid
        CityPowerComponent power = computeCityPower(world, grid, cityEntity);

        // Add robot energy demand
        const CityAutomationComponent* automation =
            world.tryGetComponent<CityAutomationComponent>(cityEntity);
        if (automation != nullptr) {
            power.energyDemand += automation->robotEnergyDemand();
        }

        cityPowerEfficiency[cityEntity.index] = power.powerEfficiency();

        // Check nuclear meltdown (deterministic hash from turn counter + city index)
        if (power.hasNuclear) {
            uint32_t turnHash = this->m_depletionTurnCounter * 7919u + cityEntity.index;
            checkNuclearMeltdown(world, cityEntity, turnHash);
        }

        // Update automation
        updateCityAutomation(world, cityEntity);
    }

    // Track per-city: total recipes executed, and per-building batches used.
    struct CityProductionState {
        int32_t totalRecipesExecuted = 0;
        std::unordered_map<uint16_t, int32_t> buildingBatchesUsed;
    };
    std::unordered_map<uint32_t, CityProductionState> cityState;

    // Process recipes in dependency order (raw first, advanced last)
    for (const ProductionRecipe* recipe : this->m_productionChain.executionOrder()) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            EntityId cityEntity = cityPool->entities()[i];
            const CityComponent& city = cityPool->data()[i];
            CityProductionState& state = cityState[cityEntity.index];

            // Worker capacity: population-based + robot workers
            const CityAutomationComponent* automation =
                world.tryGetComponent<CityAutomationComponent>(cityEntity);
            int32_t robotSlots = (automation != nullptr) ? automation->bonusRecipeSlots() : 0;
            const int32_t maxRecipes = totalWorkerCapacity(city.population, robotSlots);
            if (state.totalRecipesExecuted >= maxRecipes) {
                continue;
            }

            // Check building exists
            const CityDistrictsComponent* districts =
                world.tryGetComponent<CityDistrictsComponent>(cityEntity);
            if (districts == nullptr || !districts->hasBuilding(recipe->requiredBuilding)) {
                continue;
            }

            // Building capacity check: has this building exceeded its throughput?
            const CityBuildingLevelsComponent* levels =
                world.tryGetComponent<CityBuildingLevelsComponent>(cityEntity);
            int32_t buildingCap = 3;  // Default if no levels component
            int32_t buildingLevel = 1;
            if (levels != nullptr) {
                buildingCap = levels->capacity(recipe->requiredBuilding);
                buildingLevel = levels->getLevel(recipe->requiredBuilding);
            }
            int32_t buildingUsed = state.buildingBatchesUsed[recipe->requiredBuilding.value];
            if (buildingUsed >= buildingCap) {
                continue;
            }

            // Check inputs
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

            // === Consume inputs ===
            // Track input quality for quality propagation
            float inputQualitySum = 0.0f;
            int32_t inputCount = 0;
            CityQualityComponent* quality =
                world.tryGetComponent<CityQualityComponent>(cityEntity);

            for (const RecipeInput& input : recipe->inputs) {
                if (input.consumed) {
                    [[maybe_unused]] bool ok = stockpile->consumeGoods(input.goodId, input.amount);
                    // Track quality of consumed inputs
                    if (quality != nullptr) {
                        float q = quality->consumeGoods(input.goodId, input.amount);
                        inputQualitySum += q;
                        ++inputCount;
                    }
                }
            }

            float avgInputQuality = (inputCount > 0) ? inputQualitySum / static_cast<float>(inputCount) : 0.0f;

            // === Compute output modifiers ===
            const float infraBonus = computeInfrastructureBonus(world, grid, cityEntity);
            const float envModifier = computeEnvironmentModifier(
                world, grid, cityEntity, recipe->requiredBuilding);

            // Power efficiency
            float powerEff = 1.0f;
            auto powerIt = cityPowerEfficiency.find(cityEntity.index);
            if (powerIt != cityPowerEfficiency.end()) {
                powerEff = powerIt->second;
            }

            // Production experience
            CityProductionExperienceComponent* experience =
                world.tryGetComponent<CityProductionExperienceComponent>(cityEntity);
            if (experience == nullptr) {
                CityProductionExperienceComponent newExp{};
                world.addComponent<CityProductionExperienceComponent>(cityEntity, std::move(newExp));
                experience = world.tryGetComponent<CityProductionExperienceComponent>(cityEntity);
            }
            float expMultiplier = 1.0f;
            if (experience != nullptr) {
                expMultiplier = experience->efficiencyMultiplier(recipe->recipeId);
                experience->addExperience(recipe->recipeId);
            }

            // Industrial revolution production bonus
            float revMultiplier = 1.0f;
            const aoc::ecs::ComponentPool<PlayerIndustrialComponent>* indPool =
                world.getPool<PlayerIndustrialComponent>();
            if (indPool != nullptr) {
                for (uint32_t ri = 0; ri < indPool->size(); ++ri) {
                    if (indPool->data()[ri].owner == city.owner) {
                        revMultiplier = indPool->data()[ri].cumulativeProductionMultiplier();
                        break;
                    }
                }
            }

            // Combined output
            const int32_t boostedOutput = std::max(1, static_cast<int32_t>(
                static_cast<float>(recipe->outputAmount)
                * infraBonus * envModifier * powerEff * expMultiplier * revMultiplier));
            stockpile->addGoods(recipe->outputGoodId, boostedOutput);

            // === Quality determination ===
            bool hasPrecisionInstr = stockpile->getAmount(goods::PRECISION_INSTRUMENTS) > 0;
            uint32_t qualityHash = this->m_depletionTurnCounter * 2654435761u
                                 + cityEntity.index * 2246822519u
                                 + recipe->recipeId * 104729u;
            QualityTier outputQuality = determineOutputQuality(
                buildingLevel,
                (experience != nullptr) ? experience->getExperience(recipe->recipeId) : 0,
                hasPrecisionInstr,
                avgInputQuality,
                qualityHash);

            // Store quality in quality tracking component
            if (quality == nullptr) {
                CityQualityComponent newQuality{};
                world.addComponent<CityQualityComponent>(cityEntity, std::move(newQuality));
                quality = world.tryGetComponent<CityQualityComponent>(cityEntity);
            }
            if (quality != nullptr) {
                quality->addGoods(recipe->outputGoodId, boostedOutput, outputQuality);
            }

            // === Waste generation ===
            accumulateWaste(world, cityEntity, recipe->requiredBuilding, 1);

            // === Bookkeeping ===
            ++state.totalRecipesExecuted;
            ++state.buildingBatchesUsed[recipe->requiredBuilding.value];
        }
    }

    // Post-production: waste treatment for all cities
    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        processWasteTreatment(world, cityPool->entities()[i]);
    }
}

// ============================================================================
// Internal trade: redistribute surplus goods between a player's own cities
// ============================================================================

void EconomySimulation::processInternalTradeForAllPlayers(aoc::ecs::World& world,
                                                          const aoc::map::HexGrid& grid) {
    // Collect unique player IDs from all cities
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    std::unordered_set<PlayerId> players;
    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        const PlayerId owner = cityPool->data()[i].owner;
        if (owner != INVALID_PLAYER && owner != BARBARIAN_PLAYER) {
            players.insert(owner);
        }
    }

    for (const PlayerId player : players) {
        processInternalTrade(world, grid, player);
    }
}

// ============================================================================
// Resource depletion: small chance per turn that a tile's yield decreases
// ============================================================================

void EconomySimulation::applyResourceDepletion(aoc::ecs::World& world,
                                                aoc::map::HexGrid& grid) {
    // Use a deterministic counter-based approach for depletion checks.
    // Each tile's resource has a 1% chance per turn of yield decrease.
    // We use (turn number * tile index) hash for determinism without a full RNG.
    ++this->m_depletionTurnCounter;

    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        const CityComponent& city = cityPool->data()[i];

        for (const hex::AxialCoord& tileCoord : city.workedTiles) {
            if (!grid.isValid(tileCoord)) {
                continue;
            }
            const int32_t tileIndex = grid.toIndex(tileCoord);
            const ResourceId resId = grid.resource(tileIndex);
            if (!resId.isValid()) {
                continue;
            }

            // Deterministic 1% depletion chance per turn using hash
            const uint32_t hash = static_cast<uint32_t>(tileIndex) * 2654435761u
                                + this->m_depletionTurnCounter * 2246822519u;
            constexpr uint32_t DEPLETION_THRESHOLD = 42949672u;  // ~1% of UINT32_MAX
            if (hash < DEPLETION_THRESHOLD) {
                // Deplete: remove the resource from this tile
                grid.setResource(tileIndex, ResourceId{});
                LOG_INFO("Resource depleted at tile (%d,%d): good %u exhausted",
                         tileCoord.q, tileCoord.r, static_cast<unsigned>(resId.value));
            }
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

// ============================================================================
// Step 4b: Settle trade route imbalances in coins (price-specie flow)
//
// When goods flow from player A to player B, B owes A payment in coins.
// The net importer's highest-tier coins flow to the net exporter.
// This is the core mechanism that distributes money metals through trade.
// ============================================================================

void EconomySimulation::settleTradeInCoins(aoc::ecs::World& world) {
    const aoc::ecs::ComponentPool<TradeRouteComponent>* tradePool =
        world.getPool<TradeRouteComponent>();
    if (tradePool == nullptr) {
        return;
    }

    // Accumulate net trade value per player pair.
    // Key: (source, dest), Value: total goods value flowing source -> dest.
    struct PlayerPairHash {
        std::size_t operator()(const std::pair<PlayerId, PlayerId>& p) const {
            return std::hash<uint32_t>()(
                (static_cast<uint32_t>(p.first) << 16) | static_cast<uint32_t>(p.second));
        }
    };
    std::unordered_map<std::pair<PlayerId, PlayerId>, int32_t, PlayerPairHash> tradeFlows;

    for (uint32_t i = 0; i < tradePool->size(); ++i) {
        const TradeRouteComponent& route = tradePool->data()[i];
        if (route.turnsRemaining > 0) {
            continue;  // Cargo still in transit, no settlement yet
        }

        int32_t cargoValue = 0;
        for (const TradeOffer& offer : route.cargo) {
            cargoValue += offer.amountPerTurn * this->m_market.price(offer.goodId);
        }

        if (cargoValue > 0) {
            auto key = std::make_pair(route.sourcePlayer, route.destPlayer);
            tradeFlows[key] += cargoValue;
        }
    }

    // For each player pair, compute net balance and transfer coins.
    // Net importer pays coins to net exporter.
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool == nullptr) {
        return;
    }

    // Build a lookup from player -> monetary component index
    std::unordered_map<PlayerId, uint32_t> playerToMonetary;
    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        playerToMonetary[monetaryPool->data()[i].owner] = i;
    }

    // Process settled pairs (only process A->B, not B->A separately)
    std::unordered_set<uint64_t> processed;
    for (const auto& [pair, flow] : tradeFlows) {
        PlayerId pA = pair.first;
        PlayerId pB = pair.second;

        uint64_t pairKey = (static_cast<uint64_t>(std::min(pA, pB)) << 32)
                         | static_cast<uint64_t>(std::max(pA, pB));
        if (processed.count(pairKey) > 0) {
            continue;
        }
        processed.insert(pairKey);

        // Net flow: positive means B owes A (A is net exporter)
        int32_t flowAtoB = flow;
        auto reverseKey = std::make_pair(pB, pA);
        auto reverseIt = tradeFlows.find(reverseKey);
        int32_t flowBtoA = (reverseIt != tradeFlows.end()) ? reverseIt->second : 0;
        int32_t netBalance = flowAtoB - flowBtoA;

        if (netBalance == 0) {
            continue;
        }

        // Determine who pays and who receives
        PlayerId payer    = (netBalance > 0) ? pB : pA;
        PlayerId receiver = (netBalance > 0) ? pA : pB;
        int32_t paymentValue = std::abs(netBalance);

        auto payerIt = playerToMonetary.find(payer);
        auto recvIt  = playerToMonetary.find(receiver);
        if (payerIt == playerToMonetary.end() || recvIt == playerToMonetary.end()) {
            continue;
        }

        MonetaryStateComponent& payerState = monetaryPool->data()[payerIt->second];
        MonetaryStateComponent& recvState  = monetaryPool->data()[recvIt->second];

        // Scale payment by bilateral trade efficiency (coin tier mismatch penalty)
        float efficiency = bilateralTradeEfficiency(world, payer, receiver);
        int32_t effectivePayment = static_cast<int32_t>(
            static_cast<float>(paymentValue) * efficiency * 0.05f);
        // 5% of trade value converts to coin transfer (the rest is goods-for-goods)
        effectivePayment = std::max(1, effectivePayment);

        // Transfer coins: payer gives their highest-tier coins
        // Gold first, then silver, then copper
        int32_t remaining = effectivePayment;

        // Gold coins (value 4 each)
        if (remaining > 0 && payerState.goldCoinReserves > 0) {
            int32_t goldToTransfer = std::min(payerState.goldCoinReserves, (remaining + 3) / 4);
            payerState.goldCoinReserves -= goldToTransfer;
            recvState.goldCoinReserves  += goldToTransfer;
            remaining -= goldToTransfer * 4;
        }
        // Silver coins (value 2 each)
        if (remaining > 0 && payerState.silverCoinReserves > 0) {
            int32_t silverToTransfer = std::min(payerState.silverCoinReserves, (remaining + 1) / 2);
            payerState.silverCoinReserves -= silverToTransfer;
            recvState.silverCoinReserves  += silverToTransfer;
            remaining -= silverToTransfer * 2;
        }
        // Copper coins (value 1 each)
        if (remaining > 0 && payerState.copperCoinReserves > 0) {
            int32_t copperToTransfer = std::min(payerState.copperCoinReserves, remaining);
            payerState.copperCoinReserves -= copperToTransfer;
            recvState.copperCoinReserves  += copperToTransfer;
        }

        payerState.updateCoinTier();
        recvState.updateCoinTier();
    }
}

// ============================================================================
// Sync coin reserves from city stockpiles into the monetary state.
//
// Each turn, aggregate all COPPER_COINS, SILVER_COINS, GOLD_COINS from
// the player's cities into the monetary component. This is how minting
// (via production recipes) feeds into the monetary system.
// ============================================================================

void EconomySimulation::updateCoinReservesFromStockpiles(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool == nullptr) {
        return;
    }

    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    const aoc::ecs::ComponentPool<CityStockpileComponent>* stockpilePool =
        world.getPool<CityStockpileComponent>();
    if (cityPool == nullptr || stockpilePool == nullptr) {
        return;
    }

    // Reset coin reserves, then re-aggregate from all cities
    for (uint32_t m = 0; m < monetaryPool->size(); ++m) {
        MonetaryStateComponent& state = monetaryPool->data()[m];
        state.copperCoinReserves = 0;
        state.silverCoinReserves = 0;
        state.goldCoinReserves   = 0;
    }

    for (uint32_t i = 0; i < stockpilePool->size(); ++i) {
        EntityId cityEntity = stockpilePool->entities()[i];
        const CityStockpileComponent& stockpile = stockpilePool->data()[i];

        // Find which player owns this city
        const CityComponent* city = world.tryGetComponent<CityComponent>(cityEntity);
        if (city == nullptr) {
            continue;
        }

        // Find the player's monetary state
        for (uint32_t m = 0; m < monetaryPool->size(); ++m) {
            MonetaryStateComponent& state = monetaryPool->data()[m];
            if (state.owner == city->owner) {
                state.copperCoinReserves += stockpile.getAmount(goods::COPPER_COINS);
                state.silverCoinReserves += stockpile.getAmount(goods::SILVER_COINS);
                state.goldCoinReserves   += stockpile.getAmount(goods::GOLD_COINS);
                break;
            }
        }
    }

    // Update coin tiers and money supply for commodity money players
    for (uint32_t m = 0; m < monetaryPool->size(); ++m) {
        MonetaryStateComponent& state = monetaryPool->data()[m];
        state.updateCoinTier();

        // In commodity money, money supply = total coin value
        if (state.system == MonetarySystemType::CommodityMoney) {
            state.moneySupply = static_cast<CurrencyAmount>(state.totalCoinValue());
        }
    }
}

// ============================================================================
// Per-turn monetary mechanic ticks: debasement discovery, trust updates,
// reserve currency check, and system duration tracking.
// ============================================================================

void EconomySimulation::tickMonetaryMechanics(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool == nullptr) {
        return;
    }

    // Count active players for trust computation
    int32_t playerCount = static_cast<int32_t>(monetaryPool->size());

    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        MonetaryStateComponent& state = monetaryPool->data()[i];
        ++state.turnsInCurrentSystem;

        // Debasement discovery (Commodity Money only)
        if (state.system == MonetarySystemType::CommodityMoney) {
            if (tickDebasementDiscovery(state)) {
                LOG_INFO("Player %u: coin debasement discovered by trade partners!",
                         static_cast<unsigned>(state.owner));
            }
        }

        // Currency trust updates (Fiat only)
        if (state.system == MonetarySystemType::FiatMoney) {
            CurrencyTrustComponent* trust =
                world.tryGetComponent<CurrencyTrustComponent>(monetaryPool->entities()[i]);
            if (trust == nullptr) {
                // Create trust component on first fiat turn
                CurrencyTrustComponent newTrust{};
                newTrust.owner = state.owner;
                newTrust.trustScore = 0.30f;  // Start skeptical
                world.addComponent<CurrencyTrustComponent>(
                    monetaryPool->entities()[i], std::move(newTrust));
                trust = world.tryGetComponent<CurrencyTrustComponent>(
                    monetaryPool->entities()[i]);
            }
            if (trust != nullptr) {
                computeCurrencyTrust(world, state, *trust, playerCount);
            }
        }
    }

    // Global reserve currency check
    updateReserveCurrencyStatus(world);
}

// ============================================================================
// Currency crises, bond payments, and currency war processing.
// ============================================================================

void EconomySimulation::processCrisisAndBonds(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool == nullptr) {
        return;
    }

    // Process currency crises for each player
    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        MonetaryStateComponent& state = monetaryPool->data()[i];
        EntityId entity = monetaryPool->entities()[i];

        // Get or create crisis component
        CurrencyCrisisComponent* crisis =
            world.tryGetComponent<CurrencyCrisisComponent>(entity);
        if (crisis == nullptr) {
            CurrencyCrisisComponent newCrisis{};
            newCrisis.owner = state.owner;
            world.addComponent<CurrencyCrisisComponent>(entity, std::move(newCrisis));
            crisis = world.tryGetComponent<CurrencyCrisisComponent>(entity);
        }
        if (crisis != nullptr) {
            processCurrencyCrisis(world, state, *crisis);
        }
    }

    // Process bond payments
    processBondPayments(world);

    // Process currency wars
    processCurrencyWar(world, this->m_currencyWarState);
}

// ============================================================================
// Economic zones, speculation, and sanctions.
// ============================================================================

void EconomySimulation::processEconomicZonesAndSpeculation(aoc::ecs::World& world,
                                                            aoc::map::HexGrid& grid) {
    // Process colonial economic zones
    processEconomicZones(world, grid, this->m_market, this->m_economicZones);

    // Process sanctions (tick durations)
    processSanctions(world, this->m_sanctions);

    // Process speculation (hoarded goods affect market supply/demand)
    processSpeculation(world, this->m_market);

    // Check industrial revolution progress for all players
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool != nullptr) {
        for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
            PlayerId player = monetaryPool->data()[i].owner;
            // Create industrial component if missing
            bool hasInd = false;
            aoc::ecs::ComponentPool<PlayerIndustrialComponent>* indPool =
                world.getPool<PlayerIndustrialComponent>();
            if (indPool != nullptr) {
                for (uint32_t j = 0; j < indPool->size(); ++j) {
                    if (indPool->data()[j].owner == player) {
                        hasInd = true;
                        break;
                    }
                }
            }
            if (!hasInd) {
                EntityId entity = monetaryPool->entities()[i];
                PlayerIndustrialComponent newInd{};
                newInd.owner = player;
                world.addComponent<PlayerIndustrialComponent>(entity, std::move(newInd));
            }
            checkIndustrialRevolution(world, player,
                                      static_cast<TurnNumber>(this->m_depletionTurnCounter));
        }
    }
}

} // namespace aoc::sim
