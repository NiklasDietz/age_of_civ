/**
 * @file Chokepoint.cpp
 * @brief Strategic chokepoint detection at map generation time.
 */

#include "aoc/simulation/map/Chokepoint.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

#include <array>
#include <cstdint>

namespace aoc::sim {

void detectChokepoints(aoc::map::HexGrid& grid) {
    int32_t totalTiles = grid.tileCount();
    int32_t landChokepoints = 0;
    int32_t mountainPasses = 0;
    int32_t waterStraits = 0;

    for (int32_t i = 0; i < totalTiles; ++i) {
        aoc::map::TerrainType terrain = grid.terrain(i);
        aoc::hex::AxialCoord pos = grid.toAxial(i);
        std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(pos);

        int32_t impassableCount = 0;
        int32_t mountainCount = 0;
        int32_t waterCount = 0;
        int32_t validNeighbors = 0;

        for (const aoc::hex::AxialCoord& nbr : nbrs) {
            if (!grid.isValid(nbr)) {
                ++impassableCount;  // Map edge counts as impassable
                continue;
            }
            ++validNeighbors;
            int32_t nbrIdx = grid.toIndex(nbr);
            aoc::map::TerrainType nbrTerrain = grid.terrain(nbrIdx);

            if (aoc::map::isImpassable(nbrTerrain)) {
                ++impassableCount;
            }
            if (nbrTerrain == aoc::map::TerrainType::Mountain) {
                ++mountainCount;
            }
            if (aoc::map::isWater(nbrTerrain)) {
                ++waterCount;
            }
        }

        // Land chokepoint: walkable land tile with 4+ impassable neighbors.
        // The tile itself must be walkable (not water, not mountain).
        if (!aoc::map::isWater(terrain) && !aoc::map::isImpassable(terrain)) {
            if (impassableCount >= 4) {
                grid.setChokepoint(i, aoc::map::ChokepointType::LandChokepoint);
                ++landChokepoints;
                continue;
            }

            // Mountain pass: walkable tile surrounded by 3+ mountains specifically.
            if (mountainCount >= 3) {
                grid.setChokepoint(i, aoc::map::ChokepointType::MountainPass);
                ++mountainPasses;
                continue;
            }
        }

        // Water strait: shallow water tile where 5+ neighbors are land/mountain
        // (very narrow water passage between landmasses). Threshold at 5 keeps
        // straits rare — only single-tile gaps between landmasses qualify.
        if (aoc::map::isShallowWater(terrain)) {
            int32_t nonWaterCount = validNeighbors - waterCount;
            if (nonWaterCount >= 5) {
                grid.setChokepoint(i, aoc::map::ChokepointType::WaterStrait);
                ++waterStraits;
                continue;
            }
        }
    }

    LOG_INFO("Chokepoint detection: %d land, %d mountain pass, %d water strait (total %d / %d tiles)",
             landChokepoints, mountainPasses, waterStraits,
             landChokepoints + mountainPasses + waterStraits, totalTiles);
}

} // namespace aoc::sim
