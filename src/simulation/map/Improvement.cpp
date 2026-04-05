/**
 * @file Improvement.cpp
 * @brief Tile improvement placement logic.
 */

#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/map/Terrain.hpp"

namespace aoc::sim {

bool canPlaceImprovement(const aoc::map::HexGrid& grid,
                          int32_t index,
                          aoc::map::ImprovementType type) {
    aoc::map::TerrainType terrain = grid.terrain(index);
    aoc::map::FeatureType feature = grid.feature(index);

    // Cannot improve water (except FishingBoats on coast with fish)
    if (type != aoc::map::ImprovementType::FishingBoats && aoc::map::isWater(terrain)) {
        return false;
    }

    // Cannot improve mountains
    if (terrain == aoc::map::TerrainType::Mountain) {
        return false;
    }

    switch (type) {
        case aoc::map::ImprovementType::Farm:
            // Farm on grassland or plains (not on hills, forest, or jungle)
            return (terrain == aoc::map::TerrainType::Grassland ||
                    terrain == aoc::map::TerrainType::Plains) &&
                   feature != aoc::map::FeatureType::Hills &&
                   feature != aoc::map::FeatureType::Forest &&
                   feature != aoc::map::FeatureType::Jungle;

        case aoc::map::ImprovementType::Mine:
            // Mine on hills
            return feature == aoc::map::FeatureType::Hills;

        case aoc::map::ImprovementType::Plantation:
            // Plantation on jungle with luxury resource
            return feature == aoc::map::FeatureType::Jungle &&
                   grid.resource(index).isValid();

        case aoc::map::ImprovementType::Quarry:
            // Quarry on hills with stone resource
            return feature == aoc::map::FeatureType::Hills &&
                   grid.resource(index).isValid();

        case aoc::map::ImprovementType::LumberMill:
            // Lumber mill on forest
            return feature == aoc::map::FeatureType::Forest;

        case aoc::map::ImprovementType::Camp:
            // Camp on tundra or forest with furs resource
            return grid.resource(index).isValid() &&
                   (terrain == aoc::map::TerrainType::Tundra ||
                    feature == aoc::map::FeatureType::Forest);

        case aoc::map::ImprovementType::Pasture:
            // Pasture on grassland/plains with horses or cattle resource
            return grid.resource(index).isValid() &&
                   (terrain == aoc::map::TerrainType::Grassland ||
                    terrain == aoc::map::TerrainType::Plains);

        case aoc::map::ImprovementType::FishingBoats:
            // Fishing boats on coast with fish resource
            return terrain == aoc::map::TerrainType::Coast &&
                   grid.resource(index).isValid();

        case aoc::map::ImprovementType::Fort:
            // Fort on any land tile
            return !aoc::map::isWater(terrain);

        case aoc::map::ImprovementType::Road:
            // Road on any land tile
            return !aoc::map::isWater(terrain);

        case aoc::map::ImprovementType::None:
        case aoc::map::ImprovementType::Count:
            return false;
    }

    return false;
}

aoc::map::ImprovementType bestImprovementForTile(
    const aoc::map::HexGrid& grid, int32_t index) {
    aoc::map::TerrainType terrain = grid.terrain(index);
    aoc::map::FeatureType feature = grid.feature(index);

    // Water tiles
    if (aoc::map::isWater(terrain)) {
        if (terrain == aoc::map::TerrainType::Coast && grid.resource(index).isValid()) {
            return aoc::map::ImprovementType::FishingBoats;
        }
        return aoc::map::ImprovementType::None;
    }

    // Mountains
    if (terrain == aoc::map::TerrainType::Mountain) {
        return aoc::map::ImprovementType::None;
    }

    // Hills -> Mine
    if (feature == aoc::map::FeatureType::Hills) {
        return aoc::map::ImprovementType::Mine;
    }

    // Forest -> LumberMill
    if (feature == aoc::map::FeatureType::Forest) {
        return aoc::map::ImprovementType::LumberMill;
    }

    // Jungle with resource -> Plantation
    if (feature == aoc::map::FeatureType::Jungle && grid.resource(index).isValid()) {
        return aoc::map::ImprovementType::Plantation;
    }

    // Grassland/plains with resource -> Pasture
    if (grid.resource(index).isValid() &&
        (terrain == aoc::map::TerrainType::Grassland ||
         terrain == aoc::map::TerrainType::Plains)) {
        return aoc::map::ImprovementType::Pasture;
    }

    // Grassland/plains -> Farm
    if (terrain == aoc::map::TerrainType::Grassland ||
        terrain == aoc::map::TerrainType::Plains) {
        return aoc::map::ImprovementType::Farm;
    }

    // Tundra with resource -> Camp
    if (terrain == aoc::map::TerrainType::Tundra && grid.resource(index).isValid()) {
        return aoc::map::ImprovementType::Camp;
    }

    return aoc::map::ImprovementType::None;
}

} // namespace aoc::sim
