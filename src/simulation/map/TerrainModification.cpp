/**
 * @file TerrainModification.cpp
 * @brief Terrain modification: canals, tunnels, land reclamation, deforestation.
 */

#include "aoc/simulation/map/TerrainModification.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

bool canBuildTerrainProject(const aoc::map::HexGrid& grid,
                            int32_t tileIndex,
                            TerrainProjectType type) {
    aoc::map::TerrainType terrain = grid.terrain(tileIndex);
    aoc::map::FeatureType feature = grid.feature(tileIndex);

    switch (type) {
        case TerrainProjectType::Canal: {
            // Must be land tile (not water, mountain, or existing canal)
            if (aoc::map::isWater(terrain) || aoc::map::isImpassable(terrain)) {
                return false;
            }
            if (grid.improvement(tileIndex) == aoc::map::ImprovementType::Canal) {
                return false;  // Already a canal
            }
            // Need at least 2 adjacent water or canal tiles (supports multi-tile canals)
            hex::AxialCoord center = grid.toAxial(tileIndex);
            std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(center);
            int32_t navigableNeighbors = 0;
            for (const hex::AxialCoord& n : nbrs) {
                if (!grid.isValid(n)) { continue; }
                int32_t nbrIdx = grid.toIndex(n);
                if (aoc::map::isWater(grid.terrain(nbrIdx))
                    || grid.improvement(nbrIdx) == aoc::map::ImprovementType::Canal) {
                    ++navigableNeighbors;
                }
            }
            return navigableNeighbors >= 2;
        }

        case TerrainProjectType::Tunnel: {
            // Must be a mountain tile
            return terrain == aoc::map::TerrainType::Mountain;
        }

        case TerrainProjectType::LandReclamation: {
            // Must be coast tile adjacent to land
            if (terrain != aoc::map::TerrainType::Coast) { return false; }
            hex::AxialCoord center = grid.toAxial(tileIndex);
            std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(center);
            for (const hex::AxialCoord& n : nbrs) {
                if (grid.isValid(n) && !aoc::map::isWater(grid.terrain(grid.toIndex(n)))) {
                    return true;
                }
            }
            return false;
        }

        case TerrainProjectType::Deforestation:
            return feature == aoc::map::FeatureType::Forest
                || feature == aoc::map::FeatureType::Jungle;

        case TerrainProjectType::Reforestation:
            return feature == aoc::map::FeatureType::None
                && !aoc::map::isWater(terrain)
                && !aoc::map::isImpassable(terrain)
                && terrain != aoc::map::TerrainType::Desert
                && terrain != aoc::map::TerrainType::Snow;

        default:
            return false;
    }
}

ErrorCode executeTerrainProject(aoc::map::HexGrid& grid,
                                int32_t tileIndex,
                                TerrainProjectType type) {
    if (!canBuildTerrainProject(grid, tileIndex, type)) {
        return ErrorCode::InvalidArgument;
    }

    switch (type) {
        case TerrainProjectType::Canal:
            // Place canal improvement: land stays land but becomes navigable by ships.
            // Clear existing features (excavation removes vegetation) but keep terrain.
            grid.setFeature(tileIndex, aoc::map::FeatureType::None);
            grid.setImprovement(tileIndex, aoc::map::ImprovementType::Canal);
            LOG_INFO("Canal built at tile %d -- navigable waterway created", tileIndex);
            break;

        case TerrainProjectType::Tunnel:
            // Mountain becomes passable (movement cost 2 instead of impassable)
            // Represented by changing terrain to Plains with Hills feature.
            grid.setTerrain(tileIndex, aoc::map::TerrainType::Plains);
            grid.setFeature(tileIndex, aoc::map::FeatureType::Hills);
            LOG_INFO("Tunnel built through mountain at tile %d", tileIndex);
            break;

        case TerrainProjectType::LandReclamation:
            grid.setTerrain(tileIndex, aoc::map::TerrainType::Plains);
            grid.setFeature(tileIndex, aoc::map::FeatureType::None);
            LOG_INFO("Land reclaimed from sea at tile %d", tileIndex);
            break;

        case TerrainProjectType::Deforestation:
            grid.setFeature(tileIndex, aoc::map::FeatureType::None);
            LOG_INFO("Forest cleared at tile %d (+20 production)", tileIndex);
            break;

        case TerrainProjectType::Reforestation:
            grid.setFeature(tileIndex, aoc::map::FeatureType::Forest);
            LOG_INFO("Forest planted at tile %d", tileIndex);
            break;

        default:
            return ErrorCode::InvalidArgument;
    }

    return ErrorCode::Ok;
}

} // namespace aoc::sim
