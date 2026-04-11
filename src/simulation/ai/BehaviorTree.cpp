/**
 * @file BehaviorTree.cpp
 * @brief Behavior tree construction, blackboard refresh, and action implementations.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/ai/BehaviorTree.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim::bt {

// ============================================================================
// Blackboard refresh
// ============================================================================

void refreshBlackboard(Blackboard& bb) {
    if (bb.world == nullptr) { return; }
    aoc::game::GameState& gameState = *bb.world;

    bb.ownedCities = 0;
    bb.totalPopulation = 0;
    bb.militaryUnits = 0;
    bb.builderUnits = 0;
    bb.settlerUnits = 0;
    bb.treasury = 0;
    bb.techsResearched = 0;

    // Count cities and population
    const aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].owner == bb.player) {
                ++bb.ownedCities;
                bb.totalPopulation += cityPool->data()[i].population;
            }
        }
    }

    // Count units by type
    const aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool != nullptr) {
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            const UnitComponent& unit = unitPool->data()[i];
            if (unit.owner != bb.player) { continue; }
            UnitClass cls = unitTypeDef(unit.typeId).unitClass;
            if (isMilitary(cls)) { ++bb.militaryUnits; }
            if (cls == UnitClass::Civilian && unit.typeId.value == 5) { ++bb.builderUnits; }
            if (cls == UnitClass::Settler) { ++bb.settlerUnits; }
        }
    }

    // Treasury
    const aoc::ecs::ComponentPool<MonetaryStateComponent>* mPool =
        world.getPool<MonetaryStateComponent>();
    if (mPool != nullptr) {
        for (uint32_t i = 0; i < mPool->size(); ++i) {
            if (mPool->data()[i].owner == bb.player) {
                bb.treasury = mPool->data()[i].treasury;
                break;
            }
        }
    }

    // Techs
    const aoc::ecs::ComponentPool<PlayerTechComponent>* tPool =
        world.getPool<PlayerTechComponent>();
    if (tPool != nullptr) {
        for (uint32_t i = 0; i < tPool->size(); ++i) {
            if (tPool->data()[i].owner != bb.player) { continue; }
            for (std::size_t b = 0; b < tPool->data()[i].completedTechs.size(); ++b) {
                if (tPool->data()[i].completedTechs[b]) { ++bb.techsResearched; }
            }
            break;
        }
    }

    // Compute personality-driven targets
    if (bb.behavior != nullptr) {
        AIScaledTargets targets = computeScaledTargets(*bb.behavior);
        bb.targetMaxCities = targets.maxCities;
        bb.desiredMilitary = bb.ownedCities * targets.desiredMilitaryPerCity + 2;
    }

    // Threat assessment: enemy military near our cities
    bb.isThreatened = bb.militaryUnits < 3 && bb.ownedCities > 0;

    // Tick counter for weighted chance nodes
    bb.set("_tick_counter", bb.get("_tick_counter", 0.0f) + 1.0f);
}

// ============================================================================
// Action node factories
// ============================================================================

/// Action: enqueue a settler in the best city
static NodePtr actionBuildSettler() {
    return std::make_unique<ExecuteAction>("BuildSettler",
        [](Blackboard& bb) -> Status {
            if (bb.world == nullptr) { return Status::Failure; }
            aoc::game::GameState& gameState = *bb.world;

            const aoc::ecs::ComponentPool<CityComponent>* cityPool =
                world.getPool<CityComponent>();
            if (cityPool == nullptr) { return Status::Failure; }
            aoc::ecs::World& world = gameState.legacyWorld();

            // Find best city to produce settler (highest population)
            EntityId bestCity = NULL_ENTITY;
            int32_t bestPop = 0;
            for (uint32_t i = 0; i < cityPool->size(); ++i) {
                if (cityPool->data()[i].owner != bb.player) { continue; }
                ProductionQueueComponent* q =
                    world.tryGetComponent<ProductionQueueComponent>(cityPool->entities()[i]);
                if (q == nullptr || !q->isEmpty()) { continue; }
                if (cityPool->data()[i].population > bestPop) {
                    bestPop = cityPool->data()[i].population;
                    bestCity = cityPool->entities()[i];
                }
            }
            if (bestCity == NULL_ENTITY || bestPop < 2) { return Status::Failure; }

            ProductionQueueComponent* queue =
                world.tryGetComponent<ProductionQueueComponent>(bestCity);
            if (queue == nullptr) { return Status::Failure; }

            ProductionQueueItem item{};
            item.type = ProductionItemType::Unit;
            item.itemId = 3;  // Settler
            item.name = "Settler";
            item.totalCost = static_cast<float>(unitTypeDef(UnitTypeId{3}).productionCost)
                           * GamePace::instance().costMultiplier;
            item.progress = 0.0f;
            queue->queue.push_back(std::move(item));
            return Status::Success;
        });
}

/// Action: enqueue a military unit in a city with empty queue
static NodePtr actionBuildMilitary() {
    return std::make_unique<ExecuteAction>("BuildMilitary",
        [](Blackboard& bb) -> Status {
            if (bb.world == nullptr) { return Status::Failure; }
            aoc::game::GameState& gameState = *bb.world;

            const aoc::ecs::ComponentPool<CityComponent>* cityPool =
                world.getPool<CityComponent>();
            if (cityPool == nullptr) { return Status::Failure; }
            aoc::ecs::World& world = gameState.legacyWorld();

            // Find UnitTypeId 0 (Warrior) as default military unit
            UnitTypeId militaryId{0};

            for (uint32_t i = 0; i < cityPool->size(); ++i) {
                if (cityPool->data()[i].owner != bb.player) { continue; }
                ProductionQueueComponent* q =
                    world.tryGetComponent<ProductionQueueComponent>(cityPool->entities()[i]);
                if (q == nullptr || !q->isEmpty()) { continue; }

                ProductionQueueItem item{};
                item.type = ProductionItemType::Unit;
                item.itemId = militaryId.value;
                item.name = std::string(unitTypeDef(militaryId).name);
                item.totalCost = static_cast<float>(unitTypeDef(militaryId).productionCost)
                               * GamePace::instance().costMultiplier;
                item.progress = 0.0f;
                q->queue.push_back(std::move(item));
                return Status::Success;
            }
            return Status::Failure;
        });
}

/// Action: enqueue a builder
static NodePtr actionBuildBuilder() {
    return std::make_unique<ExecuteAction>("BuildBuilder",
        [](Blackboard& bb) -> Status {
            if (bb.world == nullptr) { return Status::Failure; }
            aoc::game::GameState& gameState = *bb.world;

            const aoc::ecs::ComponentPool<CityComponent>* cityPool =
                world.getPool<CityComponent>();
            if (cityPool == nullptr) { return Status::Failure; }
            aoc::ecs::World& world = gameState.legacyWorld();

            for (uint32_t i = 0; i < cityPool->size(); ++i) {
                if (cityPool->data()[i].owner != bb.player) { continue; }
                ProductionQueueComponent* q =
                    world.tryGetComponent<ProductionQueueComponent>(cityPool->entities()[i]);
                if (q == nullptr || !q->isEmpty()) { continue; }

                ProductionQueueItem item{};
                item.type = ProductionItemType::Unit;
                item.itemId = 5;  // Builder
                item.name = "Builder";
                item.totalCost = static_cast<float>(unitTypeDef(UnitTypeId{5}).productionCost)
                               * GamePace::instance().costMultiplier;
                item.progress = 0.0f;
                q->queue.push_back(std::move(item));
                return Status::Success;
            }
            return Status::Failure;
        });
}

/// Action: build a district or building in a city with empty queue
static NodePtr actionBuildInfrastructure() {
    return std::make_unique<ExecuteAction>("BuildInfrastructure",
        [](Blackboard& bb) -> Status {
            if (bb.world == nullptr) { return Status::Failure; }
            aoc::game::GameState& gameState = *bb.world;

            const aoc::ecs::ComponentPool<CityComponent>* cityPool =
                world.getPool<CityComponent>();
            if (cityPool == nullptr) { return Status::Failure; }
            aoc::ecs::World& world = gameState.legacyWorld();

            for (uint32_t i = 0; i < cityPool->size(); ++i) {
                if (cityPool->data()[i].owner != bb.player) { continue; }
                EntityId cityEntity = cityPool->entities()[i];
                ProductionQueueComponent* q =
                    world.tryGetComponent<ProductionQueueComponent>(cityEntity);
                if (q == nullptr || !q->isEmpty()) { continue; }

                // Try buildings first
                static constexpr uint16_t BUILDING_PRIO[] = {
                    16, 15, 1, 7, 6, 24, 0, 20, 19, 3, 26, 4, 12
                };
                for (uint16_t bidx : BUILDING_PRIO) {
                    if (bidx >= BUILDING_DEFS.size()) { continue; }
                    const BuildingDef& bdef = BUILDING_DEFS[bidx];
                    if (!canBuildBuilding(world, bb.player, cityEntity, bdef.id)) { continue; }

                    ProductionQueueItem item{};
                    item.type = ProductionItemType::Building;
                    item.itemId = bdef.id.value;
                    item.name = std::string(bdef.name);
                    item.totalCost = static_cast<float>(bdef.productionCost)
                                   * GamePace::instance().costMultiplier;
                    item.progress = 0.0f;
                    q->queue.push_back(std::move(item));
                    return Status::Success;
                }

                // Try districts
                static constexpr DistrictType DISTRICT_PRIO[] = {
                    DistrictType::Commercial, DistrictType::Campus,
                    DistrictType::Industrial, DistrictType::Encampment,
                };
                const CityDistrictsComponent* districts =
                    world.tryGetComponent<CityDistrictsComponent>(cityEntity);
                for (DistrictType dtype : DISTRICT_PRIO) {
                    bool has = (districts != nullptr && districts->hasDistrict(dtype));
                    if (!has) {
                        ProductionQueueItem item{};
                        item.type = ProductionItemType::District;
                        item.itemId = static_cast<uint16_t>(dtype);
                        item.name = std::string(districtTypeName(dtype));
                        item.totalCost = 60.0f * GamePace::instance().costMultiplier;
                        item.progress = 0.0f;
                        q->queue.push_back(std::move(item));
                        return Status::Success;
                    }
                }
            }
            return Status::Failure;
        });
}

/// Action: build a religious unit
static NodePtr actionBuildReligious() {
    return std::make_unique<ExecuteAction>("BuildReligious",
        [](Blackboard& bb) -> Status {
            if (bb.world == nullptr) { return Status::Failure; }
            aoc::game::GameState& gameState = *bb.world;

            const aoc::ecs::ComponentPool<CityComponent>* cityPool =
                world.getPool<CityComponent>();
            if (cityPool == nullptr) { return Status::Failure; }
            aoc::ecs::World& world = gameState.legacyWorld();

            for (uint32_t i = 0; i < cityPool->size(); ++i) {
                if (cityPool->data()[i].owner != bb.player) { continue; }
                ProductionQueueComponent* q =
                    world.tryGetComponent<ProductionQueueComponent>(cityPool->entities()[i]);
                if (q == nullptr || !q->isEmpty()) { continue; }

                // Missionary (UnitTypeId 25)
                ProductionQueueItem item{};
                item.type = ProductionItemType::Unit;
                item.itemId = 25;
                item.name = "Missionary";
                item.totalCost = 100.0f * GamePace::instance().costMultiplier;
                item.progress = 0.0f;
                q->queue.push_back(std::move(item));
                return Status::Success;
            }
            return Status::Failure;
        });
}

// ============================================================================
// Tree builder
// ============================================================================

NodePtr buildLeaderBehaviorTree(const LeaderPersonalityDef& personality) {
    const LeaderBehavior& b = personality.behavior;

    // Build the tree structure based on personality weights:
    //
    // Root (Selector - try each priority branch):
    //   1. Emergency: [IsThreatened] -> [BuildMilitary]
    //   2. Expansion: [NeedsSettler] -> [BuildSettler]
    //   3. Builder:   [NeedsBuilder] -> [BuildBuilder]
    //   4. Development (personality-weighted selector):
    //      - Military (weight: militaryAggression * prodMilitary)
    //      - Infrastructure (weight: economicFocus * prodBuildings)
    //      - Religious (weight: religiousZeal * prodReligious)
    //   5. Fallback: [BuildMilitary]

    std::vector<NodePtr> rootChildren;

    // 1. Emergency response
    {
        std::vector<NodePtr> emergencySeq;
        emergencySeq.push_back(isThreatened());
        emergencySeq.push_back(actionBuildMilitary());
        rootChildren.push_back(
            std::make_unique<Sequence>("Emergency", std::move(emergencySeq)));
    }

    // 2. Expansion
    {
        std::vector<NodePtr> expandSeq;
        expandSeq.push_back(needsSettler());
        expandSeq.push_back(actionBuildSettler());
        rootChildren.push_back(
            std::make_unique<WeightedChance>(b.prodSettlers,
                std::make_unique<Sequence>("Expand", std::move(expandSeq))));
    }

    // 3. Builder
    {
        std::vector<NodePtr> builderSeq;
        builderSeq.push_back(needsBuilder());
        builderSeq.push_back(actionBuildBuilder());
        rootChildren.push_back(
            std::make_unique<WeightedChance>(b.prodBuilders,
                std::make_unique<Sequence>("Builder", std::move(builderSeq))));
    }

    // 4. Development (personality-weighted)
    {
        std::vector<NodePtr> devChildren;

        // Military (if needs more troops)
        {
            std::vector<NodePtr> milSeq;
            milSeq.push_back(needsMilitary());
            milSeq.push_back(actionBuildMilitary());
            devChildren.push_back(
                std::make_unique<WeightedChance>(b.militaryAggression * b.prodMilitary,
                    std::make_unique<Sequence>("DevMilitary", std::move(milSeq))));
        }

        // Infrastructure / buildings
        devChildren.push_back(
            std::make_unique<WeightedChance>(b.economicFocus * b.prodBuildings,
                actionBuildInfrastructure()));

        // Religious units
        devChildren.push_back(
            std::make_unique<WeightedChance>(b.religiousZeal * b.prodReligious,
                actionBuildReligious()));

        rootChildren.push_back(
            std::make_unique<Selector>("Development", std::move(devChildren)));
    }

    // 5. Fallback
    rootChildren.push_back(actionBuildMilitary());

    return std::make_unique<Selector>("Root", std::move(rootChildren));
}

} // namespace aoc::sim::bt
