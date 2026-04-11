/**
 * @file BorderExpansion.cpp
 * @brief Culture-based border expansion implementation.
 *
 * Migrated from ECS to GameState object model.
 */

#include "aoc/simulation/city/BorderExpansion.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

#include <cmath>
#include <queue>
#include <unordered_set>

namespace aoc::sim {

float borderExpansionThreshold(int32_t tilesAlreadyClaimed) {
    float n = static_cast<float>(tilesAlreadyClaimed);
    return 10.0f + 5.0f * n + std::pow(n, 1.5f);
}

void processBorderExpansion(aoc::game::Player& player, aoc::map::HexGrid& grid) {
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        // Compute culture yield from worked tiles
        float cultureYield = 0.0f;
        for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
            if (grid.isValid(tile)) {
                int32_t index = grid.toIndex(tile);
                aoc::map::TileYield yield = grid.tileYield(index);
                cultureYield += static_cast<float>(yield.culture);
            }
        }

        // Minimum 1 culture per turn so borders always expand eventually
        if (cultureYield < 1.0f) {
            cultureYield = 1.0f;
        }

        city->addCultureBorderProgress(cultureYield);

        // Expand borders while we have enough culture
        while (city->cultureBorderProgress() >=
               borderExpansionThreshold(city->tilesClaimedCount())) {
            city->setCultureBorderProgress(
                city->cultureBorderProgress() -
                borderExpansionThreshold(city->tilesClaimedCount()));

            // BFS from city center to find nearest unowned tile adjacent to owned territory
            std::queue<aoc::hex::AxialCoord> frontier;
            std::unordered_set<aoc::hex::AxialCoord> visited;
            aoc::hex::AxialCoord bestTile = city->location();
            bool found = false;

            frontier.push(city->location());
            visited.insert(city->location());

            while (!frontier.empty() && !found) {
                aoc::hex::AxialCoord current = frontier.front();
                frontier.pop();

                std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(current);
                for (const aoc::hex::AxialCoord& nbr : nbrs) {
                    if (!grid.isValid(nbr)) {
                        continue;
                    }
                    if (visited.count(nbr) > 0) {
                        continue;
                    }
                    visited.insert(nbr);

                    int32_t nbrIndex = grid.toIndex(nbr);
                    if (grid.owner(nbrIndex) == INVALID_PLAYER) {
                        bestTile = nbr;
                        found = true;
                        break;
                    }
                    if (grid.owner(nbrIndex) == player.id()) {
                        frontier.push(nbr);
                    }
                }
            }

            if (found) {
                int32_t bestIndex = grid.toIndex(bestTile);
                grid.setOwner(bestIndex, player.id());
                city->incrementTilesClaimed();
                LOG_INFO("Border expanded for %s: claimed tile (%d,%d)",
                         city->name().c_str(), bestTile.q, bestTile.r);
            } else {
                break;
            }
        }
    }
}

void claimInitialTerritory(aoc::map::HexGrid& grid,
                            aoc::hex::AxialCoord center, PlayerId owner) {
    int32_t centerIdx = grid.toIndex(center);
    grid.setOwner(centerIdx, owner);
    std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(center);
    for (const aoc::hex::AxialCoord& nbr : nbrs) {
        if (grid.isValid(nbr)) {
            int32_t nbrIdx = grid.toIndex(nbr);
            if (grid.owner(nbrIdx) == INVALID_PLAYER) {
                grid.setOwner(nbrIdx, owner);
            }
        }
    }
}

} // namespace aoc::sim
