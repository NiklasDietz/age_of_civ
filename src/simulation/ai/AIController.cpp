/**
 * @file AIController.cpp
 * @brief AI decision-making implementation.
 *
 * Implements a priority-based AI that manages research, government, city
 * production, builders, military units, settlers, scouts, and diplomacy.
 */

#include "aoc/simulation/ai/AIController.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/BorderExpansion.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/ai/AIEconomicStrategy.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/Pathfinding.hpp"
#include "aoc/ecs/World.hpp"

#include <limits>
#include <unordered_set>

namespace aoc::sim::ai {

// ============================================================================
// Helper: Score a potential city location for settler placement.
// ============================================================================

static float scoreCityLocation(hex::AxialCoord pos,
                               const aoc::map::HexGrid& grid,
                               const aoc::ecs::World& world,
                               PlayerId player) {
    if (!grid.isValid(pos)) {
        return -1000.0f;
    }
    const int32_t centerIdx = grid.toIndex(pos);

    // Must be buildable land
    if (aoc::map::isWater(grid.terrain(centerIdx)) ||
        aoc::map::isImpassable(grid.terrain(centerIdx))) {
        return -1000.0f;
    }

    // Already owned by someone -- penalize heavily
    if (grid.owner(centerIdx) != INVALID_PLAYER) {
        return -500.0f;
    }

    float score = 0.0f;

    // Sum yields of surrounding tiles (ring-1)
    const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(pos);
    int32_t coastCount = 0;
    for (const hex::AxialCoord& nbr : nbrs) {
        if (!grid.isValid(nbr)) {
            continue;
        }
        const int32_t nbrIdx = grid.toIndex(nbr);
        const aoc::map::TileYield yield = grid.tileYield(nbrIdx);
        // Weight food and production most heavily for city founding
        score += static_cast<float>(yield.food) * 2.0f;
        score += static_cast<float>(yield.production) * 1.5f;
        score += static_cast<float>(yield.gold) * 1.0f;

        // Coastal bonus
        if (aoc::map::isWater(grid.terrain(nbrIdx))) {
            ++coastCount;
        }

        // Resources are valuable
        if (grid.resource(nbrIdx).isValid()) {
            score += 3.0f;
        }
    }

    // Coastal access is good (but not too much water)
    if (coastCount > 0 && coastCount <= 3) {
        score += 4.0f;
    }

    // Distance from existing cities -- want 4-6 hexes away
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            const CityComponent& city = cityPool->data()[i];
            const int32_t dist = hex::distance(pos, city.location);

            if (city.owner == player) {
                // Penalize being too close to own cities
                if (dist < 4) {
                    score -= 50.0f;
                }
                // Slightly penalize being very far from own cities
                else if (dist > 8) {
                    score -= static_cast<float>(dist - 8) * 2.0f;
                }
                // Sweet spot at 4-6 hexes
                else if (dist >= 4 && dist <= 6) {
                    score += 5.0f;
                }
            } else {
                // Penalize being too close to enemy cities
                if (dist < 4) {
                    score -= 30.0f;
                }
            }
        }
    }

    // Add yield from the center tile itself
    const aoc::map::TileYield centerYield = grid.tileYield(centerIdx);
    score += static_cast<float>(centerYield.food) * 1.5f;
    score += static_cast<float>(centerYield.production) * 1.0f;

    return score;
}

// ============================================================================
// Helper: Find the best military unit type ID the player can produce.
// Returns the unit with the highest combat strength.
// ============================================================================

static UnitTypeId bestAvailableMilitaryUnit(const aoc::ecs::World& world,
                                            PlayerId player) {
    // Find the strongest buildable land military unit using tech gating
    UnitTypeId bestId{0};  // Warrior as fallback
    int32_t bestStrength = 0;

    for (const UnitTypeDef& def : UNIT_TYPE_DEFS) {
        if (!isMilitary(def.unitClass) || isNaval(def.unitClass)) {
            continue;
        }
        if (!canBuildUnit(world, player, def.id)) {
            continue;
        }
        // Use total combat effectiveness: melee + ranged
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
    int32_t total     = 0;
};

static UnitCounts countPlayerUnits(const aoc::ecs::World& world, PlayerId player) {
    UnitCounts counts{};
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
    }
    return counts;
}

// ============================================================================
// Constructor
// ============================================================================

AIController::AIController(PlayerId player, aoc::ui::AIDifficulty difficulty)
    : m_player(player)
    , m_difficulty(difficulty)
{
}

// ============================================================================
// Main turn execution
// ============================================================================

void AIController::executeTurn(aoc::ecs::World& world,
                                aoc::map::HexGrid& grid,
                                DiplomacyManager& diplomacy,
                                const Market& market,
                                aoc::Random& rng) {
    this->manageGovernment(world);
    this->selectResearch(world);
    this->executeCityActions(world, grid);
    this->manageBuildersAndImprovements(world, grid);
    this->executeUnitActions(world, grid, rng);
    this->manageEconomy(world, diplomacy, market);
    this->manageMonetarySystem(world, diplomacy);
    aoc::sim::aiEconomicStrategy(world, grid, market, diplomacy, this->m_player,
                                  static_cast<int32_t>(this->m_difficulty));
    this->executeDiplomacyActions(world, diplomacy, market);

    // Refresh movement so units have moved for this turn
    refreshMovement(world, this->m_player);
}

// ============================================================================
// Research selection
// Priority 1: Techs that unlock buildings we don't have
// Priority 2: Techs that unlock military units when threatened
// Priority 3: Techs that unlock resources (economic growth)
// Tie-break: cheaper techs preferred
// ============================================================================

void AIController::selectResearch(aoc::ecs::World& world) {
    const UnitCounts counts = countPlayerUnits(world, this->m_player);
    const bool threatened = counts.military < 3;

    // Count how many cities we have for scaling thresholds
    int32_t ownedCityCount = 0;
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].owner == this->m_player) {
                ++ownedCityCount;
            }
        }
    }

    // Collect set of buildings we already have across all cities
    std::unordered_set<uint16_t> ownedBuildings;
    const aoc::ecs::ComponentPool<CityDistrictsComponent>* distPool =
        world.getPool<CityDistrictsComponent>();
    if (distPool != nullptr && cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].owner != this->m_player) {
                continue;
            }
            const EntityId cityEntity = cityPool->entities()[i];
            const CityDistrictsComponent* districts =
                world.tryGetComponent<CityDistrictsComponent>(cityEntity);
            if (districts != nullptr) {
                for (const CityDistrictsComponent::PlacedDistrict& d : districts->districts) {
                    for (const BuildingId& bid : d.buildings) {
                        ownedBuildings.insert(bid.value);
                    }
                }
            }
        }
    }

    world.forEach<PlayerTechComponent>(
        [this, threatened, &ownedBuildings, ownedCityCount, &world](
            EntityId, PlayerTechComponent& tech) {
            if (tech.owner != this->m_player) {
                return;
            }
            if (tech.currentResearch.isValid()) {
                return;  // Already researching
            }
            const std::vector<TechId> available = tech.availableTechs();
            if (available.empty()) {
                return;
            }

            TechId best = available[0];
            int32_t bestScore = std::numeric_limits<int32_t>::min();

            // Hard AI gets a bonus toward higher-value techs
            const bool hardAI = (this->m_difficulty == aoc::ui::AIDifficulty::Hard);

            for (const TechId& tid : available) {
                const TechDef& def = techDef(tid);
                int32_t score = 0;

                // Hard AI bonus: prefer techs with more unlocks
                if (hardAI) {
                    score += 1000;
                }

                // Priority 1: Techs that unlock buildings we don't yet have
                if (!def.unlockedBuildings.empty()) {
                    bool unlocksNew = false;
                    for (const BuildingId& bid : def.unlockedBuildings) {
                        if (ownedBuildings.find(bid.value) == ownedBuildings.end()) {
                            unlocksNew = true;
                            // Extra bonus for production buildings
                            if (bid.value < BUILDING_DEFS.size() &&
                                BUILDING_DEFS[bid.value].productionBonus > 0) {
                                score += 3000;
                            }
                        }
                    }
                    score += unlocksNew ? 10000 : 5000;
                }

                // Priority 2: Techs that unlock military units when threatened
                if (!def.unlockedUnits.empty()) {
                    score += threatened ? 8000 : 3000;
                    // Bonus for unlocking stronger units
                    for (const UnitTypeId& uid : def.unlockedUnits) {
                        if (uid.value < UNIT_TYPE_DEFS.size()) {
                            score += UNIT_TYPE_DEFS[uid.value].combatStrength * 10;
                        }
                    }
                }

                // Priority 3: Techs that unlock goods (resource processing)
                if (!def.unlockedGoods.empty()) {
                    score += 2000 + static_cast<int32_t>(def.unlockedGoods.size()) * 500;
                }

                // Scale bonus slightly by city count
                score += ownedCityCount * 100;

                // Leader personality tech bias:
                // Apply multiplier based on what the tech unlocks
                CivId myCiv2 = 0;
                const aoc::ecs::ComponentPool<PlayerCivilizationComponent>* cp =
                    world.getPool<PlayerCivilizationComponent>();
                if (cp != nullptr) {
                    for (uint32_t cc = 0; cc < cp->size(); ++cc) {
                        if (cp->data()[cc].owner == this->m_player) {
                            myCiv2 = cp->data()[cc].civId; break;
                        }
                    }
                }
                const LeaderBehavior& beh = leaderPersonality(myCiv2).behavior;

                // Military techs (unlock units) get military bias
                if (!def.unlockedUnits.empty()) {
                    score = static_cast<int32_t>(static_cast<float>(score) * beh.techMilitary);
                }
                // Economic techs (unlock buildings related to commerce)
                if (!def.unlockedBuildings.empty()) {
                    for (const BuildingId& bid : def.unlockedBuildings) {
                        if (bid.value == 6 || bid.value == 20 || bid.value == 21 || bid.value == 24) {
                            score = static_cast<int32_t>(static_cast<float>(score) * beh.techEconomic);
                        }
                        if (bid.value == 3 || bid.value == 5 || bid.value == 10 || bid.value == 11) {
                            score = static_cast<int32_t>(static_cast<float>(score) * beh.techIndustrial);
                        }
                    }
                }
                // Higher era techs biased by information focus
                if (def.era.value >= 5) {
                    score = static_cast<int32_t>(static_cast<float>(score) * beh.techInformation);
                }

                // Prefer cheaper techs as tiebreaker (invert cost so cheaper = higher)
                score -= def.researchCost;

                if (score > bestScore) {
                    bestScore = score;
                    best = tid;
                }
            }

            tech.currentResearch = best;
            LOG_INFO("AI %u Researching: %.*s",
                     static_cast<unsigned>(this->m_player),
                     static_cast<int>(techDef(best).name.size()),
                     techDef(best).name.data());
        });

    // Civic research
    world.forEach<PlayerCivicComponent>(
        [this](EntityId, PlayerCivicComponent& civic) {
            if (civic.owner != this->m_player) {
                return;
            }
            if (civic.currentResearch.isValid()) {
                return;
            }
            const uint16_t count = civicCount();
            CivicId best{};
            int32_t bestScore = std::numeric_limits<int32_t>::min();
            for (uint16_t i = 0; i < count; ++i) {
                CivicId id{i};
                if (civic.canResearch(id)) {
                    const CivicDef& def = civicDef(id);
                    int32_t score = 0;
                    if (!def.unlockedGovernmentIds.empty()) {
                        score += 5000;
                    }
                    if (!def.unlockedPolicyIds.empty()) {
                        score += 3000;
                    }
                    score -= def.cultureCost;

                    if (score > bestScore) {
                        bestScore = score;
                        best = id;
                    }
                }
            }
            if (best.isValid()) {
                civic.currentResearch = best;
            }
        });
}

// ============================================================================
// City actions: priority-based production queue management
//
// Priority order per city:
//   1. Settler if < 3 cities and pop > 3 and no settler already building/existing
//   2. Builder if unimproved tiles and no builders
//   3. Military unit if total military < cities * 2 + 2
//   4. Building -- pick most valuable building the city can build
//   5. Default: best military unit
// ============================================================================

void AIController::executeCityActions(aoc::ecs::World& world,
                                       aoc::map::HexGrid& grid) {
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    const UnitCounts unitCounts = countPlayerUnits(world, this->m_player);

    // Count owned cities
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

    // Determine best available military unit type once
    const UnitTypeId bestMilitaryId = bestAvailableMilitaryUnit(world, this->m_player);
    const UnitTypeDef& bestMilitaryDef = unitTypeDef(bestMilitaryId);

    bool settlerEnqueued = false;  // Only produce one settler across all cities
    bool builderEnqueued = false;  // Only produce one builder across all cities

    for (uint32_t ci = 0; ci < cityPool->size(); ++ci) {
        CityComponent& city = cityPool->data()[ci];
        if (city.owner != this->m_player) {
            continue;
        }
        const EntityId cityEntity = cityPool->entities()[ci];
        ProductionQueueComponent* queue =
            world.tryGetComponent<ProductionQueueComponent>(cityEntity);
        if (queue == nullptr || !queue->isEmpty()) {
            continue;  // Already producing something
        }

        // Easy AI: 30% chance to skip city action entirely (suboptimal play)
        if (this->m_difficulty == aoc::ui::AIDifficulty::Easy) {
            // Use a simple hash of city index + turn as pseudo-random
            const uint32_t pseudoRand = (ci * 7919u + 31u) % 100u;
            if (pseudoRand < 30u) {
                continue;
            }
        }

        ProductionQueueItem item{};

        // Priority 1: Settler if conditions met and this city has pop > 3
        if (needsSettler && !settlerEnqueued && city.population >= targets.settlePopThreshold) {
            item.type = ProductionItemType::Unit;
            item.itemId = 3;  // Settler
            item.name = "Settler";
            item.totalCost = static_cast<float>(unitTypeDef(UnitTypeId{3}).productionCost);
            item.progress = 0.0f;
            settlerEnqueued = true;
            LOG_INFO("AI %u Enqueued Settler in %s (pop %d)",
                     static_cast<unsigned>(this->m_player),
                     city.name.c_str(), city.population);
        }
        // Priority 2: Builder if we need improvements and have no builders
        else if (needsBuilder && !builderEnqueued) {
            item.type = ProductionItemType::Unit;
            item.itemId = 5;  // Builder
            item.name = "Builder";
            item.totalCost = static_cast<float>(unitTypeDef(UnitTypeId{5}).productionCost);
            item.progress = 0.0f;
            builderEnqueued = true;
            LOG_INFO("AI %u Enqueued Builder in %s",
                     static_cast<unsigned>(this->m_player), city.name.c_str());
        }
        // Priority 3: Military units if below desired count
        else if (needsMilitary) {
            item.type = ProductionItemType::Unit;
            item.itemId = bestMilitaryId.value;
            item.name = std::string(bestMilitaryDef.name);
            item.totalCost = static_cast<float>(bestMilitaryDef.productionCost);
            item.progress = 0.0f;
            LOG_INFO("AI %u Enqueued %.*s in %s",
                     static_cast<unsigned>(this->m_player),
                     static_cast<int>(bestMilitaryDef.name.size()),
                     bestMilitaryDef.name.data(),
                     city.name.c_str());
        }
        // Priority 4+5: Districts, then Buildings, then Fallback
        else {
            bool enqueuedSomething = false;

            // --- 4a: Districts the city is missing ---
            const CityDistrictsComponent* existingDistricts =
                world.tryGetComponent<CityDistrictsComponent>(cityEntity);

            static constexpr DistrictType DISTRICT_PRIORITY[] = {
                DistrictType::Commercial, DistrictType::Campus,
                DistrictType::Industrial, DistrictType::Encampment,
                DistrictType::Harbor,
            };
            static constexpr int32_t DISTRICT_COSTS[] = {60, 55, 60, 55, 70};

            for (int32_t di = 0; di < 5 && !enqueuedSomething; ++di) {
                DistrictType dtype = DISTRICT_PRIORITY[di];
                bool alreadyHas = (existingDistricts != nullptr
                                   && existingDistricts->hasDistrict(dtype));
                if (!alreadyHas) {
                    item.type = ProductionItemType::District;
                    item.itemId = static_cast<uint16_t>(dtype);
                    item.name = std::string(districtTypeName(dtype));
                    item.totalCost = static_cast<float>(DISTRICT_COSTS[di]);
                    item.progress = 0.0f;
                    enqueuedSomething = true;
                }
            }

            // --- 4b: Buildings (if no district queued) ---
            if (!enqueuedSomething) {
                static constexpr uint16_t BLDG_PRIO[] = {
                    16, 15, 1, 7, 6, 24, 0, 20, 19, 3, 26, 4, 12
                };
                for (uint16_t bidx : BLDG_PRIO) {
                    if (bidx >= BUILDING_DEFS.size()) { continue; }
                    const BuildingDef& bdef = BUILDING_DEFS[bidx];
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

            // Try any building
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

        // Apply game pace cost multiplier (Marathon/Eternal = higher costs)
        item.totalCost *= aoc::sim::GamePace::instance().costMultiplier;
        queue->queue.push_back(std::move(item));
    }
}

// ============================================================================
// Unit actions
//
// Settlers: find best city location and move there; found when arrived.
// Military: if at war, seek enemies and attack; otherwise patrol borders.
//           Fortify on hills/forests for defense.
//           Prioritize defending cities under threat.
//           Use ranged attacks when possible.
// Scouts:   explore toward unexplored territory.
// ============================================================================

void AIController::executeUnitActions(aoc::ecs::World& world,
                                       aoc::map::HexGrid& grid,
                                       aoc::Random& rng) {
    aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool == nullptr) {
        return;
    }

    // Collect owned units (copy because we may modify pool during iteration)
    struct UnitInfo {
        EntityId entity;
        UnitComponent unit;
    };
    std::vector<UnitInfo> ownedUnits;
    for (uint32_t i = 0; i < unitPool->size(); ++i) {
        if (unitPool->data()[i].owner == this->m_player) {
            ownedUnits.push_back({unitPool->entities()[i], unitPool->data()[i]});
        }
    }

    // Cache enemy unit positions
    struct EnemyInfo {
        hex::AxialCoord position;
        EntityId entity;
        PlayerId owner;
    };
    std::vector<EnemyInfo> enemyUnits;
    for (uint32_t i = 0; i < unitPool->size(); ++i) {
        if (unitPool->data()[i].owner != this->m_player) {
            enemyUnits.push_back({
                unitPool->data()[i].position,
                unitPool->entities()[i],
                unitPool->data()[i].owner
            });
        }
    }

    // Cache own city locations for border patrol / defense
    std::vector<hex::AxialCoord> ownCityLocations;
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].owner == this->m_player) {
                ownCityLocations.push_back(cityPool->data()[i].location);
            }
        }
    }

    // Cache enemy city locations for military targeting
    struct EnemyCityInfo {
        hex::AxialCoord position;
        PlayerId owner;
    };
    std::vector<EnemyCityInfo> enemyCities;
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].owner != this->m_player) {
                enemyCities.push_back({
                    cityPool->data()[i].location,
                    cityPool->data()[i].owner
                });
            }
        }
    }

    // Check which players we're at war with (read from diplomacy via enemy units)
    // We approximate by checking if enemy units are adjacent to our territory

    for (const UnitInfo& info : ownedUnits) {
        if (!world.isAlive(info.entity)) {
            continue;
        }

        const UnitTypeDef& def = unitTypeDef(info.unit.typeId);

        // ================================================================
        // SETTLERS: Find best city location and move/found
        // ================================================================
        if (def.unitClass == UnitClass::Settler) {
            // Score candidate locations in a radius around the settler
            float bestScore = -999.0f;
            hex::AxialCoord bestLocation = info.unit.position;

            // Search in a spiral up to 8 tiles away
            std::vector<hex::AxialCoord> candidates;
            candidates.reserve(200);
            hex::spiral(info.unit.position, 8, std::back_inserter(candidates));

            for (const hex::AxialCoord& candidate : candidates) {
                if (!grid.isValid(candidate)) {
                    continue;
                }
                const float locationScore = scoreCityLocation(
                    candidate, grid, world, this->m_player);
                if (locationScore > bestScore) {
                    bestScore = locationScore;
                    bestLocation = candidate;
                }
            }

            // If we're at the best location (or close enough), found the city
            if (bestLocation == info.unit.position && bestScore > -100.0f) {
                EntityId cityEntity = world.createEntity();
                const std::string aiCityName = getNextCityName(world, this->m_player);
                world.addComponent<CityComponent>(
                    cityEntity,
                    CityComponent::create(this->m_player, info.unit.position, aiCityName));
                world.addComponent<ProductionQueueComponent>(
                    cityEntity, ProductionQueueComponent{});

                CityDistrictsComponent districts{};
                CityDistrictsComponent::PlacedDistrict center;
                center.type = DistrictType::CityCenter;
                center.location = info.unit.position;
                districts.districts.push_back(std::move(center));
                world.addComponent<CityDistrictsComponent>(
                    cityEntity, std::move(districts));

                // Attach religion component to AI-founded city
                world.addComponent<CityReligionComponent>(
                    cityEntity, CityReligionComponent{});

                claimInitialTerritory(grid, info.unit.position, this->m_player);
                world.destroyEntity(info.entity);
                LOG_INFO("AI %u Founded city at (%d,%d) score=%.1f",
                         static_cast<unsigned>(this->m_player),
                         info.unit.position.q, info.unit.position.r,
                         static_cast<double>(bestScore));
                continue;
            }

            // Move toward the best location
            if (bestLocation != info.unit.position && info.unit.movementRemaining > 0) {
                orderUnitMove(world, info.entity, bestLocation, grid);
                moveUnitAlongPath(world, info.entity, grid);
            }
            continue;
        }

        // Builders are handled by manageBuildersAndImprovements()
        if (def.unitClass == UnitClass::Civilian) {
            continue;
        }

        if (info.unit.movementRemaining <= 0) {
            continue;
        }

        const std::array<hex::AxialCoord, 6> neighborTiles = hex::neighbors(info.unit.position);

        // ================================================================
        // SCOUTS: Explore systematically toward unexplored territory
        // ================================================================
        if (def.unitClass == UnitClass::Scout) {
            // For each neighbor, count how many of its ring-2 neighbors are owned by us.
            // Pick the direction moving away from owned territory.
            hex::AxialCoord bestMove = info.unit.position;
            int32_t lowestOwnedCount = std::numeric_limits<int32_t>::max();

            for (const hex::AxialCoord& nbr : neighborTiles) {
                if (!grid.isValid(nbr) || grid.movementCost(grid.toIndex(nbr)) <= 0) {
                    continue;
                }
                // Count owned tiles in ring-1 and ring-2 around this candidate
                int32_t ownedNearby = 0;
                std::vector<hex::AxialCoord> checkTiles;
                checkTiles.reserve(18);
                hex::spiral(nbr, 2, std::back_inserter(checkTiles));
                for (const hex::AxialCoord& ct : checkTiles) {
                    if (grid.isValid(ct) && grid.owner(grid.toIndex(ct)) == this->m_player) {
                        ++ownedNearby;
                    }
                }
                if (ownedNearby < lowestOwnedCount) {
                    lowestOwnedCount = ownedNearby;
                    bestMove = nbr;
                }
            }

            if (bestMove != info.unit.position) {
                orderUnitMove(world, info.entity, bestMove, grid);
                moveUnitAlongPath(world, info.entity, grid);
            }
            continue;
        }

        // ================================================================
        // MILITARY UNITS: Attack, defend, patrol
        // ================================================================
        if (isMilitary(def.unitClass)) {
            // --- Ranged attack: try to attack enemies within range first ---
            if (def.rangedStrength > 0 && def.range > 0) {
                EntityId bestTarget = NULL_ENTITY;
                int32_t bestTargetDist = std::numeric_limits<int32_t>::max();

                for (const EnemyInfo& enemy : enemyUnits) {
                    const int32_t dist = hex::distance(info.unit.position, enemy.position);
                    if (dist <= def.range && dist < bestTargetDist) {
                        bestTargetDist = dist;
                        bestTarget = enemy.entity;
                    }
                }

                if (bestTarget.isValid() && world.isAlive(bestTarget)) {
                    resolveRangedCombat(world, rng, grid, info.entity, bestTarget);
                    if (!world.isAlive(info.entity)) {
                        continue;
                    }
                    // Ranged units can still move after shooting; continue to movement
                }
            }

            // --- Melee attack: check for adjacent enemies ---
            bool attacked = false;
            if (def.rangedStrength == 0 || def.range == 0) {
                for (const hex::AxialCoord& nbr : neighborTiles) {
                    if (!grid.isValid(nbr)) {
                        continue;
                    }
                    aoc::ecs::ComponentPool<UnitComponent>* pool =
                        world.getPool<UnitComponent>();
                    if (pool == nullptr) {
                        break;
                    }
                    for (uint32_t j = 0; j < pool->size(); ++j) {
                        if (pool->data()[j].position == nbr &&
                            pool->data()[j].owner != this->m_player) {
                            resolveMeleeCombat(world, rng, grid, info.entity, pool->entities()[j]);
                            attacked = true;
                            break;
                        }
                    }
                    if (attacked) {
                        break;
                    }
                }
            }

            if (attacked || !world.isAlive(info.entity)) {
                continue;
            }

            // --- Check if any own city is threatened (enemy within 3 tiles) ---
            bool cityThreatened = false;
            hex::AxialCoord threatenedCityPos{};
            for (const hex::AxialCoord& cityLoc : ownCityLocations) {
                for (const EnemyInfo& enemy : enemyUnits) {
                    if (hex::distance(cityLoc, enemy.position) <= 3) {
                        cityThreatened = true;
                        threatenedCityPos = cityLoc;
                        break;
                    }
                }
                if (cityThreatened) {
                    break;
                }
            }

            // If a city is threatened, rush to defend it
            if (cityThreatened) {
                const int32_t distToCity = hex::distance(info.unit.position, threatenedCityPos);
                if (distToCity > 1) {
                    orderUnitMove(world, info.entity, threatenedCityPos, grid);
                    moveUnitAlongPath(world, info.entity, grid);
                    continue;
                }
            }

            // --- Seek enemies: move toward nearest enemy unit or city ---
            // Hard AI prioritizes enemy cities over individual units
            int32_t closestDist = std::numeric_limits<int32_t>::max();
            hex::AxialCoord closestTarget = info.unit.position;

            const bool hardMode = (this->m_difficulty == aoc::ui::AIDifficulty::Hard);

            // Hard AI: prioritize cities first, then units
            if (hardMode) {
                for (const EnemyCityInfo& ecity : enemyCities) {
                    const int32_t dist = hex::distance(info.unit.position, ecity.position);
                    if (dist < closestDist) {
                        closestDist = dist;
                        closestTarget = ecity.position;
                    }
                }
            }

            for (const EnemyInfo& enemy : enemyUnits) {
                const int32_t dist = hex::distance(info.unit.position, enemy.position);
                if (dist < closestDist) {
                    closestDist = dist;
                    closestTarget = enemy.position;
                }
            }

            // Non-hard AI also considers enemy cities (after units)
            if (!hardMode) {
                for (const EnemyCityInfo& ecity : enemyCities) {
                    const int32_t dist = hex::distance(info.unit.position, ecity.position);
                    if (dist < closestDist) {
                        closestDist = dist;
                        closestTarget = ecity.position;
                    }
                }
            }

            if (closestDist < std::numeric_limits<int32_t>::max() && closestDist > 1) {
                // Move toward the target -- pick the neighbor closest to target
                hex::AxialCoord bestMove = info.unit.position;
                int32_t bestDist = closestDist;
                for (const hex::AxialCoord& nbr : neighborTiles) {
                    if (!grid.isValid(nbr) || grid.movementCost(grid.toIndex(nbr)) <= 0) {
                        continue;
                    }
                    const int32_t dist = hex::distance(nbr, closestTarget);
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestMove = nbr;
                    }
                }
                if (bestMove != info.unit.position) {
                    orderUnitMove(world, info.entity, bestMove, grid);
                    moveUnitAlongPath(world, info.entity, grid);
                }
            }
            // No enemies visible -- patrol borders
            else if (closestDist == std::numeric_limits<int32_t>::max()) {
                // Try to move toward border tiles (tiles we own at the edge of our territory)
                hex::AxialCoord bestBorder = info.unit.position;
                int32_t bestBorderScore = std::numeric_limits<int32_t>::min();

                // Check tiles in a radius of 5 for border positions
                std::vector<hex::AxialCoord> searchTiles;
                searchTiles.reserve(60);
                hex::spiral(info.unit.position, 4, std::back_inserter(searchTiles));

                for (const hex::AxialCoord& tile : searchTiles) {
                    if (!grid.isValid(tile)) {
                        continue;
                    }
                    const int32_t tileIdx = grid.toIndex(tile);
                    if (grid.owner(tileIdx) != this->m_player) {
                        continue;
                    }
                    if (grid.movementCost(tileIdx) <= 0) {
                        continue;
                    }

                    // A border tile has at least one non-owned neighbor
                    const std::array<hex::AxialCoord, 6> tileNbrs = hex::neighbors(tile);
                    int32_t unownedNeighbors = 0;
                    for (const hex::AxialCoord& tn : tileNbrs) {
                        if (!grid.isValid(tn) || grid.owner(grid.toIndex(tn)) != this->m_player) {
                            ++unownedNeighbors;
                        }
                    }
                    if (unownedNeighbors == 0) {
                        continue;  // Interior tile, not a border
                    }

                    // Score: prefer tiles that are on the border and have defensive terrain
                    int32_t borderScore = unownedNeighbors * 10;
                    const aoc::map::FeatureType feat = grid.feature(tileIdx);
                    if (feat == aoc::map::FeatureType::Hills) {
                        borderScore += 5;
                    }
                    if (feat == aoc::map::FeatureType::Forest) {
                        borderScore += 3;
                    }
                    // Penalize distance
                    borderScore -= hex::distance(info.unit.position, tile);

                    if (borderScore > bestBorderScore) {
                        bestBorderScore = borderScore;
                        bestBorder = tile;
                    }
                }

                if (bestBorder != info.unit.position) {
                    orderUnitMove(world, info.entity, bestBorder, grid);
                    moveUnitAlongPath(world, info.entity, grid);
                } else {
                    // Fortify in place if on good terrain
                    const int32_t unitIdx = grid.toIndex(info.unit.position);
                    const aoc::map::FeatureType unitFeat = grid.feature(unitIdx);
                    if (unitFeat == aoc::map::FeatureType::Hills ||
                        unitFeat == aoc::map::FeatureType::Forest) {
                        UnitComponent* liveUnit =
                            world.tryGetComponent<UnitComponent>(info.entity);
                        if (liveUnit != nullptr && liveUnit->state != UnitState::Fortified) {
                            liveUnit->state = UnitState::Fortified;
                            LOG_INFO("AI %u Unit fortified at (%d,%d)",
                                     static_cast<unsigned>(this->m_player),
                                     info.unit.position.q, info.unit.position.r);
                        }
                    } else {
                        // Random patrol if nothing better to do
                        std::vector<hex::AxialCoord> validMoves;
                        for (const hex::AxialCoord& nbr : neighborTiles) {
                            if (grid.isValid(nbr) && grid.movementCost(grid.toIndex(nbr)) > 0) {
                                validMoves.push_back(nbr);
                            }
                        }
                        if (!validMoves.empty()) {
                            const int32_t idx = rng.nextInt(
                                0, static_cast<int32_t>(validMoves.size()) - 1);
                            orderUnitMove(world, info.entity,
                                          validMoves[static_cast<std::size_t>(idx)], grid);
                            moveUnitAlongPath(world, info.entity, grid);
                        }
                    }
                }
            }
            continue;
        }
    }
}

// ============================================================================
// Diplomacy
//
// Declare war if military > 1.5x neighbor AND relations < -20.
// Propose peace if losing a war (fewer military units).
// Request open borders from friendly neighbors (relations > 10).
// ============================================================================

void AIController::executeDiplomacyActions(aoc::ecs::World& world,
                                            DiplomacyManager& diplomacy,
                                            const Market& market) {
    const aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool == nullptr) {
        return;
    }

    // Count military units per player
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
            // Propose peace if losing (they have more military)
            if (theirMilitary > ourMilitary) {
                diplomacy.makePeace(this->m_player, other);
                LOG_INFO("AI %u Proposed peace with player %u (outmilitaried %d vs %d)",
                         static_cast<unsigned>(this->m_player),
                         static_cast<unsigned>(other),
                         ourMilitary, theirMilitary);
            }
        } else {
            // Easy AI: never declares war proactively
            // Normal AI: declare war if military > 1.5x AND relations < -20
            // Hard AI: more aggressive, lower threshold (1.2x, relations < -10)
            const bool easyAI = (this->m_difficulty == aoc::ui::AIDifficulty::Easy);
            const bool hardAI = (this->m_difficulty == aoc::ui::AIDifficulty::Hard);

            const float militaryRatioThreshold = hardAI ? 1.2f : 1.5f;
            const int32_t relationThreshold = hardAI ? -10 : -20;
            const int32_t warChanceThreshold = hardAI ? 5 : 3;  // higher = more likely

            if (!easyAI && ourMilitary > 0 && theirMilitary >= 0 &&
                static_cast<float>(ourMilitary) > militaryRatioThreshold * static_cast<float>(theirMilitary) &&
                relationScore < relationThreshold) {
                // Use deterministic pseudo-random based on player IDs and military counts
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

            // Propose open borders with friendly neighbors (relations > 10)
            if (!rel.hasOpenBorders && relationScore > 10) {
                diplomacy.grantOpenBorders(this->m_player, other);
                LOG_INFO("AI %u Opened borders with player %u (relations %d)",
                         static_cast<unsigned>(this->m_player),
                         static_cast<unsigned>(other), relationScore);
            }

            // Propose economic alliance if relations are friendly and
            // there is complementary supply/demand (market-driven).
            // Check if the other player has goods we lack at high prices.
            if (!rel.hasEconomicAlliance && relationScore > 20) {
                int32_t complementaryGoods = 0;
                const uint16_t totalGoods = market.goodsCount();
                for (uint16_t g = 0; g < totalGoods; ++g) {
                    const int32_t currentPrice = market.price(g);
                    const int32_t basePrice = goodDef(g).basePrice;
                    if (basePrice > 0) {
                        const float priceRatio = static_cast<float>(currentPrice) /
                                                 static_cast<float>(basePrice);
                        // Count goods with significant price deviation as trade opportunities
                        if (priceRatio > 1.3f || priceRatio < 0.7f) {
                            ++complementaryGoods;
                        }
                    }
                }
                // Form economic alliance if there are enough trade opportunities
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
// Builder management: auto-improve tiles near owned cities
//
// For each builder:
//   1. If on an unimproved owned tile: build best improvement.
//   2. If not: find nearest unimproved owned tile near any city, move there.
//   3. If no unimproved tiles exist: do nothing.
// ============================================================================

void AIController::manageBuildersAndImprovements(aoc::ecs::World& world,
                                                  aoc::map::HexGrid& grid) {
    aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool == nullptr) {
        return;
    }

    // Collect owned city locations for proximity checks
    std::vector<hex::AxialCoord> cityLocations;
    const aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].owner == this->m_player) {
                cityLocations.push_back(cityPool->data()[i].location);
            }
        }
    }

    if (cityLocations.empty()) {
        return;
    }

    // Collect ALL builder units owned by this player
    struct BuilderInfo {
        EntityId entity;
        UnitComponent unit;
    };
    std::vector<BuilderInfo> builders;
    for (uint32_t i = 0; i < unitPool->size(); ++i) {
        const UnitComponent& unit = unitPool->data()[i];
        if (unit.owner == this->m_player &&
            unitTypeDef(unit.typeId).unitClass == UnitClass::Civilian &&
            unit.chargesRemaining != 0) {
            builders.push_back({unitPool->entities()[i], unit});
        }
    }

    // Track tiles we've already targeted so multiple builders don't go to the same tile
    std::unordered_set<hex::AxialCoord> targetedTiles;

    for (const BuilderInfo& builder : builders) {
        if (!world.isAlive(builder.entity)) {
            continue;
        }
        if (builder.unit.movementRemaining <= 0) {
            continue;
        }

        // Step 1: Check if current tile can be improved
        const int32_t currentIdx = grid.toIndex(builder.unit.position);
        if (grid.owner(currentIdx) == this->m_player &&
            grid.improvement(currentIdx) == aoc::map::ImprovementType::None &&
            grid.movementCost(currentIdx) > 0) {
            const aoc::map::ImprovementType bestImpr = bestImprovementForTile(grid, currentIdx);
            if (bestImpr != aoc::map::ImprovementType::None &&
                canPlaceImprovement(grid, currentIdx, bestImpr)) {
                grid.setImprovement(currentIdx, bestImpr);
                UnitComponent* liveUnit = world.tryGetComponent<UnitComponent>(builder.entity);
                if (liveUnit != nullptr && liveUnit->chargesRemaining > 0) {
                    --liveUnit->chargesRemaining;
                    if (liveUnit->chargesRemaining == 0) {
                        world.destroyEntity(builder.entity);
                        LOG_INFO("AI %u Builder exhausted after improving (%d,%d)",
                                 static_cast<unsigned>(this->m_player),
                                 builder.unit.position.q, builder.unit.position.r);
                        continue;
                    }
                }
                LOG_INFO("AI %u Builder improved tile (%d,%d)",
                         static_cast<unsigned>(this->m_player),
                         builder.unit.position.q, builder.unit.position.r);
                continue;
            }
        }

        // Step 2: Find nearest unimproved owned tile near any city
        hex::AxialCoord bestTarget = builder.unit.position;
        int32_t bestDist = std::numeric_limits<int32_t>::max();

        for (const hex::AxialCoord& cityLoc : cityLocations) {
            // Check tiles in ring-1 and ring-2 around each city
            std::vector<hex::AxialCoord> cityTiles;
            cityTiles.reserve(18);
            hex::ring(cityLoc, 1, std::back_inserter(cityTiles));
            hex::ring(cityLoc, 2, std::back_inserter(cityTiles));

            for (const hex::AxialCoord& tile : cityTiles) {
                if (!grid.isValid(tile)) {
                    continue;
                }
                const int32_t tileIdx = grid.toIndex(tile);
                if (grid.owner(tileIdx) != this->m_player) {
                    continue;
                }
                if (grid.improvement(tileIdx) != aoc::map::ImprovementType::None) {
                    continue;
                }
                if (grid.movementCost(tileIdx) <= 0) {
                    continue;
                }
                if (bestImprovementForTile(grid, tileIdx) == aoc::map::ImprovementType::None) {
                    continue;
                }
                // Don't target tiles already targeted by another builder
                if (targetedTiles.find(tile) != targetedTiles.end()) {
                    continue;
                }

                const int32_t dist = hex::distance(builder.unit.position, tile);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestTarget = tile;
                }
            }
        }

        // Move toward the target tile
        if (bestTarget != builder.unit.position && bestDist < std::numeric_limits<int32_t>::max()) {
            targetedTiles.insert(bestTarget);
            orderUnitMove(world, builder.entity, bestTarget, grid);
            moveUnitAlongPath(world, builder.entity, grid);
        }
    }
}

// ============================================================================
// Government management: adopt best available government, fill policy slots
// ============================================================================

void AIController::manageGovernment(aoc::ecs::World& world) {
    world.forEach<PlayerGovernmentComponent>(
        [this](EntityId, PlayerGovernmentComponent& gov) {
            if (gov.owner != this->m_player) {
                return;
            }

            // Adopt the best (highest-index) unlocked government
            GovernmentType bestGov = gov.government;
            for (uint8_t g = 0; g < GOVERNMENT_COUNT; ++g) {
                GovernmentType gt = static_cast<GovernmentType>(g);
                if (gov.isGovernmentUnlocked(gt)) {
                    bestGov = gt;  // Higher index = more advanced
                }
            }
            if (bestGov != gov.government) {
                gov.government = bestGov;
                // Clear policies when switching government
                for (uint8_t s = 0; s < MAX_POLICY_SLOTS; ++s) {
                    gov.activePolicies[s] = EMPTY_POLICY_SLOT;
                }
                LOG_INFO("AI %u Adopted government: %.*s",
                         static_cast<unsigned>(this->m_player),
                         static_cast<int>(governmentDef(bestGov).name.size()),
                         governmentDef(bestGov).name.data());
            }

            // Fill policy slots with best available cards
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

            // Track which policies we've already assigned
            std::array<bool, POLICY_CARD_COUNT> assigned{};

            for (const SlotInfo& slot : slots) {
                int8_t bestPolicy = EMPTY_POLICY_SLOT;
                float bestValue = -1.0f;

                for (uint8_t p = 0; p < POLICY_CARD_COUNT; ++p) {
                    if (!gov.isPolicyUnlocked(p) || assigned[static_cast<std::size_t>(p)]) {
                        continue;
                    }
                    const PolicyCardDef& pdef = policyCardDef(p);
                    // Card must match slot type or be a wildcard slot
                    if (pdef.slotType != slot.slotType &&
                        slot.slotType != PolicySlotType::Wildcard) {
                        continue;
                    }
                    // Score policy by total modifier value
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
// Economy management: sell expensive surplus, buy cheap needed goods
//
// The AI evaluates market prices for all goods it holds:
//   - If a good's price is > 1.5x its base price: AI is incentivized to sell
//   - If a good's price is < 0.7x its base price: AI wants to buy
// This creates natural comparative advantage trade flows.
// ============================================================================

void AIController::manageEconomy(aoc::ecs::World& world,
                                  DiplomacyManager& diplomacy,
                                  const Market& market) {
    // Aggregate the AI's total stockpile across all cities
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
        for (const auto& [goodId, amount] : stockpile->goods) {
            totalStockpile[goodId] += amount;
        }
    }

    // Identify goods to sell (price > 1.5x base) and goods to buy (price < 0.7x base)
    constexpr float SELL_THRESHOLD = 1.5f;
    constexpr float BUY_THRESHOLD  = 0.7f;
    constexpr int32_t MIN_SURPLUS_TO_SELL = 3;

    struct TradeDesire {
        uint16_t goodId;
        int32_t  amount;
        bool     wantToSell;  ///< true = sell, false = buy
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

        // Sell if we have surplus and the price is high
        if (priceRatio > SELL_THRESHOLD && held > MIN_SURPLUS_TO_SELL) {
            const int32_t sellAmount = held / 2;  // Sell half of surplus
            if (sellAmount > 0) {
                desires.push_back({g, sellAmount, true});
            }
        }
        // Buy if the price is low and we have none (deficit)
        else if (priceRatio < BUY_THRESHOLD && held == 0) {
            desires.push_back({g, 2, false});  // Request modest amount
        }
    }

    if (desires.empty()) {
        return;
    }

    // For each sell desire, find a trade partner who might want to buy
    const uint8_t playerCount = diplomacy.playerCount();
    for (const TradeDesire& desire : desires) {
        if (!desire.wantToSell) {
            continue;  // Buy desires are handled via diplomacy actions
        }

        for (uint8_t other = 0; other < playerCount; ++other) {
            if (other == this->m_player) {
                continue;
            }
            const PairwiseRelation& rel = diplomacy.relation(this->m_player, other);
            if (rel.isAtWar || rel.hasEmbargo) {
                continue;
            }

            // Only trade with neutral or better relations
            if (rel.totalScore() < -10) {
                continue;
            }

            // Check if the partner lacks this good (would value it highly)
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

            // Partner is a good trade target if they have little of this good
            if (partnerHoldings < 2) {
                LOG_INFO("AI %u wants to sell %d of good %u (price ratio %.2f) to player %u",
                         static_cast<unsigned>(this->m_player),
                         desire.amount,
                         static_cast<unsigned>(desire.goodId),
                         static_cast<double>(static_cast<float>(market.price(desire.goodId)) /
                                             static_cast<float>(goodDef(desire.goodId).basePrice)),
                         static_cast<unsigned>(other));

                // Improve relations slightly for wanting to trade (trade bonus)
                diplomacy.addModifier(this->m_player, other,
                    RelationModifier{"Trade interest", 1, 10});
                break;  // One partner per good per turn
            }
        }
    }
}

// ============================================================================
// Action scoring (for future expansion)
// ============================================================================

std::vector<ScoredAction> AIController::evaluateActions(
    const aoc::ecs::World& /*world*/,
    const aoc::map::HexGrid& /*grid*/,
    const DiplomacyManager& /*diplomacy*/,
    const Market& /*market*/) const {
    // Full utility-based evaluation will be implemented as the game matures.
    // For now, actions are hard-coded priorities in executeTurn().
    return {};
}

// ============================================================================
// Monetary system management
//
// AI strategy for monetary transitions:
//   1. Transition to Commodity Money as soon as any coins are held.
//   2. Transition to Gold Standard when gold coins + Banking tech available.
//   3. Transition to Fiat only if GDP rank is top half and economy is stable.
//   4. Never debase currency (AI plays conservatively with money).
// ============================================================================

void AIController::manageMonetarySystem(aoc::ecs::World& world,
                                         const DiplomacyManager& /*diplomacy*/) {
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

    // Count cities
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

    // Count trade partners
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

    // Compute GDP rank
    int32_t gdpRank = 1;
    int32_t playerCount = static_cast<int32_t>(monetaryPool->size());
    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        const MonetaryStateComponent& other = monetaryPool->data()[i];
        if (other.owner != this->m_player && other.gdp > myState->gdp) {
            ++gdpRank;
        }
    }

    // Determine next transition target
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
            return;  // Already on fiat, nothing to transition to
    }

    // AI only transitions to fiat if GDP rank is top 2 (conservative)
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
    }
}

} // namespace aoc::sim::ai
