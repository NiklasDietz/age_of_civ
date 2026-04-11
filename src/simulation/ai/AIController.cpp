/**
 * @file AIController.cpp
 * @brief Top-level AI orchestration and city/diplomacy/economy management.
 *
 * Delegates research, settlers, builders, and military to focused subsystem
 * controllers. Retains city production queue management, diplomacy, economy,
 * monetary system, and government logic.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/ai/AIController.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/ai/AIEconomicStrategy.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/ai/UtilityScoring.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ecs/World.hpp"

#include <unordered_set>

namespace aoc::sim::ai {

// ============================================================================
// Helper: Find the best military unit type ID the player can produce.
// ============================================================================

static UnitTypeId bestAvailableMilitaryUnit(const aoc::game::GameState& gameState,
                                            PlayerId player) {
    aoc::ecs::World& world = gameState.legacyWorld();
    UnitTypeId bestId{0};
    aoc::ecs::World& world = gameState.legacyWorld();
    int32_t bestStrength = 0;

    for (const UnitTypeDef& def : UNIT_TYPE_DEFS) {
        aoc::ecs::World& world = gameState.legacyWorld();
        if (!isMilitary(def.unitClass) || isNaval(def.unitClass)) {
            continue;
        }
        if (!canBuildUnit(world, player, def.id)) {
            continue;
        }
        const int32_t strength = def.combatStrength + def.rangedStrength;
        if (strength > bestStrength) {
            bestStrength = strength;
            bestId = def.id;
        }
    }

    return bestId;
}

// ============================================================================
// Helper: Count units by class for a player.
// ============================================================================

struct UnitCounts {
    int32_t military  = 0;
    int32_t builders  = 0;
    int32_t settlers  = 0;
    int32_t scouts    = 0;
    int32_t traders   = 0;
    int32_t total     = 0;
};

static UnitCounts countPlayerUnits(const aoc::game::GameState& gameState, PlayerId player) {
    UnitCounts counts{};
    aoc::ecs::World& world = gameState.legacyWorld();
    const aoc::ecs::ComponentPool<UnitComponent>* unitPool =
        world.getPool<UnitComponent>();
    if (unitPool == nullptr) {
        return counts;
    }
    for (uint32_t i = 0; i < unitPool->size(); ++i) {
        if (unitPool->data()[i].owner != player) {
            continue;
        }
        ++counts.total;
        const UnitTypeDef& def = unitTypeDef(unitPool->data()[i].typeId);
        if (isMilitary(def.unitClass)) {
            ++counts.military;
        }
        if (def.unitClass == UnitClass::Civilian) {
            ++counts.builders;
        }
        if (def.unitClass == UnitClass::Settler) {
            ++counts.settlers;
        }
        if (def.unitClass == UnitClass::Scout) {
            ++counts.scouts;
        }
        if (def.unitClass == UnitClass::Trader) {
            ++counts.traders;
        }
    }
    return counts;
}

// ============================================================================
// Constructor
// ============================================================================

AIController::AIController(PlayerId player, aoc::ui::AIDifficulty difficulty)
    : m_player(player)
    , m_difficulty(difficulty)
    , m_researchPlanner(player, difficulty)
    , m_settlerController(player, difficulty)
    , m_builderController(player, difficulty)
    , m_militaryController(player, difficulty)
{
}

// ============================================================================
// Main turn execution
// ============================================================================

void AIController::executeTurn(aoc::game::GameState& gameState,
                                aoc::map::HexGrid& grid,
                                DiplomacyManager& diplomacy,
                                const Market& market,
                                aoc::Random& rng) {
    aoc::ecs::World& world = gameState.legacyWorld();
    this->manageGovernment(world);
    this->m_researchPlanner.selectResearch(world);
    this->executeCityActions(world, grid);
    this->m_builderController.manageBuildersAndImprovements(world, grid);
    this->m_settlerController.executeSettlerActions(world, grid);
    this->m_militaryController.executeMilitaryActions(world, grid, rng);
    this->manageEconomy(world, diplomacy, market);
    this->manageMonetarySystem(world, diplomacy);
    aoc::sim::aiEconomicStrategy(world, grid, market, diplomacy, this->m_player,
                                  static_cast<int32_t>(this->m_difficulty));
    this->executeDiplomacyActions(world, diplomacy, market);
    this->manageTradeRoutes(world, grid, market, diplomacy);

    refreshMovement(world, this->m_player);
}

// ============================================================================
// City actions: priority-based production queue management
//
// Priority order per city:
//   1. Settler if < N cities and pop > threshold and no settler already
//   2. Builder if unimproved tiles and no builders
//   3. Military unit if total military < cities * M + 2
//   4. District or building -- pick most valuable
//   5. Default: best military unit
// ============================================================================

void AIController::executeCityActions(aoc::game::GameState& gameState,
                                       aoc::map::HexGrid& grid) {
    aoc::ecs::World& world = gameState.legacyWorld();
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    const UnitCounts unitCounts = countPlayerUnits(world, this->m_player);

    int32_t ownedCityCount = 0;
    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        if (cityPool->data()[i].owner == this->m_player) {
            ++ownedCityCount;
        }
    }

    // Check if any owned tile near cities lacks improvements
    bool needsBuilder = false;
    if (unitCounts.builders == 0) {
        for (uint32_t ci = 0; ci < cityPool->size(); ++ci) {
            const CityComponent& city = cityPool->data()[ci];
            if (city.owner != this->m_player) {
                continue;
            }
            const std::array<hex::AxialCoord, 6> cityNeighbors = hex::neighbors(city.location);
            for (const hex::AxialCoord& nbr : cityNeighbors) {
                if (!grid.isValid(nbr)) {
                    continue;
                }
                const int32_t idx = grid.toIndex(nbr);
                if (grid.owner(idx) == this->m_player &&
                    grid.improvement(idx) == aoc::map::ImprovementType::None &&
                    grid.movementCost(idx) > 0) {
                    needsBuilder = true;
                    break;
                }
            }
            if (needsBuilder) {
                break;
            }
        }
    }

    // Get leader personality for this AI's civilization
    CivId myCivId = 0;
    const aoc::ecs::ComponentPool<PlayerCivilizationComponent>* civPool =
        world.getPool<PlayerCivilizationComponent>();
    if (civPool != nullptr) {
        for (uint32_t ci = 0; ci < civPool->size(); ++ci) {
            if (civPool->data()[ci].owner == this->m_player) {
                myCivId = civPool->data()[ci].civId;
                break;
            }
        }
    }
    const LeaderPersonalityDef& personality = leaderPersonality(myCivId);
    const AIScaledTargets targets = computeScaledTargets(personality.behavior);

    const int32_t desiredMilitary = ownedCityCount * targets.desiredMilitaryPerCity + 2;
    const bool needsMilitary = unitCounts.military < desiredMilitary;
    const bool needsSettler = ownedCityCount < targets.maxCities && unitCounts.settlers == 0;

    const UnitTypeId bestMilitaryId = bestAvailableMilitaryUnit(world, this->m_player);
    const UnitTypeDef& bestMilitaryDef = unitTypeDef(bestMilitaryId);

    bool settlerEnqueued = false;
    bool builderEnqueued = false;
    int32_t militaryProducers = 0;
    const int32_t maxMilitaryProducers = std::max(0, ownedCityCount / 2 - 1);
    int32_t traderProducers = 0;
    // Traders require Foreign Trade civic (CivicId{2}) -- one of the first civics
    bool hasForeignTrade = false;
    world.forEach<PlayerCivicComponent>(
        [this, &hasForeignTrade](EntityId, const PlayerCivicComponent& civic) {
            if (civic.owner == this->m_player && civic.hasCompleted(CivicId{2})) {
                hasForeignTrade = true;
            }
        });
    const bool needsTrader = hasForeignTrade
        && unitCounts.traders < std::min(ownedCityCount, 3);

    // Check if player has any coins
    bool playerHasCoins = false;
    const aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool != nullptr) {
        for (uint32_t mi = 0; mi < monetaryPool->size(); ++mi) {
            if (monetaryPool->data()[mi].owner == this->m_player) {
                playerHasCoins = monetaryPool->data()[mi].totalCoinCount() > 0;
                break;
            }
        }
    }

    for (uint32_t ci = 0; ci < cityPool->size(); ++ci) {
        CityComponent& city = cityPool->data()[ci];
        if (city.owner != this->m_player) {
            continue;
        }
        const EntityId cityEntity = cityPool->entities()[ci];
        ProductionQueueComponent* queue =
            world.tryGetComponent<ProductionQueueComponent>(cityEntity);
        if (queue == nullptr || !queue->isEmpty()) {
            continue;
        }

        // Easy AI: 30% chance to skip city action entirely
        if (this->m_difficulty == aoc::ui::AIDifficulty::Easy) {
            const uint32_t pseudoRand = (ci * 7919u + 31u) % 100u;
            if (pseudoRand < 30u) {
                continue;
            }
        }

        ProductionQueueItem item{};

        // EMERGENCY: Check for enemy military units within 5 hexes of this city.
        // If threatened, immediately produce a military unit regardless of other priorities.
        bool emergencyThreat = false;
        const aoc::ecs::ComponentPool<UnitComponent>* allUnits = world.getPool<UnitComponent>();
        if (bestMilitaryId.isValid() && allUnits != nullptr) {
            for (uint32_t ui = 0; ui < allUnits->size(); ++ui) {
                const UnitComponent& enemyUnit = allUnits->data()[ui];
                if (enemyUnit.owner == this->m_player) { continue; }
                if (!isMilitary(unitTypeDef(enemyUnit.typeId).unitClass)) { continue; }
                if (aoc::hex::distance(enemyUnit.position, city.location) <= 5) {
                    emergencyThreat = true;
                    break;
                }
            }
        }
        if (emergencyThreat && unitCounts.military < ownedCityCount) {
            item.type = ProductionItemType::Unit;
            item.itemId = bestMilitaryId.value;
            item.name = std::string(bestMilitaryDef.name);
            item.totalCost = static_cast<float>(bestMilitaryDef.productionCost);
            item.progress = 0.0f;
            ++militaryProducers;
            LOG_INFO("AI %u EMERGENCY: Enqueued %.*s in %s (enemy nearby!)",
                     static_cast<unsigned>(this->m_player),
                     static_cast<int>(bestMilitaryDef.name.size()),
                     bestMilitaryDef.name.data(),
                     city.name.c_str());
        }

        // Priority 0: Military defense - build if we have fewer than half the desired military
        bool criticalMilitaryNeed = item.name.empty()
            && (unitCounts.military < desiredMilitary / 2)
            && bestMilitaryId.isValid() && militaryProducers == 0;
        if (criticalMilitaryNeed) {
            item.type = ProductionItemType::Unit;
            item.itemId = bestMilitaryId.value;
            item.name = std::string(bestMilitaryDef.name);
            item.totalCost = static_cast<float>(bestMilitaryDef.productionCost);
            item.progress = 0.0f;
            ++militaryProducers;
            LOG_INFO("AI %u Enqueued %.*s in %s (defense priority)",
                     static_cast<unsigned>(this->m_player),
                     static_cast<int>(bestMilitaryDef.name.size()),
                     bestMilitaryDef.name.data(),
                     city.name.c_str());
        }
        // Priority 1: Industrial district in capital (enables production chain).
        // This is the foundation of the economy -- Forge/Workshop can't be built without it.
        else if (city.isOriginalCapital) {
            const CityDistrictsComponent* capDistricts =
                world.tryGetComponent<CityDistrictsComponent>(cityEntity);
            bool capHasIndustrial = (capDistricts != nullptr
                && capDistricts->hasDistrict(DistrictType::Industrial));
            bool capHasForge = (capDistricts != nullptr
                && capDistricts->hasBuilding(BuildingId{0}));
            bool capHasWorkshop = (capDistricts != nullptr
                && capDistricts->hasBuilding(BuildingId{1}));

            if (!capHasIndustrial) {
                item.type = ProductionItemType::District;
                item.itemId = static_cast<uint16_t>(DistrictType::Industrial);
                item.name = "Industrial Zone";
                item.totalCost = 60.0f;
                item.progress = 0.0f;
                LOG_INFO("AI %u Enqueued Industrial Zone in %s (capital priority)",
                         static_cast<unsigned>(this->m_player), city.name.c_str());
            } else if (!capHasForge) {
                item.type = ProductionItemType::Building;
                item.itemId = 0;
                item.name = "Forge";
                item.totalCost = 60.0f;
                item.progress = 0.0f;
                LOG_INFO("AI %u Enqueued Forge in %s (capital priority)",
                         static_cast<unsigned>(this->m_player), city.name.c_str());
            } else if (!capHasWorkshop) {
                item.type = ProductionItemType::Building;
                item.itemId = 1;
                item.name = "Workshop";
                item.totalCost = 40.0f;
                item.progress = 0.0f;
                LOG_INFO("AI %u Enqueued Workshop in %s (capital priority)",
                         static_cast<unsigned>(this->m_player), city.name.c_str());
            }
            // If capital already has all three, fall through to next priority
        }
        // Priority 2: First Trader (enables trade routes)
        if (item.name.empty() && traderProducers == 0 && needsTrader && unitCounts.traders == 0) {
            item.type = ProductionItemType::Unit;
            item.itemId = 30;  // Trader
            item.name = "Trader";
            item.totalCost = static_cast<float>(unitTypeDef(UnitTypeId{30}).productionCost);
            item.progress = 0.0f;
            ++traderProducers;
            LOG_INFO("AI %u Enqueued Trader in %s (first trader priority)",
                     static_cast<unsigned>(this->m_player), city.name.c_str());
        }
        // Priority 3: Settler
        if (item.name.empty() && needsSettler && !settlerEnqueued && city.population >= targets.settlePopThreshold) {
            item.type = ProductionItemType::Unit;
            item.itemId = 3;
            item.name = "Settler";
            item.totalCost = static_cast<float>(unitTypeDef(UnitTypeId{3}).productionCost);
            item.progress = 0.0f;
            settlerEnqueued = true;
            LOG_INFO("AI %u Enqueued Settler in %s (pop %d)",
                     static_cast<unsigned>(this->m_player),
                     city.name.c_str(), city.population);
        }
        // Priority 4: Builder
        if (item.name.empty() && needsBuilder && !builderEnqueued) {
            item.type = ProductionItemType::Unit;
            item.itemId = 5;
            item.name = "Builder";
            item.totalCost = static_cast<float>(unitTypeDef(UnitTypeId{5}).productionCost);
            item.progress = 0.0f;
            builderEnqueued = true;
            LOG_INFO("AI %u Enqueued Builder in %s",
                     static_cast<unsigned>(this->m_player), city.name.c_str());
        }
        // Priority 5: Additional Traders (one per city, max 3)
        if (item.name.empty() && traderProducers == 0 && needsTrader) {
            item.type = ProductionItemType::Unit;
            item.itemId = 30;
            item.name = "Trader";
            item.totalCost = static_cast<float>(unitTypeDef(UnitTypeId{30}).productionCost);
            item.progress = 0.0f;
            ++traderProducers;
            LOG_INFO("AI %u Enqueued Trader in %s",
                     static_cast<unsigned>(this->m_player), city.name.c_str());
        }
        // Priority 6: Military units (limited to half of cities)
        if (item.name.empty() && needsMilitary && militaryProducers < maxMilitaryProducers) {
            item.type = ProductionItemType::Unit;
            item.itemId = bestMilitaryId.value;
            item.name = std::string(bestMilitaryDef.name);
            item.totalCost = static_cast<float>(bestMilitaryDef.productionCost);
            item.progress = 0.0f;
            ++militaryProducers;
            LOG_INFO("AI %u Enqueued %.*s in %s",
                     static_cast<unsigned>(this->m_player),
                     static_cast<int>(bestMilitaryDef.name.size()),
                     bestMilitaryDef.name.data(),
                     city.name.c_str());
        }
        // Priority 7+8: Districts, then Buildings, then Fallback
        if (item.name.empty()) {
            bool enqueuedSomething = false;

            // --- 4a: Districts the city is missing ---
            const CityDistrictsComponent* existingDistricts =
                world.tryGetComponent<CityDistrictsComponent>(cityEntity);

            // Check if city is coastal (any neighbor is water)
            bool isCityCoastal = false;
            {
                std::array<aoc::hex::AxialCoord, 6> cityNbrs = aoc::hex::neighbors(city.location);
                for (const aoc::hex::AxialCoord& cn : cityNbrs) {
                    if (grid.isValid(cn) && aoc::map::isWater(grid.terrain(grid.toIndex(cn)))) {
                        isCityCoastal = true;
                        break;
                    }
                }
            }

            // District priority: Industrial first, then Harbor (for coastal cities),
            // then Commercial, then Campus.
            DistrictType districtPriority[5];
            int32_t districtCosts[5];
            bool hasIndustrial = (existingDistricts != nullptr
                && existingDistricts->hasDistrict(DistrictType::Industrial));
            bool hasHarbor = (existingDistricts != nullptr
                && existingDistricts->hasDistrict(DistrictType::Harbor));

            if (!hasIndustrial) {
                districtPriority[0] = DistrictType::Industrial;  districtCosts[0] = 60;
                // Coastal cities get Harbor early (enables naval + sea trade)
                if (isCityCoastal && !hasHarbor) {
                    districtPriority[1] = DistrictType::Harbor;      districtCosts[1] = 70;
                    districtPriority[2] = DistrictType::Commercial;  districtCosts[2] = 60;
                } else {
                    districtPriority[1] = DistrictType::Commercial;  districtCosts[1] = 60;
                    districtPriority[2] = DistrictType::Harbor;      districtCosts[2] = 70;
                }
                districtPriority[3] = DistrictType::Campus;      districtCosts[3] = 55;
                districtPriority[4] = DistrictType::Encampment;  districtCosts[4] = 55;
            } else {
                if (isCityCoastal && !hasHarbor) {
                    districtPriority[0] = DistrictType::Harbor;      districtCosts[0] = 70;
                    districtPriority[1] = DistrictType::Commercial;  districtCosts[1] = 60;
                } else {
                    districtPriority[0] = DistrictType::Commercial;  districtCosts[0] = 60;
                    districtPriority[1] = DistrictType::Harbor;      districtCosts[1] = 70;
                }
                districtPriority[2] = DistrictType::Campus;      districtCosts[2] = 55;
                districtPriority[3] = DistrictType::Encampment;  districtCosts[3] = 55;
                districtPriority[4] = DistrictType::Industrial;  districtCosts[4] = 60;
            }

            int32_t totalBuildings = 0;
            if (existingDistricts != nullptr) {
                for (const CityDistrictsComponent::PlacedDistrict& d : existingDistricts->districts) {
                    totalBuildings += static_cast<int32_t>(d.buildings.size());
                }
            }
            int32_t existingDistrictCount = (existingDistricts != nullptr)
                ? static_cast<int32_t>(existingDistricts->districts.size()) : 0;

            bool shouldBuildDistrict = (existingDistrictCount <= 1)
                || (totalBuildings >= existingDistrictCount - 1);

            if (shouldBuildDistrict) {
                for (int32_t di = 0; di < 5 && !enqueuedSomething; ++di) {
                    DistrictType dtype = districtPriority[di];
                    bool alreadyHas = (existingDistricts != nullptr
                                       && existingDistricts->hasDistrict(dtype));
                    if (!alreadyHas) {
                        item.type = ProductionItemType::District;
                        item.itemId = static_cast<uint16_t>(dtype);
                        item.name = std::string(districtTypeName(dtype));
                        item.totalCost = static_cast<float>(districtCosts[di]);
                        item.progress = 0.0f;
                        enqueuedSomething = true;
                        LOG_INFO("AI %u Enqueued district %.*s in %s",
                                 static_cast<unsigned>(this->m_player),
                                 static_cast<int>(districtTypeName(dtype).size()),
                                 districtTypeName(dtype).data(),
                                 city.name.c_str());
                    }
                }
            }

            // --- 4b: Buildings scored by utility function ---
            if (!enqueuedSomething) {
                aoc::sim::AIContext aiCtx{};
                aiCtx.ownedCities = ownedCityCount;
                aiCtx.totalPopulation = 0;
                for (uint32_t cp = 0; cp < cityPool->size(); ++cp) {
                    if (cityPool->data()[cp].owner == this->m_player) {
                        aiCtx.totalPopulation += cityPool->data()[cp].population;
                    }
                }
                aiCtx.militaryUnits = unitCounts.military;
                aiCtx.builderUnits = unitCounts.builders;
                aiCtx.settlerUnits = unitCounts.settlers;
                aiCtx.isThreatened = unitCounts.military < 3;
                aiCtx.needsImprovements = needsBuilder;
                aiCtx.hasMint = (existingDistricts != nullptr && existingDistricts->hasBuilding(BuildingId{24}));
                aiCtx.hasCoins = playerHasCoins;
                aiCtx.hasCampus = (existingDistricts != nullptr && existingDistricts->hasDistrict(DistrictType::Campus));
                aiCtx.hasCommercial = (existingDistricts != nullptr && existingDistricts->hasDistrict(DistrictType::Commercial));
                aiCtx.targetMaxCities = targets.maxCities;
                aiCtx.desiredMilitary = desiredMilitary;

                float bestScore = -1.0f;
                BuildingId bestBid{0};
                for (uint16_t bidx = 0; bidx < static_cast<uint16_t>(BUILDING_DEFS.size()); ++bidx) {
                    const BuildingDef& bdef = BUILDING_DEFS[bidx];
                    if (!canBuildBuilding(world, this->m_player, cityEntity, bdef.id)) { continue; }
                    float score = scoreBuildingForLeader(personality.behavior, bdef.id, aiCtx);
                    if (score > bestScore) {
                        bestScore = score;
                        bestBid = bdef.id;
                    }
                }

                if (bestScore > 0.0f) {
                    const BuildingDef& bdef = BUILDING_DEFS[bestBid.value];
                    item.type = ProductionItemType::Building;
                    item.itemId = bdef.id.value;
                    item.name = std::string(bdef.name);
                    item.totalCost = static_cast<float>(bdef.productionCost);
                    item.progress = 0.0f;
                    enqueuedSomething = true;
                    LOG_INFO("AI %u Enqueued building %.*s in %s (score %.1f)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<int>(bdef.name.size()),
                             bdef.name.data(),
                             city.name.c_str(),
                             static_cast<double>(bestScore));
                }
            }

            // Try any building (fallback when utility scoring found nothing)
            if (!enqueuedSomething) {
                for (const BuildingDef& bdef : BUILDING_DEFS) {
                    if (!canBuildBuilding(world, this->m_player, cityEntity, bdef.id)) { continue; }
                    item.type = ProductionItemType::Building;
                    item.itemId = bdef.id.value;
                    item.name = std::string(bdef.name);
                    item.totalCost = static_cast<float>(bdef.productionCost);
                    item.progress = 0.0f;
                    enqueuedSomething = true;
                    break;
                }
            }

            // --- 4c: Fallback -- produce best military unit ---
            if (!enqueuedSomething) {
                item.type = ProductionItemType::Unit;
                item.itemId = bestMilitaryId.value;
                item.name = std::string(bestMilitaryDef.name);
                item.totalCost = static_cast<float>(bestMilitaryDef.productionCost);
                item.progress = 0.0f;
            }
        }

        item.totalCost *= aoc::sim::GamePace::instance().costMultiplier;
        queue->queue.push_back(std::move(item));
    }
}

// ============================================================================
// Diplomacy
// ============================================================================

void AIController::executeDiplomacyActions(aoc::game::GameState& gameState,
                                            DiplomacyManager& diplomacy,
                                            const Market& market) {
    aoc::ecs::World& world = gameState.legacyWorld();
    const aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool == nullptr) {
        return;
    }

    constexpr uint8_t MAX_PLAYER_COUNT = 16;
    std::array<int32_t, MAX_PLAYER_COUNT> militaryCounts{};
    for (uint32_t i = 0; i < unitPool->size(); ++i) {
        const PlayerId owner = unitPool->data()[i].owner;
        if (owner < MAX_PLAYER_COUNT &&
            isMilitary(unitTypeDef(unitPool->data()[i].typeId).unitClass)) {
            ++militaryCounts[static_cast<std::size_t>(owner)];
        }
    }

    const int32_t ourMilitary = militaryCounts[static_cast<std::size_t>(this->m_player)];
    const uint8_t playerCount = diplomacy.playerCount();

    for (uint8_t other = 0; other < playerCount; ++other) {
        if (other == this->m_player) {
            continue;
        }

        PairwiseRelation& rel = diplomacy.relation(this->m_player, other);
        const int32_t theirMilitary = militaryCounts[static_cast<std::size_t>(other)];
        const int32_t relationScore = rel.totalScore();

        if (rel.isAtWar) {
            if (theirMilitary > ourMilitary) {
                diplomacy.makePeace(this->m_player, other);
                LOG_INFO("AI %u Proposed peace with player %u (outmilitaried %d vs %d)",
                         static_cast<unsigned>(this->m_player),
                         static_cast<unsigned>(other),
                         ourMilitary, theirMilitary);
            }
        } else {
            const bool easyAI = (this->m_difficulty == aoc::ui::AIDifficulty::Easy);
            const bool hardAI = (this->m_difficulty == aoc::ui::AIDifficulty::Hard);

            const float militaryRatioThreshold = hardAI ? 1.2f : 1.5f;
            const int32_t relationThreshold = hardAI ? -10 : -20;
            const int32_t warChanceThreshold = hardAI ? 5 : 3;

            if (!easyAI && ourMilitary > 0 && theirMilitary >= 0 &&
                static_cast<float>(ourMilitary) > militaryRatioThreshold * static_cast<float>(theirMilitary) &&
                relationScore < relationThreshold) {
                const int32_t warChance =
                    ((ourMilitary * 7 + theirMilitary * 13 +
                      static_cast<int32_t>(this->m_player) * 31) % 10);
                if (warChance < warChanceThreshold) {
                    diplomacy.declareWar(this->m_player, other);
                    LOG_INFO("AI %u Declared war on player %u (military %d vs %d, relations %d)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             ourMilitary, theirMilitary, relationScore);
                }
            }

            if (!rel.hasOpenBorders && relationScore > 10) {
                diplomacy.grantOpenBorders(this->m_player, other);
                LOG_INFO("AI %u Opened borders with player %u (relations %d)",
                         static_cast<unsigned>(this->m_player),
                         static_cast<unsigned>(other), relationScore);
            }

            if (!rel.hasEconomicAlliance && relationScore > 20) {
                int32_t complementaryGoods = 0;
                const uint16_t totalGoods = market.goodsCount();
                for (uint16_t g = 0; g < totalGoods; ++g) {
                    const int32_t currentPrice = market.price(g);
                    const int32_t basePrice = goodDef(g).basePrice;
                    if (basePrice > 0) {
                        const float priceRatio = static_cast<float>(currentPrice) /
                                                 static_cast<float>(basePrice);
                        if (priceRatio > 1.3f || priceRatio < 0.7f) {
                            ++complementaryGoods;
                        }
                    }
                }
                if (complementaryGoods >= 3) {
                    diplomacy.formEconomicAlliance(this->m_player, other);
                    LOG_INFO("AI %u Formed economic alliance with player %u "
                             "(relations %d, %d complementary goods)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             relationScore, complementaryGoods);
                }
            }
        }
    }
}

// ============================================================================
// Economy management
// ============================================================================

void AIController::manageEconomy(aoc::game::GameState& gameState,
                                  DiplomacyManager& diplomacy,
                                  const Market& market) {
    aoc::ecs::World& world = gameState.legacyWorld();
    std::unordered_map<uint16_t, int32_t> totalStockpile;

    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        if (cityPool->data()[i].owner != this->m_player) {
            continue;
        }
        const EntityId cityEntity = cityPool->entities()[i];
        const CityStockpileComponent* stockpile =
            world.tryGetComponent<CityStockpileComponent>(cityEntity);
        if (stockpile == nullptr) {
            continue;
        }
        for (const std::pair<const uint16_t, int32_t>& entry : stockpile->goods) {
            totalStockpile[entry.first] += entry.second;
        }
    }

    constexpr float SELL_THRESHOLD = 1.5f;
    constexpr float BUY_THRESHOLD  = 0.7f;
    constexpr int32_t MIN_SURPLUS_TO_SELL = 3;

    struct TradeDesire {
        uint16_t goodId;
        int32_t  amount;
        bool     wantToSell;
    };
    std::vector<TradeDesire> desires;

    const uint16_t totalGoods = market.goodsCount();
    for (uint16_t g = 0; g < totalGoods; ++g) {
        const int32_t currentPrice = market.price(g);
        const GoodDef& def = goodDef(g);
        if (def.basePrice <= 0) {
            continue;
        }

        const float priceRatio = static_cast<float>(currentPrice) /
                                 static_cast<float>(def.basePrice);
        const int32_t held = totalStockpile[g];

        if (priceRatio > SELL_THRESHOLD && held > MIN_SURPLUS_TO_SELL) {
            const int32_t sellAmount = held / 2;
            if (sellAmount > 0) {
                desires.push_back({g, sellAmount, true});
            }
        }
        else if (priceRatio < BUY_THRESHOLD && held == 0) {
            desires.push_back({g, 2, false});
        }
    }

    if (desires.empty()) {
        return;
    }

    const uint8_t playerCount = diplomacy.playerCount();
    for (const TradeDesire& desire : desires) {
        if (!desire.wantToSell) {
            continue;
        }

        for (uint8_t other = 0; other < playerCount; ++other) {
            if (other == this->m_player) {
                continue;
            }
            const PairwiseRelation& rel = diplomacy.relation(this->m_player, other);
            if (rel.isAtWar || rel.hasEmbargo) {
                continue;
            }

            if (rel.totalScore() < -10) {
                continue;
            }

            int32_t partnerHoldings = 0;
            for (uint32_t ci = 0; ci < cityPool->size(); ++ci) {
                if (cityPool->data()[ci].owner != other) {
                    continue;
                }
                const CityStockpileComponent* partnerStockpile =
                    world.tryGetComponent<CityStockpileComponent>(cityPool->entities()[ci]);
                if (partnerStockpile != nullptr) {
                    partnerHoldings += partnerStockpile->getAmount(desire.goodId);
                }
            }

            if (partnerHoldings < 2) {
                LOG_INFO("AI %u wants to sell %d of good %u (price ratio %.2f) to player %u",
                         static_cast<unsigned>(this->m_player),
                         desire.amount,
                         static_cast<unsigned>(desire.goodId),
                         static_cast<double>(static_cast<float>(market.price(desire.goodId)) /
                                             static_cast<float>(goodDef(desire.goodId).basePrice)),
                         static_cast<unsigned>(other));

                diplomacy.addModifier(this->m_player, other,
                    RelationModifier{"Trade interest", 1, 10});
                break;
            }
        }
    }
}

// ============================================================================
// Government management
// ============================================================================

void AIController::manageGovernment(aoc::game::GameState& gameState) {
    world.forEach<PlayerGovernmentComponent>(
        [this](EntityId, PlayerGovernmentComponent& gov) {
            if (gov.owner != this->m_player) {
                return;
            }

            GovernmentType bestGov = gov.government;
            for (uint8_t g = 0; g < GOVERNMENT_COUNT; ++g) {
                GovernmentType gt = static_cast<GovernmentType>(g);
                if (gov.isGovernmentUnlocked(gt)) {
                    bestGov = gt;
                }
            }
            if (bestGov != gov.government) {
                gov.government = bestGov;
                for (uint8_t s = 0; s < MAX_POLICY_SLOTS; ++s) {
                    gov.activePolicies[s] = EMPTY_POLICY_SLOT;
                }
                LOG_INFO("AI %u Adopted government: %.*s",
                         static_cast<unsigned>(this->m_player),
                         static_cast<int>(governmentDef(bestGov).name.size()),
                         governmentDef(bestGov).name.data());
            }

            const GovernmentDef& gdef = governmentDef(gov.government);

            struct SlotInfo {
                uint8_t slotIndex;
                PolicySlotType slotType;
            };
            std::vector<SlotInfo> slots;
            uint8_t slotIdx = 0;
            for (uint8_t s = 0; s < gdef.militarySlots && slotIdx < MAX_POLICY_SLOTS; ++s, ++slotIdx) {
                slots.push_back({slotIdx, PolicySlotType::Military});
            }
            for (uint8_t s = 0; s < gdef.economicSlots && slotIdx < MAX_POLICY_SLOTS; ++s, ++slotIdx) {
                slots.push_back({slotIdx, PolicySlotType::Economic});
            }
            for (uint8_t s = 0; s < gdef.diplomaticSlots && slotIdx < MAX_POLICY_SLOTS; ++s, ++slotIdx) {
                slots.push_back({slotIdx, PolicySlotType::Diplomatic});
            }
            for (uint8_t s = 0; s < gdef.wildcardSlots && slotIdx < MAX_POLICY_SLOTS; ++s, ++slotIdx) {
                slots.push_back({slotIdx, PolicySlotType::Wildcard});
            }

            std::array<bool, POLICY_CARD_COUNT> assigned{};

            for (const SlotInfo& slot : slots) {
                int8_t bestPolicy = EMPTY_POLICY_SLOT;
                float bestValue = -1.0f;

                for (uint8_t p = 0; p < POLICY_CARD_COUNT; ++p) {
                    if (!gov.isPolicyUnlocked(p) || assigned[static_cast<std::size_t>(p)]) {
                        continue;
                    }
                    const PolicyCardDef& pdef = policyCardDef(p);
                    if (pdef.slotType != slot.slotType &&
                        slot.slotType != PolicySlotType::Wildcard) {
                        continue;
                    }
                    float value = pdef.modifiers.productionMultiplier +
                                  pdef.modifiers.goldMultiplier +
                                  pdef.modifiers.scienceMultiplier +
                                  pdef.modifiers.cultureMultiplier +
                                  pdef.modifiers.combatStrengthBonus * 0.1f;
                    if (value > bestValue) {
                        bestValue = value;
                        bestPolicy = static_cast<int8_t>(p);
                    }
                }

                gov.activePolicies[slot.slotIndex] = bestPolicy;
                if (bestPolicy != EMPTY_POLICY_SLOT) {
                    assigned[static_cast<std::size_t>(bestPolicy)] = true;
                }
            }
        });
}

// ============================================================================
// Monetary system management
// ============================================================================

void AIController::manageTradeRoutes(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                                      const Market& market, const DiplomacyManager& diplomacy) {
    aoc::ecs::World& world = gameState.legacyWorld();
    // Find idle Trader units owned by this player that are not already on a route
    aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool == nullptr) {
        return;
    }

    // Collect Trader entities that don't have a TraderComponent (idle)
    std::vector<EntityId> idleTraders;
    for (uint32_t i = 0; i < unitPool->size(); ++i) {
        if (unitPool->data()[i].owner != this->m_player) { continue; }
        const UnitTypeDef& def = unitTypeDef(unitPool->data()[i].typeId);
        if (def.unitClass != UnitClass::Trader) { continue; }

        EntityId traderEntity = unitPool->entities()[i];
        // Check if already assigned to a route
        const TraderComponent* existing = world.tryGetComponent<TraderComponent>(traderEntity);
        if (existing != nullptr) { continue; }

        idleTraders.push_back(traderEntity);
    }

    if (idleTraders.empty()) {
        return;
    }

    // Find foreign cities to trade with (prefer closest)
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    // Get our needs for scoring destinations
    const PlayerEconomyComponent* myEcon = nullptr;
    const aoc::ecs::ComponentPool<PlayerEconomyComponent>* econPool =
        world.getPool<PlayerEconomyComponent>();
    if (econPool != nullptr) {
        for (uint32_t ei = 0; ei < econPool->size(); ++ei) {
            if (econPool->data()[ei].owner == this->m_player) {
                myEcon = &econPool->data()[ei];
                break;
            }
        }
    }

    for (EntityId traderEntity : idleTraders) {
        const UnitComponent* traderUnit = world.tryGetComponent<UnitComponent>(traderEntity);
        if (traderUnit == nullptr) { continue; }

        // Score each city as a trade destination based on complementary resources.
        // High score = destination has what we need AND needs what we have.
        EntityId bestCity = NULL_ENTITY;
        float bestScore = -1.0f;

        for (uint32_t ci = 0; ci < cityPool->size(); ++ci) {
            const CityComponent& city = cityPool->data()[ci];
            if (city.location == traderUnit->position) { continue; }

            float score = 0.0f;
            int32_t dist = aoc::hex::distance(traderUnit->position, city.location);
            float distPenalty = 1.0f / static_cast<float>(std::max(1, dist));

            if (city.owner != this->m_player) {
                // Foreign city: check what THEY have that WE need
                const CityStockpileComponent* destStock =
                    world.tryGetComponent<CityStockpileComponent>(cityPool->entities()[ci]);
                if (destStock != nullptr && myEcon != nullptr) {
                    for (const std::pair<const uint16_t, int32_t>& need : myEcon->totalNeeds) {
                        int32_t destHas = destStock->getAmount(need.first);
                        if (destHas > 1) {
                            score += static_cast<float>(std::min(destHas, need.second))
                                   * static_cast<float>(market.marketData(need.first).currentPrice);
                        }
                    }
                }
                // Also check what WE have that THEY need
                const PlayerEconomyComponent* destEcon = nullptr;
                if (econPool != nullptr) {
                    for (uint32_t ei = 0; ei < econPool->size(); ++ei) {
                        if (econPool->data()[ei].owner == city.owner) {
                            destEcon = &econPool->data()[ei];
                            break;
                        }
                    }
                }
                if (destEcon != nullptr) {
                    for (const std::pair<const uint16_t, int32_t>& theirNeed : destEcon->totalNeeds) {
                        int32_t weHave = 0;
                        if (myEcon != nullptr) {
                            std::unordered_map<uint16_t, int32_t>::const_iterator supIt =
                                myEcon->totalSupply.find(theirNeed.first);
                            if (supIt != myEcon->totalSupply.end()) { weHave = supIt->second; }
                        }
                        if (weHave > 1) {
                            score += static_cast<float>(std::min(weHave, theirNeed.second))
                                   * static_cast<float>(market.marketData(theirNeed.first).currentPrice) * 0.5f;
                        }
                    }
                }

                // Foreign trade base bonus (always some value from gold earnings)
                score += 50.0f;
            } else {
                // Internal trade: lower priority, just moves goods between own cities
                score += 10.0f;
            }

            score *= distPenalty;

            if (score > bestScore) {
                bestScore = score;
                bestCity = cityPool->entities()[ci];
            }
        }

        if (bestCity.isValid()) {
            ErrorCode result = establishTradeRoute(world, grid, market, &diplomacy, traderEntity, bestCity);
            if (result == ErrorCode::Ok) {
                const CityComponent* destCity = world.tryGetComponent<CityComponent>(bestCity);
                LOG_INFO("AI %u established trade route to %s (player %u, score %.0f)",
                         static_cast<unsigned>(this->m_player),
                         (destCity != nullptr) ? destCity->name.c_str() : "unknown",
                         static_cast<unsigned>((destCity != nullptr) ? destCity->owner : INVALID_PLAYER),
                         static_cast<double>(bestScore));
            }
        }
    }
}

// ============================================================================

void AIController::manageMonetarySystem(aoc::game::GameState& gameState,
                                         const DiplomacyManager& /*diplomacy*/) {
    aoc::ecs::World& world = gameState.legacyWorld();
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool == nullptr) {
        return;
    }

    MonetaryStateComponent* myState = nullptr;
    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        if (monetaryPool->data()[i].owner == this->m_player) {
            myState = &monetaryPool->data()[i];
            break;
        }
    }
    if (myState == nullptr) {
        return;
    }

    int32_t cityCount = 0;
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].owner == this->m_player) {
                ++cityCount;
            }
        }
    }

    int32_t tradePartnerCount = 0;
    const aoc::ecs::ComponentPool<TradeRouteComponent>* tradePool =
        world.getPool<TradeRouteComponent>();
    if (tradePool != nullptr) {
        std::unordered_set<PlayerId> partners;
        for (uint32_t i = 0; i < tradePool->size(); ++i) {
            const TradeRouteComponent& route = tradePool->data()[i];
            if (route.sourcePlayer == this->m_player && route.destPlayer != this->m_player) {
                partners.insert(route.destPlayer);
            }
            if (route.destPlayer == this->m_player && route.sourcePlayer != this->m_player) {
                partners.insert(route.sourcePlayer);
            }
        }
        tradePartnerCount = static_cast<int32_t>(partners.size());
    }

    int32_t gdpRank = 1;
    int32_t playerCount = static_cast<int32_t>(monetaryPool->size());
    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        const MonetaryStateComponent& other = monetaryPool->data()[i];
        if (other.owner != this->m_player && other.gdp > myState->gdp) {
            ++gdpRank;
        }
    }

    MonetarySystemType nextTarget = MonetarySystemType::Count;
    switch (myState->system) {
        case MonetarySystemType::Barter:
            nextTarget = MonetarySystemType::CommodityMoney;
            break;
        case MonetarySystemType::CommodityMoney:
            nextTarget = MonetarySystemType::GoldStandard;
            break;
        case MonetarySystemType::GoldStandard:
            nextTarget = MonetarySystemType::FiatMoney;
            break;
        default:
            return;
    }

    if (nextTarget == MonetarySystemType::FiatMoney && gdpRank > 2) {
        return;
    }

    ErrorCode result = myState->canTransition(
        nextTarget, cityCount, tradePartnerCount, gdpRank, playerCount);
    if (result == ErrorCode::Ok) {
        myState->transitionTo(nextTarget);
        LOG_INFO("AI player %u transitioned to %.*s",
                 static_cast<unsigned>(this->m_player),
                 static_cast<int>(monetarySystemName(nextTarget).size()),
                 monetarySystemName(nextTarget).data());
    } else {
        if (myState->turnsInCurrentSystem % 50 == 0 && myState->turnsInCurrentSystem > 0) {
            LOG_INFO("AI player %u cannot transition to %.*s: strength=%d coins=%d cities=%d trades=%d inflation=%.2f",
                     static_cast<unsigned>(this->m_player),
                     static_cast<int>(monetarySystemName(nextTarget).size()),
                     monetarySystemName(nextTarget).data(),
                     myState->currencyStrength(),
                     myState->totalCoinCount(),
                     cityCount, tradePartnerCount,
                     static_cast<double>(myState->inflationRate));
        }
    }
}

} // namespace aoc::sim::ai
