/**
 * @file Automation.cpp
 * @brief Player automation: research queue, auto-explore, alert, auto-improve.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/automation/Automation.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Log.hpp"

#include <limits>

namespace aoc::sim {

void processResearchQueue(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return; }

    PlayerTechComponent& tech = gsPlayer->tech();
    PlayerResearchQueueComponent& researchQueue = gsPlayer->researchQueue();

    // If no current research and queue has entries, start next
    if (!tech.currentResearch.isValid() && !researchQueue.researchQueue.empty()) {
        TechId nextTech = researchQueue.popNext();
        // Skip already-researched techs
        while (nextTech.isValid() && tech.hasResearched(nextTech)) {
            nextTech = researchQueue.popNext();
        }
        if (nextTech.isValid()) {
            tech.currentResearch = nextTech;
            tech.researchProgress = 0.0f;
            LOG_INFO("Research queue: player %u auto-started %.*s",
                     static_cast<unsigned>(player),
                     static_cast<int>(techDef(nextTech).name.size()),
                     techDef(nextTech).name.data());
        }
    }
}

void processAutoExplore(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return; }

    for (const std::unique_ptr<aoc::game::Unit>& unit : gsPlayer->units()) {
        if (!unit->autoExplore) { continue; }

        const UnitTypeDef& def = unitTypeDef(unit->typeId());
        if (def.unitClass != UnitClass::Scout) { continue; }

        // Find nearest unexplored tile (simple: check ring-1 through ring-5)
        aoc::hex::AxialCoord bestTarget = unit->position();
        int32_t bestDist = std::numeric_limits<int32_t>::max();

        for (int32_t ring = 1; ring <= 5; ++ring) {
            std::vector<aoc::hex::AxialCoord> tiles;
            tiles.reserve(static_cast<std::size_t>(ring) * 6);
            aoc::hex::ring(unit->position(), ring, std::back_inserter(tiles));

            for (const aoc::hex::AxialCoord& tile : tiles) {
                if (!grid.isValid(tile)) { continue; }
                const int32_t idx = grid.toIndex(tile);
                // Check if tile is unowned (proxy for unexplored)
                if (grid.owner(idx) != INVALID_PLAYER) { continue; }
                if (grid.movementCost(idx) <= 0) { continue; }

                const int32_t dist = grid.distance(unit->position(), tile);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestTarget = tile;
                }
            }
            if (bestDist < std::numeric_limits<int32_t>::max()) { break; }
        }

        if (bestTarget != unit->position()) {
            orderUnitMove(*unit, bestTarget, grid);
            moveUnitAlongPath(gameState, *unit, grid);
        }
    }
}

void processAlertStance(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid, PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return; }

    for (const std::unique_ptr<aoc::game::Unit>& unit : gsPlayer->units()) {
        if (!unit->alertStance) { continue; }

        // Only wake sleeping/fortified units
        if (unit->state() != UnitState::Sleeping && unit->state() != UnitState::Fortified) {
            continue;
        }

        // Scan for enemy units within alert radius
        bool enemyNearby = false;
        for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
            if (other->id() == player) { continue; }
            for (const std::unique_ptr<aoc::game::Unit>& enemyUnit : other->units()) {
                const int32_t dist = grid.distance(unit->position(), enemyUnit->position());
                if (dist <= unit->alertRadius) {
                    enemyNearby = true;
                    break;
                }
            }
            if (enemyNearby) { break; }
        }

        if (enemyNearby) {
            unit->setState(UnitState::Idle);
            LOG_INFO("Alert: player %u unit at (%d,%d) woke up - enemy nearby!",
                     static_cast<unsigned>(player),
                     unit->position().q, unit->position().r);
        }
    }
}

void processAutomation(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, PlayerId player) {
    processResearchQueue(gameState, player);
    processAutoExplore(gameState, grid, player);
    processAlertStance(gameState, grid, player);
}

} // namespace aoc::sim
