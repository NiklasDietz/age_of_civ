/**
 * @file AIController.cpp
 * @brief AI decision-making implementation.
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
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Pathfinding.hpp"
#include "aoc/ecs/World.hpp"

#include <limits>

namespace aoc::sim::ai {

AIController::AIController(PlayerId player)
    : m_player(player)
{
}

void AIController::executeTurn(aoc::ecs::World& world,
                                aoc::map::HexGrid& grid,
                                DiplomacyManager& diplomacy,
                                const Market& market,
                                aoc::Random& rng) {
    this->selectResearch(world);
    this->manageGovernment(world);
    this->executeCityActions(world, grid);
    this->manageBuildersAndImprovements(world, grid);
    this->executeUnitActions(world, grid, rng);
    this->executeDiplomacyActions(world, diplomacy, market);

    // Refresh movement so units have moved for this turn
    refreshMovement(world, this->m_player);
}

// ============================================================================
// Research selection: prioritize techs that unlock buildings > military > cheapest
// ============================================================================

void AIController::selectResearch(aoc::ecs::World& world) {
    // Count military units to detect threat level
    int32_t militaryCount = 0;
    const aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool != nullptr) {
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            if (unitPool->data()[i].owner == this->m_player &&
                isMilitary(unitTypeDef(unitPool->data()[i].typeId).unitClass)) {
                ++militaryCount;
            }
        }
    }
    const bool threatened = militaryCount < 2;

    world.forEach<PlayerTechComponent>(
        [this, threatened](EntityId, PlayerTechComponent& tech) {
            if (tech.owner != this->m_player) {
                return;
            }
            if (tech.currentResearch.isValid()) {
                return;  // Already researching
            }
            std::vector<TechId> available = tech.availableTechs();
            if (available.empty()) {
                return;
            }

            // Score each available tech: higher is better
            // Priority: building-unlocking techs > military unit techs (if threatened) > cheapest
            TechId best = available[0];
            int32_t bestScore = std::numeric_limits<int32_t>::min();

            for (const TechId& tid : available) {
                const TechDef& def = techDef(tid);
                int32_t score = 0;

                // Techs that unlock buildings are highest priority
                if (!def.unlockedBuildings.empty()) {
                    score += 10000;
                }

                // Techs that unlock military units, weighted by threat
                if (!def.unlockedUnits.empty()) {
                    score += threatened ? 8000 : 3000;
                }

                // Techs that unlock goods add moderate value
                if (!def.unlockedGoods.empty()) {
                    score += 2000;
                }

                // Prefer cheaper techs as tiebreaker (invert cost so cheaper = higher score)
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

    world.forEach<PlayerCivicComponent>(
        [this](EntityId, PlayerCivicComponent& civic) {
            if (civic.owner != this->m_player) {
                return;
            }
            if (civic.currentResearch.isValid()) {
                return;
            }
            // Prefer civics that unlock governments or policies, then cheapest
            uint16_t count = civicCount();
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
// Unit actions: settlers found cities, military move toward enemies,
// scouts explore away from known territory
// ============================================================================

void AIController::executeUnitActions(aoc::ecs::World& world,
                                       aoc::map::HexGrid& grid,
                                       aoc::Random& rng) {
    aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool == nullptr) {
        return;
    }

    // Collect owned units (iterate by copy since we may modify during iteration)
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

    // Cache enemy unit positions for military AI targeting
    struct EnemyInfo {
        hex::AxialCoord position;
        EntityId entity;
    };
    std::vector<EnemyInfo> enemyUnits;
    for (uint32_t i = 0; i < unitPool->size(); ++i) {
        if (unitPool->data()[i].owner != this->m_player) {
            enemyUnits.push_back({unitPool->data()[i].position, unitPool->entities()[i]});
        }
    }

    for (const UnitInfo& info : ownedUnits) {
        if (!world.isAlive(info.entity)) {
            continue;
        }

        const UnitTypeDef& def = unitTypeDef(info.unit.typeId);

        // Settlers: found city at current location
        if (def.unitClass == UnitClass::Settler) {
            EntityId cityEntity = world.createEntity();
            world.addComponent<CityComponent>(
                cityEntity,
                CityComponent::create(this->m_player, info.unit.position, "AI City"));
            world.addComponent<ProductionQueueComponent>(
                cityEntity, ProductionQueueComponent{});

            CityDistrictsComponent districts{};
            CityDistrictsComponent::PlacedDistrict center;
            center.type = DistrictType::CityCenter;
            center.location = info.unit.position;
            districts.districts.push_back(std::move(center));
            world.addComponent<CityDistrictsComponent>(
                cityEntity, std::move(districts));

            claimInitialTerritory(grid, info.unit.position, this->m_player);
            world.destroyEntity(info.entity);
            LOG_INFO("AI %u Founded city at (%d,%d)",
                     static_cast<unsigned>(this->m_player),
                     info.unit.position.q, info.unit.position.r);
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

        // Check for adjacent enemies to attack (all combat units)
        if (isMilitary(def.unitClass)) {
            bool attacked = false;
            for (const hex::AxialCoord& nbr : neighborTiles) {
                if (!grid.isValid(nbr)) {
                    continue;
                }
                aoc::ecs::ComponentPool<UnitComponent>* pool = world.getPool<UnitComponent>();
                if (pool == nullptr) {
                    break;
                }
                for (uint32_t j = 0; j < pool->size(); ++j) {
                    if (pool->data()[j].position == nbr && pool->data()[j].owner != this->m_player) {
                        resolveMeleeCombat(world, rng, grid, info.entity, pool->entities()[j]);
                        attacked = true;
                        break;
                    }
                }
                if (attacked) {
                    break;
                }
            }

            if (attacked || !world.isAlive(info.entity)) {
                continue;
            }

            // Military units: move toward nearest enemy unit
            int32_t closestDist = std::numeric_limits<int32_t>::max();
            hex::AxialCoord closestEnemyPos = info.unit.position;
            for (const EnemyInfo& enemy : enemyUnits) {
                int32_t dist = hex::distance(info.unit.position, enemy.position);
                if (dist < closestDist) {
                    closestDist = dist;
                    closestEnemyPos = enemy.position;
                }
            }

            if (closestDist < std::numeric_limits<int32_t>::max() && closestDist > 1) {
                // Move toward the enemy -- pick the neighbor closest to the enemy
                hex::AxialCoord bestMove = info.unit.position;
                int32_t bestDist = closestDist;
                for (const hex::AxialCoord& nbr : neighborTiles) {
                    if (!grid.isValid(nbr) || grid.movementCost(grid.toIndex(nbr)) <= 0) {
                        continue;
                    }
                    int32_t dist = hex::distance(nbr, closestEnemyPos);
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestMove = nbr;
                    }
                }
                if (bestMove != info.unit.position) {
                    orderUnitMove(world, info.entity, bestMove, grid);
                    moveUnitAlongPath(world, info.entity, grid);
                }
            } else if (closestDist == std::numeric_limits<int32_t>::max()) {
                // No enemies found -- random patrol
                std::vector<hex::AxialCoord> validMoves;
                for (const hex::AxialCoord& nbr : neighborTiles) {
                    if (grid.isValid(nbr) && grid.movementCost(grid.toIndex(nbr)) > 0) {
                        validMoves.push_back(nbr);
                    }
                }
                if (!validMoves.empty()) {
                    int32_t idx = rng.nextInt(0, static_cast<int32_t>(validMoves.size()) - 1);
                    orderUnitMove(world, info.entity, validMoves[static_cast<std::size_t>(idx)], grid);
                    moveUnitAlongPath(world, info.entity, grid);
                }
            }
            continue;
        }

        // Scouts: move away from owned territory (prefer unexplored directions)
        if (def.unitClass == UnitClass::Scout) {
            // For each neighbor, count how many of its own neighbors are owned by us.
            // Pick the neighbor with the fewest owned tiles nearby (moving away from our lands).
            hex::AxialCoord bestMove = info.unit.position;
            int32_t lowestOwnedCount = std::numeric_limits<int32_t>::max();

            for (const hex::AxialCoord& nbr : neighborTiles) {
                if (!grid.isValid(nbr) || grid.movementCost(grid.toIndex(nbr)) <= 0) {
                    continue;
                }
                // Count owned tiles around this candidate
                int32_t ownedNearby = 0;
                const std::array<hex::AxialCoord, 6> nbrNeighbors = hex::neighbors(nbr);
                for (const hex::AxialCoord& nn : nbrNeighbors) {
                    if (grid.isValid(nn) && grid.owner(grid.toIndex(nn)) == this->m_player) {
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
        }
    }
}

// ============================================================================
// City actions: priority-based production queue management
// Priority: Builder (if no improvements) > military (if count < 2+cities)
//           > buildings > warriors (fallback)
// ============================================================================

void AIController::executeCityActions(aoc::ecs::World& world,
                                       aoc::map::HexGrid& grid) {
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    // Count owned units by class
    int32_t militaryCount = 0;
    int32_t builderCount = 0;
    int32_t ownedCityCount = 0;
    const aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool != nullptr) {
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            if (unitPool->data()[i].owner != this->m_player) {
                continue;
            }
            const UnitTypeDef& def = unitTypeDef(unitPool->data()[i].typeId);
            if (isMilitary(def.unitClass)) {
                ++militaryCount;
            }
            if (def.unitClass == UnitClass::Civilian) {
                ++builderCount;
            }
        }
    }

    // Count owned cities
    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        if (cityPool->data()[i].owner == this->m_player) {
            ++ownedCityCount;
        }
    }

    // Check if any owned tile near our cities lacks improvements
    bool needsBuilder = false;
    if (builderCount == 0) {
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
                int32_t idx = grid.toIndex(nbr);
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

    const int32_t desiredMilitary = 2 + ownedCityCount;
    const bool needsMilitary = militaryCount < desiredMilitary;

    for (uint32_t ci = 0; ci < cityPool->size(); ++ci) {
        CityComponent& city = cityPool->data()[ci];
        if (city.owner != this->m_player) {
            continue;
        }
        EntityId cityEntity = cityPool->entities()[ci];
        ProductionQueueComponent* queue =
            world.tryGetComponent<ProductionQueueComponent>(cityEntity);
        if (queue == nullptr || !queue->isEmpty()) {
            continue;
        }

        ProductionQueueItem item{};

        // Priority 1: Builder if we need improvements and have no builders
        if (needsBuilder) {
            item.type = ProductionItemType::Unit;
            item.itemId = 5;  // Builder
            item.name = "Builder";
            item.totalCost = static_cast<float>(unitTypeDef(UnitTypeId{5}).productionCost);
            item.progress = 0.0f;
            needsBuilder = false;  // Only enqueue one builder across all cities
            LOG_INFO("AI %u Enqueued Builder in %s",
                     static_cast<unsigned>(this->m_player), city.name.c_str());
        }
        // Priority 2: Military units if below desired count
        else if (needsMilitary) {
            item.type = ProductionItemType::Unit;
            item.itemId = 0;  // Warrior
            item.name = "Warrior";
            item.totalCost = static_cast<float>(unitTypeDef(UnitTypeId{0}).productionCost);
            item.progress = 0.0f;
            LOG_INFO("AI %u Enqueued Warrior in %s",
                     static_cast<unsigned>(this->m_player), city.name.c_str());
        }
        // Priority 3: Buildings -- pick first affordable building we lack
        else {
            const CityDistrictsComponent* districts =
                world.tryGetComponent<CityDistrictsComponent>(cityEntity);
            bool enqueuedBuilding = false;
            if (districts != nullptr) {
                for (const BuildingDef& bdef : BUILDING_DEFS) {
                    if (districts->hasBuilding(bdef.id)) {
                        continue;
                    }
                    // Check if we have the required district
                    if (!districts->hasDistrict(bdef.requiredDistrict)) {
                        continue;
                    }
                    item.type = ProductionItemType::Building;
                    item.itemId = bdef.id.value;
                    item.name = std::string(bdef.name);
                    item.totalCost = static_cast<float>(bdef.productionCost);
                    item.progress = 0.0f;
                    enqueuedBuilding = true;
                    LOG_INFO("AI %u Enqueued %.*s in %s",
                             static_cast<unsigned>(this->m_player),
                             static_cast<int>(bdef.name.size()), bdef.name.data(),
                             city.name.c_str());
                    break;
                }
            }

            // Fallback: produce a warrior
            if (!enqueuedBuilding) {
                item.type = ProductionItemType::Unit;
                item.itemId = 0;  // Warrior
                item.name = "Warrior";
                item.totalCost = static_cast<float>(unitTypeDef(UnitTypeId{0}).productionCost);
                item.progress = 0.0f;
                LOG_INFO("AI %u Enqueued Warrior (fallback) in %s",
                         static_cast<unsigned>(this->m_player), city.name.c_str());
            }
        }

        queue->queue.push_back(std::move(item));
    }
}

// ============================================================================
// Diplomacy: if military > 1.5x neighbor declare war (30% chance),
//            if losing war propose peace
// ============================================================================

void AIController::executeDiplomacyActions(aoc::ecs::World& world,
                                            DiplomacyManager& diplomacy,
                                            const Market& /*market*/) {
    const aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool == nullptr) {
        return;
    }

    // Count military units per player
    constexpr uint8_t MAX_PLAYERS = 16;
    std::array<int32_t, MAX_PLAYERS> militaryCounts{};
    for (uint32_t i = 0; i < unitPool->size(); ++i) {
        PlayerId owner = unitPool->data()[i].owner;
        if (owner < MAX_PLAYERS && isMilitary(unitTypeDef(unitPool->data()[i].typeId).unitClass)) {
            ++militaryCounts[static_cast<std::size_t>(owner)];
        }
    }

    const int32_t ourMilitary = militaryCounts[static_cast<std::size_t>(this->m_player)];
    const uint8_t playerCount = diplomacy.playerCount();

    for (uint8_t other = 0; other < playerCount; ++other) {
        if (other == this->m_player) {
            continue;
        }

        const PairwiseRelation& rel = diplomacy.relation(this->m_player, other);
        const int32_t theirMilitary = militaryCounts[static_cast<std::size_t>(other)];

        if (rel.isAtWar) {
            // If losing war (they have more military), propose peace
            if (theirMilitary > ourMilitary) {
                diplomacy.makePeace(this->m_player, other);
                LOG_INFO("AI %u Proposed peace with player %u (outmilitaried %d vs %d)",
                         static_cast<unsigned>(this->m_player),
                         static_cast<unsigned>(other),
                         ourMilitary, theirMilitary);
            }
        } else {
            // Consider declaring war if we have > 1.5x their military
            // and relations are hostile/unfriendly
            if (ourMilitary > 0 && theirMilitary >= 0 &&
                static_cast<float>(ourMilitary) > 1.5f * static_cast<float>(theirMilitary) &&
                rel.totalScore() < 0) {
                // 30% chance to declare war (use a simple hash to be deterministic-ish per turn)
                // We use the relation score modulus to approximate randomness without needing rng
                int32_t warChance = ((ourMilitary * 7 + theirMilitary * 13 +
                                      static_cast<int32_t>(this->m_player) * 31) % 10);
                if (warChance < 3) {
                    diplomacy.declareWar(this->m_player, other);
                    LOG_INFO("AI %u Declared war on player %u (military %d vs %d)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             ourMilitary, theirMilitary);
                }
            }
        }
    }
}

// ============================================================================
// Builder management: auto-improve tiles near owned cities
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

    // Collect builder units
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

    for (const BuilderInfo& builder : builders) {
        if (!world.isAlive(builder.entity)) {
            continue;
        }
        if (builder.unit.movementRemaining <= 0) {
            continue;
        }

        // Check if current tile can be improved
        const int32_t currentIdx = grid.toIndex(builder.unit.position);
        if (grid.owner(currentIdx) == this->m_player &&
            grid.improvement(currentIdx) == aoc::map::ImprovementType::None &&
            grid.movementCost(currentIdx) > 0) {
            // Build the best improvement on this tile
            aoc::map::ImprovementType bestImpr = bestImprovementForTile(grid, currentIdx);
            if (bestImpr != aoc::map::ImprovementType::None &&
                canPlaceImprovement(grid, currentIdx, bestImpr)) {
                grid.setImprovement(currentIdx, bestImpr);
                // Decrement charges
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

        // Find nearest unimproved owned tile near a city
        hex::AxialCoord bestTarget = builder.unit.position;
        int32_t bestDist = std::numeric_limits<int32_t>::max();

        for (const hex::AxialCoord& cityLoc : cityLocations) {
            const std::array<hex::AxialCoord, 6> cityNeighbors = hex::neighbors(cityLoc);
            for (const hex::AxialCoord& nbr : cityNeighbors) {
                if (!grid.isValid(nbr)) {
                    continue;
                }
                int32_t nbrIdx = grid.toIndex(nbr);
                if (grid.owner(nbrIdx) != this->m_player) {
                    continue;
                }
                if (grid.improvement(nbrIdx) != aoc::map::ImprovementType::None) {
                    continue;
                }
                if (grid.movementCost(nbrIdx) <= 0) {
                    continue;
                }
                // Check that some improvement is valid here
                if (bestImprovementForTile(grid, nbrIdx) == aoc::map::ImprovementType::None) {
                    continue;
                }
                int32_t dist = hex::distance(builder.unit.position, nbr);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestTarget = nbr;
                }
            }
        }

        // Move toward the target tile
        if (bestTarget != builder.unit.position && bestDist < std::numeric_limits<int32_t>::max()) {
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

            // Build list of slot types for each slot index
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

} // namespace aoc::sim::ai
