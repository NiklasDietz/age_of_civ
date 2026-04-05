/**
 * @file BorderExpansion.cpp
 * @brief Culture-based border expansion implementation.
 */

#include "aoc/simulation/city/BorderExpansion.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <cmath>
#include <queue>
#include <unordered_set>
#include <vector>

namespace aoc::sim {

float borderExpansionThreshold(int32_t tilesAlreadyClaimed) {
    float n = static_cast<float>(tilesAlreadyClaimed);
    return 10.0f + 5.0f * n + std::pow(n, 1.5f);
}

void processBorderExpansion(aoc::ecs::World& world,
                             aoc::map::HexGrid& grid,
                             PlayerId player) {
    aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        CityComponent& city = cityPool->data()[i];
        if (city.owner != player) {
            continue;
        }

        // Compute culture yield from worked tiles
        float cultureYield = 0.0f;
        for (const hex::AxialCoord& tile : city.workedTiles) {
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

        city.cultureBorderProgress += cultureYield;

        // Expand borders while we have enough culture
        while (city.cultureBorderProgress >=
               borderExpansionThreshold(city.tilesClaimedCount)) {
            city.cultureBorderProgress -=
                borderExpansionThreshold(city.tilesClaimedCount);

            // BFS from city center to find nearest unowned tile adjacent to owned territory
            std::queue<hex::AxialCoord> frontier;
            std::unordered_set<hex::AxialCoord> visited;
            hex::AxialCoord bestTile = city.location;
            bool found = false;

            frontier.push(city.location);
            visited.insert(city.location);

            while (!frontier.empty() && !found) {
                hex::AxialCoord current = frontier.front();
                frontier.pop();

                std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(current);
                for (const hex::AxialCoord& nbr : nbrs) {
                    if (!grid.isValid(nbr)) {
                        continue;
                    }
                    if (visited.count(nbr) > 0) {
                        continue;
                    }
                    visited.insert(nbr);

                    int32_t nbrIndex = grid.toIndex(nbr);
                    if (grid.owner(nbrIndex) == INVALID_PLAYER) {
                        // Found an unowned tile adjacent to owned territory
                        bestTile = nbr;
                        found = true;
                        break;
                    }
                    if (grid.owner(nbrIndex) == player) {
                        // Owned tile -- continue searching through it
                        frontier.push(nbr);
                    }
                }
            }

            if (found) {
                int32_t bestIndex = grid.toIndex(bestTile);
                grid.setOwner(bestIndex, player);
                ++city.tilesClaimedCount;
                LOG_INFO("Border expanded for %s: claimed tile (%d,%d)",
                         city.name.c_str(), bestTile.q, bestTile.r);
            } else {
                // No more tiles to claim
                break;
            }
        }
    }
}

void claimInitialTerritory(aoc::map::HexGrid& grid,
                            hex::AxialCoord cityLocation,
                            PlayerId owner) {
    // Claim the city center tile
    if (grid.isValid(cityLocation)) {
        grid.setOwner(grid.toIndex(cityLocation), owner);
    }

    // Claim all 6 neighbors
    std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(cityLocation);
    for (const hex::AxialCoord& nbr : nbrs) {
        if (grid.isValid(nbr)) {
            grid.setOwner(grid.toIndex(nbr), owner);
        }
    }
}

} // namespace aoc::sim
