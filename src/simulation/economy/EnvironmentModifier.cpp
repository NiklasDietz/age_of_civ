/**
 * @file EnvironmentModifier.cpp
 * @brief Terrain and feature effects on building/production efficiency.
 */

#include "aoc/simulation/economy/EnvironmentModifier.hpp"
#include "aoc/map/HexCoord.hpp"

#include <algorithm>

namespace aoc::sim {

float computeEnvironmentModifier(const aoc::map::HexGrid& grid,
                                  aoc::hex::AxialCoord location,
                                  BuildingId buildingId) {
    if (!grid.isValid(location)) {
        return 1.0f;
    }

    const int32_t cityIndex = grid.toIndex(location);
    const aoc::map::TerrainType cityTerrain = grid.terrain(cityIndex);
    const aoc::map::FeatureType cityFeature = grid.feature(cityIndex);
    const bool hasRiver = (grid.riverEdges(cityIndex) != 0);

    // Check 6 neighbors for adjacency conditions
    bool nearMountain = false;
    bool nearCoast    = false;
    bool nearForest   = false;

    const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(location);
    for (const hex::AxialCoord& n : nbrs) {
        if (!grid.isValid(n)) {
            continue;
        }
        const int32_t nIndex = grid.toIndex(n);
        const aoc::map::TerrainType nTerrain = grid.terrain(nIndex);
        const aoc::map::FeatureType nFeature = grid.feature(nIndex);

        if (nTerrain == aoc::map::TerrainType::Mountain) {
            nearMountain = true;
        }
        if (aoc::map::isWater(nTerrain)) {
            nearCoast = true;
        }
        if (nFeature == aoc::map::FeatureType::Forest ||
            nFeature == aoc::map::FeatureType::Jungle) {
            nearForest = true;
        }
    }

    float modifier = 1.0f;

    switch (buildingId.value) {
        case 0:  // Forge
            if (cityFeature == aoc::map::FeatureType::Hills) {
                modifier += 0.20f;
            }
            if (nearMountain) {
                modifier += 0.10f;
            }
            if (cityTerrain == aoc::map::TerrainType::Desert) {
                modifier -= 0.15f;
            }
            break;
        case 1:  // Workshop
            if (nearForest) {
                modifier += 0.15f;
            }
            if (cityTerrain == aoc::map::TerrainType::Plains) {
                modifier += 0.10f;
            }
            if (cityTerrain == aoc::map::TerrainType::Snow) {
                modifier -= 0.20f;
            }
            break;
        case 2:  // Refinery
            if (nearCoast) {
                modifier += 0.10f;
            }
            if (cityTerrain == aoc::map::TerrainType::Desert) {
                modifier += 0.15f;
            }
            if (nearMountain) {
                modifier -= 0.15f;
            }
            break;
        case 3:  // Factory
            if (hasRiver) {
                modifier += 0.15f;
            }
            if (cityTerrain == aoc::map::TerrainType::Plains) {
                modifier += 0.10f;
            }
            if (cityTerrain == aoc::map::TerrainType::Tundra) {
                modifier -= 0.15f;
            }
            break;
        case 4:  // Electronics Plant
            if (nearCoast) {
                modifier += 0.05f;
            }
            break;
        case 7:  // Library
            if (nearMountain) {
                modifier += 0.15f;  // Observatory effect
            }
            break;
        case 12: // Research Lab
            if (nearMountain) {
                modifier += 0.10f;
            }
            break;
        case 23: // Shipyard
            if (nearCoast) {
                modifier += 0.20f;
            }
            break;
        default:
            break;
    }

    return std::clamp(modifier, 0.5f, 2.0f);
}

float computeImprovementEnvironmentModifier(const aoc::map::HexGrid& grid,
                                              int32_t tileIndex,
                                              aoc::map::ImprovementType improvement) {
    const aoc::map::TerrainType terrain = grid.terrain(tileIndex);
    const aoc::map::FeatureType feature = grid.feature(tileIndex);
    const bool hasRiver = (grid.riverEdges(tileIndex) != 0);

    // Check neighbors for mountain adjacency
    bool nearMountain = false;
    const hex::AxialCoord axial = grid.toAxial(tileIndex);
    const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);
    for (const hex::AxialCoord& n : nbrs) {
        if (grid.isValid(n) &&
            grid.terrain(grid.toIndex(n)) == aoc::map::TerrainType::Mountain) {
            nearMountain = true;
            break;
        }
    }

    switch (improvement) {
        case aoc::map::ImprovementType::Farm:
            if (feature == aoc::map::FeatureType::Floodplains) {
                return 1.50f;
            }
            if (hasRiver) {
                return 1.25f;  // River irrigation
            }
            if (terrain == aoc::map::TerrainType::Grassland) {
                return 1.10f;
            }
            if (terrain == aoc::map::TerrainType::Desert) {
                return 0.50f;
            }
            if (terrain == aoc::map::TerrainType::Tundra) {
                return 0.70f;
            }
            break;

        case aoc::map::ImprovementType::Mine:
            if (feature == aoc::map::FeatureType::Hills) {
                return 1.30f;
            }
            if (nearMountain) {
                return 1.20f;
            }
            break;

        case aoc::map::ImprovementType::LumberMill:
            if (feature == aoc::map::FeatureType::Forest) {
                return 1.25f;
            }
            if (feature == aoc::map::FeatureType::Jungle) {
                return 1.10f;
            }
            break;

        case aoc::map::ImprovementType::Plantation:
            if (terrain == aoc::map::TerrainType::Plains ||
                terrain == aoc::map::TerrainType::Grassland) {
                return 1.15f;
            }
            if (terrain == aoc::map::TerrainType::Tundra) {
                return 0.60f;
            }
            break;

        case aoc::map::ImprovementType::Pasture:
            if (terrain == aoc::map::TerrainType::Grassland) {
                return 1.20f;
            }
            if (terrain == aoc::map::TerrainType::Plains) {
                return 1.10f;
            }
            break;

        case aoc::map::ImprovementType::Camp:
            if (feature == aoc::map::FeatureType::Forest) {
                return 1.20f;
            }
            if (terrain == aoc::map::TerrainType::Tundra) {
                return 1.15f;
            }
            break;

        case aoc::map::ImprovementType::Quarry:
            if (feature == aoc::map::FeatureType::Hills) {
                return 1.25f;
            }
            break;

        case aoc::map::ImprovementType::FishingBoats:
            return 1.0f;  // No terrain modifier for fishing boats

        default:
            break;
    }

    return 1.0f;
}

} // namespace aoc::sim
