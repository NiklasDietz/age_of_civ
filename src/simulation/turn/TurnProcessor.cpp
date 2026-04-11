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

// New object model (Phase 3 migration)
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"

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

    // Assign workers up to population count (center tile is free, doesn't consume a citizen)
    const int32_t maxWorkers = startingPop;
    int32_t assigned = 0;
    for (const TileScore& ts : tileScores) {
        if (assigned >= maxWorkers) { break; }
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
    assert(ctx.gameState != nullptr && "GameState is required for turn processing");

    aoc::ecs::World& world = *ctx.world;
    aoc::map::HexGrid& grid = *ctx.grid;
    aoc::game::Player* gsPlayer = ctx.gameState->player(player);
    assert(gsPlayer != nullptr);

    // Civilization meeting detection: check if any of our units/cities are near
    // another player's units/cities. Triggers MeetCivilization eureka.
    {
        const aoc::ecs::ComponentPool<UnitComponent>* unitPool =
            world.getPool<UnitComponent>();
        const aoc::ecs::ComponentPool<CityComponent>* cityPool =
            world.getPool<CityComponent>();
        if (unitPool != nullptr && cityPool != nullptr) {
            bool metAnotherCiv = false;
            // Check if any of our units are within 4 hexes of a foreign city
            for (uint32_t ui = 0; ui < unitPool->size() && !metAnotherCiv; ++ui) {
                if (unitPool->data()[ui].owner != player) { continue; }
                for (uint32_t ci = 0; ci < cityPool->size() && !metAnotherCiv; ++ci) {
                    if (cityPool->data()[ci].owner == player) { continue; }
                    if (aoc::hex::distance(unitPool->data()[ui].position,
                                           cityPool->data()[ci].location) <= 4) {
                        metAnotherCiv = true;
                    }
                }
            }
            // Also check if any foreign units are near our cities
            for (uint32_t ci = 0; ci < cityPool->size() && !metAnotherCiv; ++ci) {
                if (cityPool->data()[ci].owner != player) { continue; }
                for (uint32_t ui = 0; ui < unitPool->size() && !metAnotherCiv; ++ui) {
                    if (unitPool->data()[ui].owner == player) { continue; }
                    if (aoc::hex::distance(unitPool->data()[ui].position,
                                           cityPool->data()[ci].location) <= 4) {
                        metAnotherCiv = true;
                    }
                }
            }
            if (metAnotherCiv) {
                checkEurekaConditions(world, player, EurekaCondition::MeetCivilization);
            }
        }
    }

    // Trigger FoundCity eureka (fires once when player has 2+ cities)
    if (gsPlayer->cityCount() >= 2) {
        checkEurekaConditions(world, player, EurekaCondition::FoundCity);
    }

    // Gold income: reads from Player/City objects, writes to Player::treasury()
    processGoldIncome(*gsPlayer, grid);

    // Maintenance: per-unit era-scaled, per-building, per-district, per-city
    processUnitMaintenance(*gsPlayer);
    processBuildingMaintenance(*gsPlayer);

    // Sync treasury back to ECS so downstream ECS-based systems see correct values
    world.forEach<PlayerEconomyComponent>(
        [player, gsPlayer](EntityId, PlayerEconomyComponent& ec) {
            if (ec.owner == player) {
                ec.treasury = gsPlayer->treasury();
                ec.incomePerTurn = gsPlayer->incomePerTurn();
            }
        });

    // City connections (still ECS-based)
    processCityConnections(world, grid, player);

    // Advanced economics (tariffs, banking, debt -- still ECS-based)
    processAdvancedEconomics(world, grid, player, ctx.economy->market());

    // War weariness
    processWarWeariness(world, player, *ctx.diplomacy);

    // Golden/Dark age effects
    processAgeEffects(world, player);

    // City growth: GameState-native
    processCityGrowth(*gsPlayer, grid);

    // Sync city population/food changes back to ECS for happiness/loyalty
    {
        aoc::ecs::ComponentPool<CityComponent>* cityPool =
            world.getPool<CityComponent>();
        if (cityPool != nullptr) {
            for (const std::unique_ptr<aoc::game::City>& gsCity : gsPlayer->cities()) {
                for (uint32_t ci = 0; ci < cityPool->size(); ++ci) {
                    CityComponent& ecsCity = cityPool->data()[ci];
                    if (ecsCity.owner == player && ecsCity.location == gsCity->location()) {
                        ecsCity.population = gsCity->population();
                        ecsCity.foodSurplus = gsCity->foodSurplus();
                        ecsCity.workedTiles = gsCity->workedTiles();
                        break;
                    }
                }
            }
        }
    }

    // City happiness (still ECS-based, reads CityComponent population)
    computeCityHappiness(world, player);

    // City loyalty (still ECS-based, reads happiness values)
    computeCityLoyalty(world, grid, player);

    // Government processing (anarchy ticks, active action ticks)
    processGovernment(world, player);

    // Religion
    accumulateFaith(world, grid, player);
    applyReligionBonuses(world, player);

    // Science + tech research: GameState-native
    {
        float science = computePlayerScience(*gsPlayer, grid);
        float culture = computePlayerCulture(*gsPlayer, grid);

        advanceResearch(gsPlayer->tech(), science);
        advanceCivicResearch(gsPlayer->civics(), culture, &gsPlayer->government());

        // Sync tech/civic state back to ECS
        world.forEach<PlayerTechComponent>(
            [player, gsPlayer](aoc::EntityId, PlayerTechComponent& tech) {
                if (tech.owner == player) {
                    tech = gsPlayer->tech();
                }
            });
        world.forEach<PlayerCivicComponent>(
            [player, gsPlayer](aoc::EntityId, PlayerCivicComponent& civic) {
                if (civic.owner == player) {
                    civic = gsPlayer->civics();
                }
            });
    }

    // Production queues (still ECS-based)
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

    // 2b. Sync ECS -> GameState before per-player processing.
    // AI decisions and economy simulation may have created new cities/units in ECS.
    if (ctx.gameState != nullptr) {
        syncEcsToGameState(ctx);
    }

    // 3. Per-player processing (uses GameState for migrated subsystems)
    for (PlayerId player : ctx.allPlayers) {
        processPlayerTurn(ctx, player);
    }

    // 4. Global systems (still ECS-based)
    processGlobalSystems(ctx);

    // 5. Final sync: pick up any state changes from global systems and
    //    write-back results from GameState-native processing.
    if (ctx.gameState != nullptr) {
        syncEcsToGameState(ctx);
    }

    ++ctx.currentTurn;
}

// ============================================================================
// ECS <-> GameState synchronization (Phase 3 migration bridge)
// ============================================================================

void syncEcsToGameState(TurnContext& ctx) {
    if (ctx.gameState == nullptr || ctx.world == nullptr) {
        return;
    }

    aoc::ecs::World& world = *ctx.world;
    aoc::game::GameState& gs = *ctx.gameState;

    // Sync player-level economy state
    const aoc::ecs::ComponentPool<PlayerEconomyComponent>* econPool =
        world.getPool<PlayerEconomyComponent>();
    if (econPool != nullptr) {
        for (uint32_t i = 0; i < econPool->size(); ++i) {
            const PlayerEconomyComponent& econ = econPool->data()[i];
            aoc::game::Player* player = gs.player(econ.owner);
            if (player == nullptr) { continue; }

            player->setTreasury(econ.treasury);
            player->setIncomePerTurn(econ.incomePerTurn);
        }
    }

    // Sync player-level monetary state
    const aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool != nullptr) {
        for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
            const MonetaryStateComponent& mon = monetaryPool->data()[i];
            aoc::game::Player* player = gs.player(mon.owner);
            if (player == nullptr) { continue; }

            player->monetary() = mon;
        }
    }

    // Sync player-level tech state
    const aoc::ecs::ComponentPool<PlayerTechComponent>* techPool =
        world.getPool<PlayerTechComponent>();
    if (techPool != nullptr) {
        for (uint32_t i = 0; i < techPool->size(); ++i) {
            const PlayerTechComponent& tech = techPool->data()[i];
            aoc::game::Player* player = gs.player(tech.owner);
            if (player == nullptr) { continue; }

            player->tech() = tech;
        }
    }

    // Sync player-level civic state
    const aoc::ecs::ComponentPool<PlayerCivicComponent>* civicPool =
        world.getPool<PlayerCivicComponent>();
    if (civicPool != nullptr) {
        for (uint32_t i = 0; i < civicPool->size(); ++i) {
            const PlayerCivicComponent& civic = civicPool->data()[i];
            aoc::game::Player* player = gs.player(civic.owner);
            if (player == nullptr) { continue; }

            player->civics() = civic;
        }
    }

    // Sync player-level government state
    const aoc::ecs::ComponentPool<PlayerGovernmentComponent>* govPool =
        world.getPool<PlayerGovernmentComponent>();
    if (govPool != nullptr) {
        for (uint32_t i = 0; i < govPool->size(); ++i) {
            const PlayerGovernmentComponent& gov = govPool->data()[i];
            aoc::game::Player* player = gs.player(gov.owner);
            if (player == nullptr) { continue; }

            player->government() = gov;
        }
    }

    // Sync player-level faith state
    const aoc::ecs::ComponentPool<PlayerFaithComponent>* faithPool =
        world.getPool<PlayerFaithComponent>();
    if (faithPool != nullptr) {
        for (uint32_t i = 0; i < faithPool->size(); ++i) {
            const PlayerFaithComponent& faith = faithPool->data()[i];
            aoc::game::Player* player = gs.player(faith.owner);
            if (player == nullptr) { continue; }

            player->faith() = faith;
        }
    }

    // Sync city-level state: match ECS cities to GameState cities by location.
    // If a city exists in ECS but not in GameState, create it (AI founded a new city).
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            const CityComponent& ecsCity = cityPool->data()[i];
            aoc::game::Player* player = gs.player(ecsCity.owner);
            if (player == nullptr) { continue; }

            aoc::game::City* gsCity = player->cityAt(ecsCity.location);
            if (gsCity == nullptr) {
                // City was founded via ECS (AI settler) -- mirror it into GameState
                aoc::game::City& newCity = player->addCity(ecsCity.location, ecsCity.name);
                newCity.setOriginalCapital(ecsCity.isOriginalCapital);
                newCity.setOriginalOwner(ecsCity.originalOwner);
                gsCity = &newCity;
            }

            gsCity->setPopulation(ecsCity.population);
            gsCity->setFoodSurplus(ecsCity.foodSurplus);

            // Sync worked tiles
            gsCity->workedTiles() = ecsCity.workedTiles;

            // Sync districts
            EntityId cityEntity = cityPool->entities()[i];
            const CityDistrictsComponent* districts =
                world.tryGetComponent<CityDistrictsComponent>(cityEntity);
            if (districts != nullptr) {
                gsCity->districts() = *districts;
            }

            // Sync production queue
            const ProductionQueueComponent* prodQueue =
                world.tryGetComponent<ProductionQueueComponent>(cityEntity);
            if (prodQueue != nullptr) {
                gsCity->production() = *prodQueue;
            }

            // Sync happiness
            const CityHappinessComponent* happiness =
                world.tryGetComponent<CityHappinessComponent>(cityEntity);
            if (happiness != nullptr) {
                gsCity->happiness() = *happiness;
            }

            // Sync loyalty
            const CityLoyaltyComponent* loyalty =
                world.tryGetComponent<CityLoyaltyComponent>(cityEntity);
            if (loyalty != nullptr) {
                gsCity->loyalty() = *loyalty;
            }

            // Sync religion
            const CityReligionComponent* religion =
                world.tryGetComponent<CityReligionComponent>(cityEntity);
            if (religion != nullptr) {
                gsCity->religion() = *religion;
            }

            // Sync stockpile
            const CityStockpileComponent* stockpile =
                world.tryGetComponent<CityStockpileComponent>(cityEntity);
            if (stockpile != nullptr) {
                gsCity->stockpile() = *stockpile;
            }
        }
    }

    // Sync unit-level state: match ECS units to GameState units by position.
    // Also handle units that were produced/destroyed via ECS during the turn.

    // First, collect all ECS unit positions per player to detect removed units
    const aoc::ecs::ComponentPool<UnitComponent>* unitPool =
        world.getPool<UnitComponent>();
    if (unitPool != nullptr) {
        // Remove GameState units that no longer exist in ECS
        for (int32_t pi = 0; pi < gs.playerCount(); ++pi) {
            aoc::game::Player* player = gs.player(static_cast<PlayerId>(pi));
            if (player == nullptr) { continue; }

            std::vector<aoc::game::Unit*> toRemove;
            for (const std::unique_ptr<aoc::game::Unit>& gsUnit : player->units()) {
                bool foundInEcs = false;
                for (uint32_t ui = 0; ui < unitPool->size(); ++ui) {
                    const UnitComponent& eu = unitPool->data()[ui];
                    if (eu.owner == player->id() && eu.position == gsUnit->position()
                        && eu.typeId == gsUnit->typeId()) {
                        foundInEcs = true;
                        break;
                    }
                }
                if (!foundInEcs) {
                    toRemove.push_back(gsUnit.get());
                }
            }
            for (aoc::game::Unit* dead : toRemove) {
                player->removeUnit(dead);
            }
        }

        // Now sync existing and create new units
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            const UnitComponent& ecsUnit = unitPool->data()[i];
            aoc::game::Player* player = gs.player(ecsUnit.owner);
            if (player == nullptr) { continue; }

            // Find matching unit in GameState (by position + type)
            aoc::game::Unit* gsUnit = nullptr;
            for (const std::unique_ptr<aoc::game::Unit>& u : player->units()) {
                if (u->position() == ecsUnit.position && u->typeId() == ecsUnit.typeId) {
                    gsUnit = u.get();
                    break;
                }
            }
            if (gsUnit == nullptr) {
                // Unit was produced via ECS (production queue) -- mirror into GameState
                aoc::game::Unit& newUnit = player->addUnit(ecsUnit.typeId, ecsUnit.position);
                gsUnit = &newUnit;
            }

            gsUnit->setHitPoints(ecsUnit.hitPoints);
            gsUnit->setMovementRemaining(ecsUnit.movementRemaining);
            gsUnit->setState(ecsUnit.state);
            gsUnit->pendingPath() = ecsUnit.pendingPath;
            gsUnit->autoExplore = ecsUnit.autoExplore;
            gsUnit->autoImprove = ecsUnit.autoImprove;
            gsUnit->isAnimating = ecsUnit.isAnimating;
            gsUnit->animProgress = ecsUnit.animProgress;
            gsUnit->animFrom = ecsUnit.animFrom;
            gsUnit->animTo = ecsUnit.animTo;
        }
    }
}

void syncGameStateToEcs(TurnContext& ctx) {
    if (ctx.gameState == nullptr || ctx.world == nullptr) {
        return;
    }

    aoc::ecs::World& world = *ctx.world;
    aoc::game::GameState& gs = *ctx.gameState;

    // Sync player economy back to ECS
    aoc::ecs::ComponentPool<PlayerEconomyComponent>* econPool =
        world.getPool<PlayerEconomyComponent>();
    if (econPool != nullptr) {
        for (uint32_t i = 0; i < econPool->size(); ++i) {
            PlayerEconomyComponent& econ = econPool->data()[i];
            const aoc::game::Player* player = gs.player(econ.owner);
            if (player == nullptr) { continue; }

            econ.treasury = player->treasury();
            econ.incomePerTurn = player->incomePerTurn();
        }
    }

    // Sync city state back to ECS
    aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            CityComponent& ecsCity = cityPool->data()[i];
            const aoc::game::Player* player = gs.player(ecsCity.owner);
            if (player == nullptr) { continue; }

            const aoc::game::City* gsCity = player->cityAt(ecsCity.location);
            if (gsCity == nullptr) { continue; }

            ecsCity.population = gsCity->population();
            ecsCity.foodSurplus = gsCity->foodSurplus();
            ecsCity.workedTiles = gsCity->workedTiles();
        }
    }

    // Sync unit state back to ECS
    aoc::ecs::ComponentPool<UnitComponent>* unitPool =
        world.getPool<UnitComponent>();
    if (unitPool != nullptr) {
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            UnitComponent& ecsUnit = unitPool->data()[i];
            const aoc::game::Player* player = gs.player(ecsUnit.owner);
            if (player == nullptr) { continue; }

            const aoc::game::Unit* gsUnit = player->unitAt(ecsUnit.position);
            if (gsUnit == nullptr) { continue; }

            ecsUnit.hitPoints = gsUnit->hitPoints();
            ecsUnit.movementRemaining = gsUnit->movementRemaining();
            ecsUnit.state = gsUnit->state();
            ecsUnit.pendingPath = gsUnit->pendingPath();
        }
    }
}

} // namespace aoc::sim
