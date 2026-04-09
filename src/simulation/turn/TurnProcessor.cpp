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

// AI
#include "aoc/simulation/ai/AIController.hpp"

// Monetary
#include "aoc/simulation/monetary/MonetarySystem.hpp"

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

    // Auto-assign nearby tiles
    std::array<aoc::hex::AxialCoord, 6> neighbors = aoc::hex::neighbors(location);
    for (int32_t n = 0; n < 3 && n < 6; ++n) {
        if (grid.isValid(neighbors[static_cast<std::size_t>(n)])) {
            city.workedTiles.push_back(neighbors[static_cast<std::size_t>(n)]);
        }
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

    // Natural disasters
    float globalTemp = 14.0f;  // Default; would read from climate component
    const aoc::ecs::ComponentPool<GlobalClimateComponent>* climatePool =
        world.getPool<GlobalClimateComponent>();
    if (climatePool != nullptr && climatePool->size() > 0) {
        globalTemp = climatePool->data()[0].globalTemperature;
    }
    processNaturalDisasters(world, grid, static_cast<int32_t>(ctx.currentTurn), globalTemp);

    // Climate / CO2
    if (climatePool != nullptr) {
        for (uint32_t ci = 0; ci < climatePool->size(); ++ci) {
            // Population CO2
            const aoc::ecs::ComponentPool<CityComponent>* cityPool =
                world.getPool<CityComponent>();
            if (cityPool != nullptr) {
                for (uint32_t cj = 0; cj < cityPool->size(); ++cj) {
                    float co2PerCity = static_cast<float>(cityPool->data()[cj].population) * 0.1f;
                    const_cast<GlobalClimateComponent&>(climatePool->data()[ci]).addCO2(co2PerCity);
                }
            }
            // Industrial pollution CO2
            int32_t industrialCO2 = totalIndustrialCO2(world);
            const_cast<GlobalClimateComponent&>(climatePool->data()[ci]).addCO2(
                static_cast<float>(industrialCO2));
            const_cast<GlobalClimateComponent&>(climatePool->data()[ci]).processTurn(
                grid, *ctx.rng);
        }
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
