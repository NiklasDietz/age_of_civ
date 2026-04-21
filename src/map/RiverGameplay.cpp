/**
 * @file RiverGameplay.cpp
 * @brief River and elevation gameplay effects implementation.
 */

#include "aoc/map/RiverGameplay.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_set>

namespace aoc::map {

// ============================================================================
// River crossing detection
// ============================================================================

bool crossesRiver(const HexGrid& grid, int32_t fromIndex, int32_t toIndex) {
    hex::AxialCoord fromAxial = grid.toAxial(fromIndex);
    hex::AxialCoord toAxial = grid.toAxial(toIndex);

    // Find which hex direction goes from -> to
    std::array<hex::AxialCoord, 6> neighbors = hex::neighbors(fromAxial);
    for (int dir = 0; dir < 6; ++dir) {
        if (neighbors[static_cast<std::size_t>(dir)] == toAxial) {
            // Check if fromTile has a river on this edge
            return grid.hasRiverOnEdge(fromIndex, dir);
        }
    }
    return false;
}

// ============================================================================
// Elevation combat
// ============================================================================

float elevationCombatModifier(const HexGrid& grid,
                              int32_t attackerTile,
                              int32_t defenderTile) {
    int8_t attackerElev = grid.elevation(attackerTile);
    int8_t defenderElev = grid.elevation(defenderTile);
    int32_t diff = static_cast<int32_t>(attackerElev) - static_cast<int32_t>(defenderElev);

    // +10% per elevation level advantage, -10% per level disadvantage
    return 1.0f + static_cast<float>(diff) * 0.10f;
}

// ============================================================================
// River yield bonuses
// ============================================================================

int32_t riverFoodBonus(const HexGrid& grid, int32_t tileIndex) {
    if (grid.riverEdges(tileIndex) != 0) {
        return 1;  // +1 food for any river adjacency
    }
    return 0;
}

int32_t riverHousingBonus(const HexGrid& grid, int32_t tileIndex) {
    if (grid.riverEdges(tileIndex) != 0) {
        return 3;  // +3 housing for river city
    }
    return 0;
}

// ============================================================================
// Flooding
// ============================================================================

void processFlooding(aoc::game::GameState& /*gameState*/, HexGrid& grid, int32_t turnNumber) {
    // Seasonal pattern: flooding more likely every 4 turns (simulating seasons)
    bool isFloodSeason = (turnNumber % 4) == 0;
    if (!isFloodSeason) {
        return;
    }

    int32_t totalTiles = grid.tileCount();
    for (int32_t i = 0; i < totalTiles; ++i) {
        if (grid.feature(i) != FeatureType::Floodplains) {
            continue;
        }
        if (grid.riverEdges(i) == 0) {
            continue;  // Floodplain not on a river
        }

        // Check if a Dam improvement blocks flooding
        // Dam would be an improvement on any upstream tile on this river
        // For simplicity: if this tile has a Dam improvement, no flooding
        if (grid.improvement(i) == ImprovementType::Fort) {
            // Fort acts as levee/dam protection for now
            continue;
        }

        // Deterministic flood check: hash of turn + tile index
        uint32_t hash = static_cast<uint32_t>(i) * 2654435761u
                      + static_cast<uint32_t>(turnNumber) * 2246822519u;
        // 30% chance of beneficial flood during flood season.
        // Previous code used `hash > FLOOD_THRESHOLD` with a threshold at 70%
        // of UINT32_MAX, which actually delivered ~70% floods — the inverse
        // of the intent. Flood when hash falls in the bottom 30% of range.
        constexpr uint32_t FLOOD_THRESHOLD = 1288490188u;  // ~30% of UINT32_MAX
        if (hash >= FLOOD_THRESHOLD) {
            continue;
        }

        // Beneficial flood: food bonus is already in floodplains yield.
        // But 5% chance of crop destruction (destroying the improvement)
        constexpr uint32_t DESTRUCTION_THRESHOLD = 214748364u;  // ~5% of UINT32_MAX
        uint32_t destructionHash = hash * 104729u;
        if (destructionHash < DESTRUCTION_THRESHOLD) {
            ImprovementType currentImp = grid.improvement(i);
            if (currentImp != ImprovementType::None && currentImp != ImprovementType::Road) {
                grid.setImprovement(i, ImprovementType::None);
                LOG_INFO("Flood destroyed improvement at tile %d", i);
            }
        }
    }
}

// ============================================================================
// River corridor for trade
// ============================================================================

bool hasRiverCorridor(const HexGrid& grid, int32_t fromIndex, int32_t toIndex) {
    // BFS along river edges from source to destination.
    // A tile is connected if it has river edges and a neighbor also has river edges
    // on the shared boundary.

    if (grid.riverEdges(fromIndex) == 0 || grid.riverEdges(toIndex) == 0) {
        return false;  // Both endpoints must be on a river
    }

    std::unordered_set<int32_t> visited;
    std::queue<int32_t> frontier;
    frontier.push(fromIndex);
    visited.insert(fromIndex);

    // Limit search depth to prevent infinite loops on large maps
    constexpr int32_t MAX_SEARCH_DEPTH = 50;
    int32_t depth = 0;

    while (!frontier.empty() && depth < MAX_SEARCH_DEPTH) {
        int32_t levelSize = static_cast<int32_t>(frontier.size());
        for (int32_t l = 0; l < levelSize; ++l) {
            int32_t current = frontier.front();
            frontier.pop();

            if (current == toIndex) {
                return true;
            }

            // Check all 6 neighbors for shared river edges
            hex::AxialCoord currentAxial = grid.toAxial(current);
            std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(currentAxial);
            for (int dir = 0; dir < 6; ++dir) {
                if (!grid.hasRiverOnEdge(current, dir)) {
                    continue;  // No river on this edge
                }
                hex::AxialCoord nbrAxial = nbrs[static_cast<std::size_t>(dir)];
                if (!grid.isValid(nbrAxial)) {
                    continue;
                }
                int32_t nbrIndex = grid.toIndex(nbrAxial);
                if (visited.count(nbrIndex) > 0) {
                    continue;
                }
                // Neighbor must also have river edges to be part of the corridor
                if (grid.riverEdges(nbrIndex) != 0) {
                    visited.insert(nbrIndex);
                    frontier.push(nbrIndex);
                }
            }
        }
        ++depth;
    }

    return false;
}

float transportCostModifier(const HexGrid& grid, int32_t fromIndex, int32_t toIndex) {
    bool hasRoad = grid.hasRoad(fromIndex) && grid.hasRoad(toIndex);
    bool hasRiver = hasRiverCorridor(grid, fromIndex, toIndex);

    if (hasRoad && hasRiver) {
        return 0.25f;  // Both: 75% reduction
    }
    if (hasRoad || hasRiver) {
        return 0.50f;  // Either: 50% reduction
    }
    return 1.0f;  // Neither: full cost
}

// ============================================================================
// Watershed pollution
// ============================================================================

int32_t watershedPollutionPenalty(const aoc::game::GameState& gameState,
                                  const HexGrid& grid,
                                  int32_t cityTileIndex) {
    if (grid.riverEdges(cityTileIndex) == 0) {
        return 0;  // Not on a river, no watershed effect
    }

    // Find all cities upstream on the same river system.
    // "Upstream" = higher or equal elevation tiles connected by river.
    int8_t cityElev = grid.elevation(cityTileIndex);

    int32_t totalUpstreamPollution = 0;

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            int32_t otherIndex = grid.toIndex(cityPtr->location());
            if (otherIndex == cityTileIndex) {
                continue;
            }

            // Must be on a river and at higher or equal elevation (upstream)
            if (grid.riverEdges(otherIndex) == 0) {
                continue;
            }
            if (grid.elevation(otherIndex) < cityElev) {
                continue;  // Lower elevation = downstream, not upstream
            }

            // Check if connected by river corridor
            if (!hasRiverCorridor(grid, otherIndex, cityTileIndex)) {
                continue;
            }

            // Check pollution level of upstream city
            const aoc::sim::CityPollutionComponent& pollution = cityPtr->pollution();
            if (pollution.wasteAccumulated > 10) {
                totalUpstreamPollution += pollution.wasteAccumulated;
            }
        }
    }

    // Convert to penalty: 0-30 waste = 0, 30-60 = 1, 60-100 = 2, 100+ = 3
    if (totalUpstreamPollution < 30) { return 0; }
    if (totalUpstreamPollution < 60) { return 1; }
    if (totalUpstreamPollution < 100) { return 2; }
    return 3;
}

} // namespace aoc::map
