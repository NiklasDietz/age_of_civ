/**
 * @file SupplyLines.cpp
 * @brief Military supply line computation and attrition.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/unit/SupplyLines.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace aoc::sim {

void computeSupplyLines(aoc::game::GameState& gameState,
                        const aoc::map::HexGrid& grid,
                        PlayerId player) {
    // Collect all supply sources (cities and forts owned by this player)
    std::vector<int32_t> supplySources;

    const aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer != nullptr) {
        for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
            const int32_t idx = grid.toIndex(city->location());
            supplySources.push_back(idx);
        }
    }

    // Also include forts in owned territory
    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        if (grid.owner(i) == player
            && grid.improvement(i) == aoc::map::ImprovementType::Fort) {
            supplySources.push_back(i);
        }
    }

    if (supplySources.empty()) {
        // No supply sources: all units unsupplied
        if (gsPlayer != nullptr) {
            for (const std::unique_ptr<aoc::game::Unit>& unit : gsPlayer->units()) {
                unit->supply().isSupplied = false;
            }
        }
        return;
    }

    // BFS from all supply sources, following roads/railways, to compute
    // supply distance to every reachable tile
    std::unordered_map<int32_t, int32_t> supplyDistance;
    std::queue<int32_t> frontier;

    for (int32_t src : supplySources) {
        supplyDistance[src] = 0;
        frontier.push(src);
    }

    while (!frontier.empty()) {
        const int32_t current = frontier.front();
        frontier.pop();
        const int32_t currentDist = supplyDistance[current];

        // Max range depends on infrastructure at current tile
        const int32_t maxRange = supplyRange(grid.infrastructureTier(current));
        if (currentDist >= maxRange) {
            continue;
        }

        const hex::AxialCoord currentAxial = grid.toAxial(current);
        const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(currentAxial);
        for (const hex::AxialCoord& nbr : nbrs) {
            if (!grid.isValid(nbr)) { continue; }
            const int32_t nbrIdx = grid.toIndex(nbr);

            // Can only supply through passable land
            if (aoc::map::isWater(grid.terrain(nbrIdx))
                || aoc::map::isImpassable(grid.terrain(nbrIdx))) {
                continue;
            }

            const int32_t newDist = currentDist + 1;
            std::unordered_map<int32_t, int32_t>::iterator it = supplyDistance.find(nbrIdx);
            if (it == supplyDistance.end() || newDist < it->second) {
                supplyDistance[nbrIdx] = newDist;
                frontier.push(nbrIdx);
            }
        }
    }

    // Update each military unit's supply status
    if (gsPlayer == nullptr) { return; }
    for (const std::unique_ptr<aoc::game::Unit>& unit : gsPlayer->units()) {
        if (!isMilitary(unit->typeDef().unitClass)) { continue; }

        const int32_t unitTileIdx = grid.toIndex(unit->position());
        UnitSupplyComponent& supply = unit->supply();

        std::unordered_map<int32_t, int32_t>::iterator it = supplyDistance.find(unitTileIdx);
        if (it != supplyDistance.end()) {
            supply.isSupplied = true;
            supply.distanceFromSupply = it->second;
            supply.maxSupplyRange = supplyRange(grid.infrastructureTier(unitTileIdx));
        } else {
            supply.isSupplied = false;
            supply.distanceFromSupply = 999;
        }
    }
}

void applySupplyAttrition(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return; }

    // Collect units that are unsupplied (iterate copy list to allow removal)
    std::vector<aoc::game::Unit*> toKill;
    for (const std::unique_ptr<aoc::game::Unit>& unit : gsPlayer->units()) {
        const UnitSupplyComponent& supply = unit->supply();
        if (supply.isSupplied) { continue; }

        unit->setHitPoints(unit->hitPoints() - UNSUPPLIED_ATTRITION_HP);
        if (unit->isDead()) {
            toKill.push_back(unit.get());
        }
    }

    for (aoc::game::Unit* dead : toKill) {
        gsPlayer->removeUnit(dead);
        LOG_INFO("Unit destroyed by supply attrition");
    }
}

} // namespace aoc::sim
