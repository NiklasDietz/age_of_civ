/**
 * @file Automation.cpp
 * @brief Player automation: research queue, auto-explore, alert, auto-improve.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/automation/Automation.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <limits>

namespace aoc::sim {

void processResearchQueue(aoc::game::GameState& gameState, PlayerId player) {
    aoc::ecs::World& world = gameState.legacyWorld();
    aoc::ecs::ComponentPool<PlayerTechComponent>* techPool =
        world.getPool<PlayerTechComponent>();
    aoc::ecs::ComponentPool<PlayerResearchQueueComponent>* queuePool =
        world.getPool<PlayerResearchQueueComponent>();

    if (techPool == nullptr || queuePool == nullptr) {
        return;
    }

    PlayerTechComponent* tech = nullptr;
    PlayerResearchQueueComponent* researchQueue = nullptr;

    for (uint32_t i = 0; i < techPool->size(); ++i) {
        if (techPool->data()[i].owner == player) {
            tech = &techPool->data()[i];
            break;
        }
    }
    for (uint32_t i = 0; i < queuePool->size(); ++i) {
        if (queuePool->data()[i].owner == player) {
            researchQueue = &queuePool->data()[i];
            break;
        }
    }

    if (tech == nullptr || researchQueue == nullptr) {
        return;
    }

    // If no current research and queue has entries, start next
    if (!tech->currentResearch.isValid() && !researchQueue->researchQueue.empty()) {
        TechId nextTech = researchQueue->popNext();
        // Skip already-researched techs
        while (nextTech.isValid() && tech->hasResearched(nextTech)) {
            nextTech = researchQueue->popNext();
        }
        if (nextTech.isValid()) {
            tech->currentResearch = nextTech;
            tech->researchProgress = 0.0f;
            LOG_INFO("Research queue: player %u auto-started %.*s",
                     static_cast<unsigned>(player),
                     static_cast<int>(techDef(nextTech).name.size()),
                     techDef(nextTech).name.data());
        }
    }
}

void processAutoExplore(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, PlayerId player) {
    aoc::ecs::World& world = gameState.legacyWorld();
    aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    aoc::ecs::ComponentPool<UnitAutomationComponent>* autoPool =
        world.getPool<UnitAutomationComponent>();

    if (unitPool == nullptr || autoPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < autoPool->size(); ++i) {
        UnitAutomationComponent& automation = autoPool->data()[i];
        if (!automation.autoExplore) { continue; }

        EntityId unitEntity = autoPool->entities()[i];
        UnitComponent* unit = world.tryGetComponent<UnitComponent>(unitEntity);
        if (unit == nullptr || unit->owner != player) { continue; }

        const UnitTypeDef& def = unitTypeDef(unit->typeId);
        if (def.unitClass != UnitClass::Scout) { continue; }

        // Find nearest unexplored tile (simple: check ring-1 through ring-5)
        aoc::hex::AxialCoord bestTarget = unit->position;
        int32_t bestDist = std::numeric_limits<int32_t>::max();

        for (int32_t ring = 1; ring <= 5; ++ring) {
            std::vector<aoc::hex::AxialCoord> tiles;
            tiles.reserve(static_cast<std::size_t>(ring) * 6);
            aoc::hex::ring(unit->position, ring, std::back_inserter(tiles));

            for (const aoc::hex::AxialCoord& tile : tiles) {
                if (!grid.isValid(tile)) { continue; }
                int32_t idx = grid.toIndex(tile);
                // Check if tile is unowned (proxy for unexplored)
                if (grid.owner(idx) != INVALID_PLAYER) { continue; }
                if (grid.movementCost(idx) <= 0) { continue; }

                int32_t dist = aoc::hex::distance(unit->position, tile);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestTarget = tile;
                }
            }
            if (bestDist < std::numeric_limits<int32_t>::max()) { break; }
        }

        if (bestTarget != unit->position) {
            orderUnitMove(world, unitEntity, bestTarget, grid);
            moveUnitAlongPath(world, unitEntity, grid);
        }
    }
}

void processAlertStance(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid, PlayerId player) {
    aoc::ecs::World& world = gameState.legacyWorld();
    aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    aoc::ecs::ComponentPool<UnitAutomationComponent>* autoPool =
        world.getPool<UnitAutomationComponent>();

    if (unitPool == nullptr || autoPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < autoPool->size(); ++i) {
        UnitAutomationComponent& automation = autoPool->data()[i];
        if (!automation.alertStance) { continue; }

        EntityId unitEntity = autoPool->entities()[i];
        UnitComponent* unit = world.tryGetComponent<UnitComponent>(unitEntity);
        if (unit == nullptr || unit->owner != player) { continue; }

        // Only wake sleeping/fortified units
        if (unit->state != UnitState::Sleeping && unit->state != UnitState::Fortified) {
            continue;
        }

        // Scan for enemy units within alert radius
        bool enemyNearby = false;
        for (uint32_t u = 0; u < unitPool->size(); ++u) {
            const UnitComponent& other = unitPool->data()[u];
            if (other.owner == player || other.owner == INVALID_PLAYER) { continue; }

            int32_t dist = aoc::hex::distance(unit->position, other.position);
            if (dist <= automation.alertRadius) {
                enemyNearby = true;
                break;
            }
        }

        if (enemyNearby) {
            unit->state = UnitState::Idle;  // Wake up
            LOG_INFO("Alert: player %u unit at (%d,%d) woke up - enemy nearby!",
                     static_cast<unsigned>(player),
                     unit->position.q, unit->position.r);
        }
    }

    (void)grid;
}

void processAutomation(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, PlayerId player) {
    aoc::ecs::World& world = gameState.legacyWorld();
    processResearchQueue(world, player);
    processAutoExplore(world, grid, player);
    processAlertStance(world, grid, player);
}

} // namespace aoc::sim
