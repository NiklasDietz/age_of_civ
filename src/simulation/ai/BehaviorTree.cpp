/**
 * @file BehaviorTree.cpp
 * @brief Behavior tree construction, blackboard refresh, and action implementations.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/ai/BehaviorTree.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim::bt {

// ============================================================================
// Blackboard refresh
// ============================================================================

void refreshBlackboard(Blackboard& bb) {
    if (bb.gameState == nullptr) { return; }
    aoc::game::GameState& gameState = *bb.gameState;

    bb.ownedCities = 0;
    bb.totalPopulation = 0;
    bb.militaryUnits = 0;
    bb.builderUnits = 0;
    bb.settlerUnits = 0;
    bb.treasury = 0;
    bb.techsResearched = 0;

    aoc::game::Player* player = gameState.player(bb.player);
    if (player == nullptr) { return; }

    // Count cities and population
    for (const std::unique_ptr<aoc::game::City>& cityPtr : player->cities()) {
        if (cityPtr == nullptr) { continue; }
        ++bb.ownedCities;
        bb.totalPopulation += cityPtr->population();
    }

    // Count units by type
    for (const std::unique_ptr<aoc::game::Unit>& unitPtr : player->units()) {
        if (unitPtr == nullptr) { continue; }
        UnitClass cls = unitPtr->typeDef().unitClass;
        if (isMilitary(cls)) { ++bb.militaryUnits; }
        if (cls == UnitClass::Civilian && unitPtr->typeId().value == 5) { ++bb.builderUnits; }
        if (cls == UnitClass::Settler) { ++bb.settlerUnits; }
    }

    // Treasury
    bb.treasury = player->monetary().treasury;

    // Techs
    const PlayerTechComponent& tech = player->tech();
    for (std::size_t b = 0; b < tech.completedTechs.size(); ++b) {
        if (tech.completedTechs[b]) { ++bb.techsResearched; }
    }

    // Compute personality-driven targets
    if (bb.behavior != nullptr) {
        AIScaledTargets targets = computeScaledTargets(*bb.behavior);
        bb.targetMaxCities = targets.maxCities;
        bb.desiredMilitary = bb.ownedCities * targets.desiredMilitaryPerCity + 2;
    }

    // Threat assessment: enemy military near our cities
    bb.isThreatened = bb.militaryUnits < 3 && bb.ownedCities > 0;

    // Mirror the player's AIBlackboard flag: advisor-managed expansion
    // exhaustion prevents the BT from queuing settlers that cannot be placed.
    bb.expansionExhausted = player->blackboard().expansionExhausted;

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
            if (bb.gameState == nullptr) { return Status::Failure; }
            aoc::game::GameState& gameState = *bb.gameState;
            aoc::game::Player* player = gameState.player(bb.player);
            if (player == nullptr) { return Status::Failure; }

            // Find best city to produce settler (highest population with empty queue)
            aoc::game::City* bestCity = nullptr;
            int32_t bestPop = 0;
            for (const std::unique_ptr<aoc::game::City>& cityPtr : player->cities()) {
                if (cityPtr == nullptr) { continue; }
                if (!cityPtr->production().isEmpty()) { continue; }
                if (cityPtr->population() > bestPop) {
                    bestPop = cityPtr->population();
                    bestCity = cityPtr.get();
                }
            }
            if (bestCity == nullptr) { return Status::Failure; }

            ProductionQueueItem item{};
            item.type = ProductionItemType::Unit;
            item.itemId = 3;  // Settler
            item.name = "Settler";
            item.totalCost = static_cast<float>(unitTypeDef(UnitTypeId{3}).productionCost)
                           * GamePace::instance().costMultiplier;
            item.progress = 0.0f;
            bestCity->production().queue.push_back(std::move(item));
            return Status::Success;
        });
}

/// Action: enqueue a military unit in a city with empty queue
static NodePtr actionBuildMilitary() {
    return std::make_unique<ExecuteAction>("BuildMilitary",
        [](Blackboard& bb) -> Status {
            if (bb.gameState == nullptr) { return Status::Failure; }
            aoc::game::GameState& gameState = *bb.gameState;
            aoc::game::Player* player = gameState.player(bb.player);
            if (player == nullptr) { return Status::Failure; }

            // Find UnitTypeId 0 (Warrior) as default military unit
            UnitTypeId militaryId{0};

            for (const std::unique_ptr<aoc::game::City>& cityPtr : player->cities()) {
                if (cityPtr == nullptr) { continue; }
                if (!cityPtr->production().isEmpty()) { continue; }

                ProductionQueueItem item{};
                item.type = ProductionItemType::Unit;
                item.itemId = militaryId.value;
                item.name = std::string(unitTypeDef(militaryId).name);
                item.totalCost = static_cast<float>(unitTypeDef(militaryId).productionCost)
                               * GamePace::instance().costMultiplier;
                item.progress = 0.0f;
                cityPtr->production().queue.push_back(std::move(item));
                return Status::Success;
            }
            return Status::Failure;
        });
}

/// Action: enqueue a builder
static NodePtr actionBuildBuilder() {
    return std::make_unique<ExecuteAction>("BuildBuilder",
        [](Blackboard& bb) -> Status {
            if (bb.gameState == nullptr) { return Status::Failure; }
            aoc::game::GameState& gameState = *bb.gameState;
            aoc::game::Player* player = gameState.player(bb.player);
            if (player == nullptr) { return Status::Failure; }

            for (const std::unique_ptr<aoc::game::City>& cityPtr : player->cities()) {
                if (cityPtr == nullptr) { continue; }
                if (!cityPtr->production().isEmpty()) { continue; }

                ProductionQueueItem item{};
                item.type = ProductionItemType::Unit;
                item.itemId = 5;  // Builder
                item.name = "Builder";
                item.totalCost = static_cast<float>(unitTypeDef(UnitTypeId{5}).productionCost)
                               * GamePace::instance().costMultiplier;
                item.progress = 0.0f;
                cityPtr->production().queue.push_back(std::move(item));
                return Status::Success;
            }
            return Status::Failure;
        });
}

/// Action: build a district or building in a city with empty queue
static NodePtr actionBuildInfrastructure() {
    return std::make_unique<ExecuteAction>("BuildInfrastructure",
        [](Blackboard& bb) -> Status {
            if (bb.gameState == nullptr) { return Status::Failure; }
            aoc::game::GameState& gameState = *bb.gameState;
            aoc::game::Player* player = gameState.player(bb.player);
            if (player == nullptr) { return Status::Failure; }

            for (const std::unique_ptr<aoc::game::City>& cityPtr : player->cities()) {
                if (cityPtr == nullptr) { continue; }
                if (!cityPtr->production().isEmpty()) { continue; }

                // Try buildings first
                static constexpr uint16_t BUILDING_PRIO[] = {
                    16, 15, 1, 7, 6, 24, 0, 20, 19, 3, 26, 4, 12
                };

                // canBuildBuilding still requires an EntityId from the legacy world.
                // Use city location as a proxy: skip building check here and fall through
                // to districts (which are fully migrated).
                for (uint16_t bidx : BUILDING_PRIO) {
                    if (bidx >= BUILDING_DEFS.size()) { continue; }
                    const BuildingDef& bdef = BUILDING_DEFS[bidx];
                    if (cityPtr->hasBuilding(bdef.id)) { continue; }

                    ProductionQueueItem item{};
                    item.type = ProductionItemType::Building;
                    item.itemId = bdef.id.value;
                    item.name = std::string(bdef.name);
                    item.totalCost = static_cast<float>(bdef.productionCost)
                                   * GamePace::instance().costMultiplier;
                    item.progress = 0.0f;
                    cityPtr->production().queue.push_back(std::move(item));
                    return Status::Success;
                }

                // Try districts
                static constexpr DistrictType DISTRICT_PRIO[] = {
                    DistrictType::Commercial, DistrictType::Campus,
                    DistrictType::Industrial, DistrictType::Encampment,
                };
                for (DistrictType dtype : DISTRICT_PRIO) {
                    if (!cityPtr->hasDistrict(dtype)) {
                        ProductionQueueItem item{};
                        item.type = ProductionItemType::District;
                        item.itemId = static_cast<uint16_t>(dtype);
                        item.name = std::string(districtTypeName(dtype));
                        item.totalCost = 60.0f * GamePace::instance().costMultiplier;
                        item.progress = 0.0f;
                        cityPtr->production().queue.push_back(std::move(item));
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
            if (bb.gameState == nullptr) { return Status::Failure; }
            aoc::game::GameState& gameState = *bb.gameState;
            aoc::game::Player* player = gameState.player(bb.player);
            if (player == nullptr) { return Status::Failure; }

            for (const std::unique_ptr<aoc::game::City>& cityPtr : player->cities()) {
                if (cityPtr == nullptr) { continue; }
                if (!cityPtr->production().isEmpty()) { continue; }

                // Missionary (UnitTypeId 25)
                ProductionQueueItem item{};
                item.type = ProductionItemType::Unit;
                item.itemId = 25;
                item.name = "Missionary";
                item.totalCost = 100.0f * GamePace::instance().costMultiplier;
                item.progress = 0.0f;
                cityPtr->production().queue.push_back(std::move(item));
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
