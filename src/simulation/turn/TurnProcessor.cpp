/**
 * @file TurnProcessor.cpp
 * @brief Unified turn processing: the single source of truth for game logic.
 */

#include "aoc/simulation/turn/TurnProcessor.hpp"
#include "aoc/simulation/turn/GameLength.hpp"

// ECS
#include "aoc/ecs/World.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/RiverGameplay.hpp"

// City systems
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/CityGrowth.hpp"
#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/city/ProductionSystem.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/BorderExpansion.hpp"
#include "aoc/simulation/city/CityBombardment.hpp"
#include "aoc/simulation/city/CityConnection.hpp"

// Economy
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/economy/Maintenance.hpp"
#include "aoc/simulation/economy/AdvancedEconomics.hpp"
#include "aoc/simulation/economy/EconomicDepth.hpp"
#include "aoc/simulation/economy/Market.hpp"

// Tech
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/tech/EraScore.hpp"
#include "aoc/simulation/tech/EurekaBoost.hpp"

// Military
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/Movement.hpp"

// Diplomacy
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/WarWeariness.hpp"

// Government
#include "aoc/simulation/government/GovernmentComponent.hpp"

// Religion
#include "aoc/simulation/religion/Religion.hpp"

// Victory
#include "aoc/simulation/victory/VictoryCondition.hpp"

// Barbarians
#include "aoc/simulation/barbarian/BarbarianController.hpp"

// Great People
#include "aoc/simulation/greatpeople/GreatPeople.hpp"

// City-States
#include "aoc/simulation/citystate/CityState.hpp"

// Climate & Disasters
#include "aoc/simulation/climate/Climate.hpp"
#include "aoc/simulation/climate/NaturalDisasters.hpp"

// Empire
#include "aoc/simulation/empire/CommunicationSpeed.hpp"

// Events
#include "aoc/simulation/event/WorldEvents.hpp"

// Production
#include "aoc/simulation/production/Waste.hpp"

// Automation
#include "aoc/simulation/city/Governor.hpp"
#include "aoc/simulation/automation/Automation.hpp"

// AI
#include "aoc/simulation/ai/AIController.hpp"

// Monetary
#include "aoc/simulation/monetary/MonetarySystem.hpp"

// Energy
#include "aoc/simulation/economy/EnergyDependency.hpp"

// Advanced economics
#include "aoc/simulation/economy/StockMarket.hpp"
#include "aoc/simulation/economy/TradeAgreement.hpp"
#include "aoc/simulation/economy/SupplyChain.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/economy/SpeculationBubble.hpp"
#include "aoc/simulation/economy/MonopolyPricing.hpp"
#include "aoc/simulation/economy/TechUnemployment.hpp"
#include "aoc/simulation/economy/BlackMarket.hpp"
#include "aoc/simulation/economy/HumanCapital.hpp"

// Logging
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <string>

namespace aoc::sim {

// ============================================================================
// City founding -- single source of truth
// ============================================================================

EntityId foundCity(aoc::ecs::World& world,
                   aoc::map::HexGrid& grid,
                   PlayerId owner,
                   aoc::hex::AxialCoord location,
                   const std::string& name,
                   bool isOriginalCapital,
                   int32_t startingPop) {
    EntityId cityEntity = world.createEntity();

    // Core city component
    CityComponent city{};
    city.owner = owner;
    city.name = name;
    city.location = location;
    city.population = startingPop;
    city.isOriginalCapital = isOriginalCapital;
    city.workedTiles.push_back(location);

    // Auto-assign nearby tiles, preferring tiles with resources
    std::array<aoc::hex::AxialCoord, 6> neighbors = aoc::hex::neighbors(location);

    // Sort: resource tiles first, then by yield value
    struct TileScore {
        int32_t index;
        float score;
    };
    std::vector<TileScore> tileScores;
    for (int32_t n = 0; n < 6; ++n) {
        if (!grid.isValid(neighbors[static_cast<std::size_t>(n)])) { continue; }
        int32_t idx = grid.toIndex(neighbors[static_cast<std::size_t>(n)]);
        if (aoc::map::isWater(grid.terrain(idx)) || aoc::map::isImpassable(grid.terrain(idx))) { continue; }
        float score = 0.0f;
        aoc::map::TileYield yield = grid.tileYield(idx);
        score += static_cast<float>(yield.food) * 2.0f;
        score += static_cast<float>(yield.production) * 1.5f;
        score += static_cast<float>(yield.gold) * 1.0f;
        if (grid.resource(idx).isValid()) {
            score += 5.0f;  // Strong preference for resource tiles
        }
        tileScores.push_back({n, score});
    }
    std::sort(tileScores.begin(), tileScores.end(),
        [](const TileScore& a, const TileScore& b) { return a.score > b.score; });

    int32_t assigned = 0;
    for (const TileScore& ts : tileScores) {
        if (assigned >= 3) { break; }
        city.workedTiles.push_back(neighbors[static_cast<std::size_t>(ts.index)]);
        ++assigned;
    }
    world.addComponent<CityComponent>(cityEntity, std::move(city));

    // Production queue (required for building anything)
    world.addComponent<ProductionQueueComponent>(cityEntity, ProductionQueueComponent{});

    // Resource stockpile (required for economy)
    world.addComponent<CityStockpileComponent>(cityEntity, CityStockpileComponent{});

    // Happiness
    world.addComponent<CityHappinessComponent>(cityEntity, CityHappinessComponent{});

    // Loyalty
    CityLoyaltyComponent loyalty{};
    loyalty.loyalty = 100.0f;
    world.addComponent<CityLoyaltyComponent>(cityEntity, std::move(loyalty));

    // Districts (CityCenter always present)
    CityDistrictsComponent districts{};
    CityDistrictsComponent::PlacedDistrict centerDistrict{};
    centerDistrict.type = DistrictType::CityCenter;
    centerDistrict.location = location;
    districts.districts.push_back(std::move(centerDistrict));
    world.addComponent<CityDistrictsComponent>(cityEntity, std::move(districts));

    // Religion tracking
    world.addComponent<CityReligionComponent>(cityEntity, CityReligionComponent{});

    // Claim tiles
    int32_t centerIdx = grid.toIndex(location);
    grid.setOwner(centerIdx, owner);
    for (const aoc::hex::AxialCoord& nbr : neighbors) {
        if (grid.isValid(nbr)) {
            int32_t nbrIdx = grid.toIndex(nbr);
            if (grid.owner(nbrIdx) == INVALID_PLAYER) {
                grid.setOwner(nbrIdx, owner);
            }
        }
    }

    LOG_INFO("City founded: %s by player %u at (%d,%d)",
             name.c_str(), static_cast<unsigned>(owner),
             location.q, location.r);

    return cityEntity;
}

// ============================================================================
// Per-player turn processing
// ============================================================================

void processPlayerTurn(TurnContext& ctx, PlayerId player) {
    aoc::ecs::World& world = *ctx.world;
    aoc::map::HexGrid& grid = *ctx.grid;

    // City gold income: worked tiles, buildings, and base population income
    {
        CurrencyAmount goldIncome = 0;
        const aoc::ecs::ComponentPool<CityComponent>* cityPool =
            world.getPool<CityComponent>();
        if (cityPool != nullptr) {
            for (uint32_t ci = 0; ci < cityPool->size(); ++ci) {
                if (cityPool->data()[ci].owner != player) { continue; }
                EntityId cityEntity = cityPool->entities()[ci];
                const CityComponent& city = cityPool->data()[ci];

                // Base gold from population (3 gold per citizen)
                goldIncome += static_cast<CurrencyAmount>(city.population) * 3;

                // Capital bonus
                if (city.isOriginalCapital) {
                    goldIncome += 5;
                }

                // Gold from worked tiles
                for (const aoc::hex::AxialCoord& tile : city.workedTiles) {
                    if (grid.isValid(tile)) {
                        aoc::map::TileYield yield = grid.tileYield(grid.toIndex(tile));
                        goldIncome += static_cast<CurrencyAmount>(yield.gold);
                    }
                }

                // Gold from buildings
                const CityDistrictsComponent* districts =
                    world.tryGetComponent<CityDistrictsComponent>(cityEntity);
                if (districts != nullptr) {
                    for (const CityDistrictsComponent::PlacedDistrict& d : districts->districts) {
                        for (BuildingId bid : d.buildings) {
                            goldIncome += static_cast<CurrencyAmount>(buildingDef(bid).goldBonus);
                        }
                    }
                }
            }
        }

        // Gold income goes directly to treasury (not taxed -- tax rate affects GDP revenue
        // in the fiscal policy system, not direct city yields)

        // Add to economy treasury
        world.forEach<PlayerEconomyComponent>(
            [player, goldIncome](EntityId, PlayerEconomyComponent& ec) {
                if (ec.owner == player) {
                    ec.treasury += goldIncome;
                    ec.incomePerTurn = goldIncome;
                }
            });
    }

    // Maintenance costs
    processUnitMaintenance(world, player);
    processBuildingMaintenance(world, player);

    // City connections
    processCityConnections(world, grid, player);

    // Advanced economics (tariffs, banking, debt)
    processAdvancedEconomics(world, grid, player, ctx.economy->market());

    // War weariness
    processWarWeariness(world, player, *ctx.diplomacy);

    // Golden/Dark age effects
    processAgeEffects(world, player);

    // City growth
    processCityGrowth(world, grid, player);

    // City happiness
    computeCityHappiness(world, player);

    // City loyalty
    computeCityLoyalty(world, grid, player);

    // Government processing (anarchy ticks, active action ticks)
    processGovernment(world, player);

    // Religion
    accumulateFaith(world, grid, player);
    applyReligionBonuses(world, player);

    // Science + tech research
    float science = computePlayerScience(world, grid, player);
    float culture = computePlayerCulture(world, grid, player);

    world.forEach<PlayerTechComponent>(
        [player, science](aoc::EntityId, PlayerTechComponent& tech) {
            if (tech.owner == player) {
                advanceResearch(tech, science);
            }
        });

    // Civic research (with government unlock)
    world.forEach<PlayerCivicComponent>(
        [player, culture, &world](aoc::EntityId, PlayerCivicComponent& civic) {
            if (civic.owner != player) { return; }
            PlayerGovernmentComponent* gov = nullptr;
            world.forEach<PlayerGovernmentComponent>(
                [player, &gov](aoc::EntityId, PlayerGovernmentComponent& g) {
                    if (g.owner == player) { gov = &g; }
                });
            advanceCivicResearch(civic, culture, gov);
        });

    // Production queues
    processProductionQueues(world, grid, player);

    // City bombardment
    processCityBombardment(world, grid, player, *ctx.rng);

    // Border expansion
    processBorderExpansion(world, grid, player);

    // Great people
    accumulateGreatPeoplePoints(world, player);
    checkGreatPeopleRecruitment(world, player);

    // City-state bonuses
    processCityStateBonuses(world, player);

    // Governor: auto-queue production for cities with governors
    processGovernors(world, grid, player);

    // Automation: research queue, auto-explore, military alert
    processAutomation(world, grid, player);
}

// ============================================================================
// Global systems (not per-player)
// ============================================================================

void processGlobalSystems(TurnContext& ctx) {
    aoc::ecs::World& world = *ctx.world;
    aoc::map::HexGrid& grid = *ctx.grid;

    // Religious spread (global, affects all cities)
    processReligiousSpread(world, grid);

    // Barbarians
    if (ctx.barbarians != nullptr) {
        ctx.barbarians->executeTurn(world, grid, *ctx.rng);
    }

    // Communication speed (affects all players)
    processCommunication(world, grid);

    // Tick prospect cooldowns (tiles that were surveyed become available again)
    grid.tickProspectCooldowns();

    // Labor strikes
    checkLaborStrikes(world);
    processStrikes(world);

    // Migration between civilizations
    processMigration(world, grid);

    // Insurance premium payments
    processInsurancePremiums(world);

    // Futures contract settlement
    settleFutures(world, ctx.economy->market());

    // River flooding (seasonal)
    processFlooding(world, grid, static_cast<int32_t>(ctx.currentTurn));

    // Natural disasters + climate
    {
        float globalTemp = 14.0f;
        aoc::ecs::ComponentPool<GlobalClimateComponent>* climatePool =
            world.getPool<GlobalClimateComponent>();
        if (climatePool != nullptr && climatePool->size() > 0) {
            globalTemp = climatePool->data()[0].globalTemperature;
        }
        processNaturalDisasters(world, grid, static_cast<int32_t>(ctx.currentTurn), globalTemp);

        // Climate CO2 accumulation and temperature processing
        if (climatePool != nullptr) {
            for (uint32_t ci = 0; ci < climatePool->size(); ++ci) {
                GlobalClimateComponent& climate = climatePool->data()[ci];

                // Population CO2
                const aoc::ecs::ComponentPool<CityComponent>* cityPool3 =
                    world.getPool<CityComponent>();
                if (cityPool3 != nullptr) {
                    for (uint32_t cj = 0; cj < cityPool3->size(); ++cj) {
                        float co2PerCity = static_cast<float>(cityPool3->data()[cj].population) * 0.1f;
                        climate.addCO2(co2PerCity);
                    }
                }

                // Industrial pollution CO2
                climate.addCO2(static_cast<float>(totalIndustrialCO2(world)));
                climate.processTurn(grid, *ctx.rng);
            }
        }
    }

    // Energy dependency and peak oil tracking
    {
        // Update global oil reserves
        aoc::ecs::ComponentPool<GlobalOilReserves>* oilPool =
            world.getPool<GlobalOilReserves>();
        if (oilPool != nullptr && oilPool->size() > 0) {
            updateGlobalOilReserves(grid, oilPool->data()[0]);
        }

        // Per-player energy dependency
        aoc::ecs::ComponentPool<PlayerEnergyComponent>* energyPool =
            world.getPool<PlayerEnergyComponent>();
        if (energyPool != nullptr) {
            for (uint32_t ei = 0; ei < energyPool->size(); ++ei) {
                PlayerEnergyComponent& energy = energyPool->data()[ei];

                // Count oil consumed from stockpiles (tracked in production recipes)
                int32_t oilConsumed = 0;
                const aoc::ecs::ComponentPool<CityStockpileComponent>* stockPool =
                    world.getPool<CityStockpileComponent>();
                const aoc::ecs::ComponentPool<CityComponent>* cityPool2 =
                    world.getPool<CityComponent>();
                if (stockPool != nullptr && cityPool2 != nullptr) {
                    for (uint32_t si = 0; si < stockPool->size(); ++si) {
                        EntityId cityEnt = stockPool->entities()[si];
                        const CityComponent* city2 = world.tryGetComponent<CityComponent>(cityEnt);
                        if (city2 != nullptr && city2->owner == energy.owner) {
                            oilConsumed += stockPool->data()[si].getAmount(goods::OIL);
                        }
                    }
                }

                int32_t renewables = countRenewableBuildings(world, energy.owner);
                updateEnergyDependency(energy, oilConsumed, renewables);
                processOilShock(energy);
            }
        }
    }

    // Physical trade routes: move Traders, exchange goods
    processTradeRoutes(world, grid);

    // Stock market: dividends, value updates
    processStockMarket(world);

    // Trade agreements: tick durations
    processTradeAgreements(world);

    // Supply chain health: check import dependencies
    processSupplyChains(world);

    // Monopoly detection and pricing
    detectMonopolies(world, grid);
    applyMonopolyIncome(world);

    // Black market smuggling (for embargoed players)
    processBlackMarketTrade(world);

    // Speculation bubbles (per player)
    {
        aoc::ecs::ComponentPool<PlayerBubbleComponent>* bubblePool =
            world.getPool<PlayerBubbleComponent>();
        aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool3 =
            world.getPool<MonetaryStateComponent>();
        if (bubblePool != nullptr && monetaryPool3 != nullptr) {
            for (uint32_t bi = 0; bi < bubblePool->size(); ++bi) {
                PlayerBubbleComponent& bubble = bubblePool->data()[bi];
                // Find matching monetary state
                for (uint32_t mi = 0; mi < monetaryPool3->size(); ++mi) {
                    if (monetaryPool3->data()[mi].owner == bubble.owner) {
                        bool hasShock = false;
                        // Shock triggers: war, default, resource exhaustion
                        // Simplified: check if inflation spiked or treasury negative
                        if (monetaryPool3->data()[mi].inflationRate > 0.15f
                            || monetaryPool3->data()[mi].treasury < 0) {
                            hasShock = true;
                        }
                        processSpeculationBubble(bubble,
                            monetaryPool3->data()[mi].gdp,
                            monetaryPool3->data()[mi].interestRate,
                            hasShock);
                        break;
                    }
                }
            }
        }
    }

    // Per-player: unemployment and education
    for (PlayerId player : ctx.allPlayers) {
        processUnemployment(world, player);
        updateHumanCapital(world, player);
    }

    // World events (per player)
    for (PlayerId player : ctx.allPlayers) {
        checkWorldEvents(world, player, static_cast<int32_t>(ctx.currentTurn));
    }
    tickWorldEvents(world);

    // Victory tracking
    updateVictoryTrackers(world, grid, *ctx.economy, ctx.currentTurn);
}

// ============================================================================
// Main turn entry point
// ============================================================================

void processTurn(TurnContext& ctx) {
    // 1. AI decisions
    for (ai::AIController* ai : ctx.aiControllers) {
        if (ai != nullptr) {
            ai->executeTurn(*ctx.world, *ctx.grid, *ctx.diplomacy,
                           ctx.economy->market(), *ctx.rng);
        }
    }

    // 2. Economy simulation (harvest, produce, trade, market prices)
    ctx.economy->executeTurn(*ctx.world, *ctx.grid);

    // 3. Per-player processing (SAME logic for human AND AI)
    for (PlayerId player : ctx.allPlayers) {
        processPlayerTurn(ctx, player);
    }

    // 4. Global systems
    processGlobalSystems(ctx);

    ++ctx.currentTurn;
}

} // namespace aoc::sim
