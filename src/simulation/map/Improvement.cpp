/**
 * @file Improvement.cpp
 * @brief Tile improvement placement logic.
 */

#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
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

bool canProspect(const aoc::map::HexGrid& grid, int32_t index) {
    if (grid.resource(index).isValid()) {
        return false;  // Already has a resource
    }
    aoc::map::TerrainType terrain = grid.terrain(index);
    if (aoc::map::isWater(terrain) || aoc::map::isImpassable(terrain)) {
        return false;
    }
    if (grid.prospectCooldown(index) > 0) {
        return false;  // Recently surveyed, nothing new to find yet
    }
    return true;
}

bool prospectTile(aoc::map::HexGrid& grid, int32_t index,
                  float techBonus, uint32_t rngSeed) {
    if (!canProspect(grid, index)) {
        return false;
    }

    aoc::map::TerrainType terrain = grid.terrain(index);
    aoc::map::FeatureType feature = grid.feature(index);

    // Deterministic "random" from seed
    uint32_t hash = rngSeed * 2654435761u + static_cast<uint32_t>(index) * 2246822519u;
    float roll = static_cast<float>(hash % 10000u) / 10000.0f;

    // Determine base success rate and possible resources by terrain
    float baseChance = 0.0f;
    ResourceId discovered{};

    if (feature == aoc::map::FeatureType::Hills) {
        baseChance = 0.25f;
        // Weighted selection among minerals
        uint32_t mineralRoll = (hash >> 8) % 100u;
        if (mineralRoll < 25)      { discovered = ResourceId{aoc::sim::goods::IRON_ORE}; }
        else if (mineralRoll < 45) { discovered = ResourceId{aoc::sim::goods::COPPER_ORE}; }
        else if (mineralRoll < 60) { discovered = ResourceId{aoc::sim::goods::COAL}; }
        else if (mineralRoll < 72) { discovered = ResourceId{aoc::sim::goods::TIN}; }
        else if (mineralRoll < 82) { discovered = ResourceId{aoc::sim::goods::SILVER_ORE}; }
        else if (mineralRoll < 90) { discovered = ResourceId{aoc::sim::goods::GOLD_ORE}; }
        else                       { discovered = ResourceId{aoc::sim::goods::STONE}; }
    } else if (terrain == aoc::map::TerrainType::Desert) {
        baseChance = 0.15f;
        discovered = ResourceId{aoc::sim::goods::OIL};
    } else if (terrain == aoc::map::TerrainType::Plains) {
        baseChance = 0.10f;
        uint32_t plainsRoll = (hash >> 12) % 100u;
        if (plainsRoll < 60) { discovered = ResourceId{aoc::sim::goods::OIL}; }
        else                 { discovered = ResourceId{aoc::sim::goods::NITER}; }
    } else if (terrain == aoc::map::TerrainType::Tundra) {
        baseChance = 0.15f;
        uint32_t tundraRoll = (hash >> 12) % 100u;
        if (tundraRoll < 60) { discovered = ResourceId{aoc::sim::goods::COAL}; }
        else                 { discovered = ResourceId{aoc::sim::goods::GEMS}; }
    } else {
        baseChance = 0.05f;
        uint32_t otherRoll = (hash >> 12) % 100u;
        if (otherRoll < 50) { discovered = ResourceId{aoc::sim::goods::STONE}; }
        else                { discovered = ResourceId{aoc::sim::goods::CLAY}; }
    }

    float totalChance = baseChance + techBonus;

    if (roll < totalChance && discovered.isValid()) {
        // Discovery! Place the resource with reserves.
        int16_t reserves = aoc::sim::defaultReserves(discovered.value);
        grid.setResource(index, discovered);
        grid.setReserves(index, reserves);
        grid.setProspectCooldown(index, 0);  // No cooldown on success
        return true;
    }

    // Failed: set cooldown (15 turns before re-prospecting)
    grid.setProspectCooldown(index, 15);
    return false;
}

int32_t computeFarmAdjacencyBonus(const aoc::map::HexGrid& grid, int32_t index) {
    if (grid.improvement(index) != aoc::map::ImprovementType::Farm) {
        return 0;
    }

    // Count adjacent tiles that also have Farms
    aoc::hex::AxialCoord center = grid.toAxial(index);
    std::array<aoc::hex::AxialCoord, 6> neighbors = aoc::hex::neighbors(center);

    int32_t adjacentFarms = 0;
    for (const aoc::hex::AxialCoord& nbr : neighbors) {
        if (!grid.isValid(nbr)) {
            continue;
        }
        int32_t nbrIdx = grid.toIndex(nbr);
        if (grid.improvement(nbrIdx) == aoc::map::ImprovementType::Farm) {
            ++adjacentFarms;
        }
    }

    // Bonus: +1 food if 2+ adjacent farms (forming a triangle/cluster of 3+)
    return (adjacentFarms >= 2) ? 1 : 0;
}

} // namespace aoc::sim
