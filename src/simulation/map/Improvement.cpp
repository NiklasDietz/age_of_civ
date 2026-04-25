/**
 * @file Improvement.cpp
 * @brief Tile improvement placement logic.
 */

#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/map/Terrain.hpp"

namespace aoc::sim {

bool canPlaceImprovement(const aoc::map::HexGrid& grid,
                          int32_t index,
                          aoc::map::ImprovementType type) {
    aoc::map::TerrainType terrain = grid.terrain(index);
    aoc::map::FeatureType feature = grid.feature(index);

    // Cannot improve water except for the few water-capable improvements.
    if (aoc::map::isWater(terrain)
        && type != aoc::map::ImprovementType::FishingBoats
        && type != aoc::map::ImprovementType::OffshorePlatform
        && type != aoc::map::ImprovementType::DesalinationPlant
        && type != aoc::map::ImprovementType::MangroveNursery
        && type != aoc::map::ImprovementType::KelpFarm
        && type != aoc::map::ImprovementType::FishFarm) {
        return false;
    }

    // Mountains only accept MountainMine; everything else is blocked.
    if (terrain == aoc::map::TerrainType::Mountain
        && type != aoc::map::ImprovementType::MountainMine) {
        return false;
    }

    // Cannot overwrite a canal (canals are major infrastructure projects)
    if (grid.hasCanal(index)) {
        return false;
    }

    // Canals are built via terrain projects (city production), not by builders
    if (type == aoc::map::ImprovementType::Canal) {
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

        case aoc::map::ImprovementType::MountainMine: {
            // MountainMine requires: mountain tile, a metal resource on it, and at
            // least one adjacent non-mountain tile that belongs to the same player.
            // The adjacency requirement is what keeps mountains impassable while
            // still letting a neighbouring city's workers access the deposit.
            if (terrain != aoc::map::TerrainType::Mountain) {
                return false;
            }
            ResourceId resId = grid.resource(index);
            if (!resId.isValid() || !isMountainMetal(resId.value)) {
                return false;
            }
            const PlayerId tileOwner = grid.owner(index);
            aoc::hex::AxialCoord center = grid.toAxial(index);
            std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(center);
            for (const aoc::hex::AxialCoord& nbr : nbrs) {
                if (!grid.isValid(nbr)) {
                    continue;
                }
                int32_t nbrIdx = grid.toIndex(nbr);
                if (grid.terrain(nbrIdx) == aoc::map::TerrainType::Mountain) {
                    continue;
                }
                if (aoc::map::isWater(grid.terrain(nbrIdx))) {
                    continue;
                }
                // Non-mountain land neighbour. If the mountain tile has no owner
                // yet, any owned non-mountain neighbour is sufficient. Otherwise
                // the neighbour must share the mountain's owner.
                const PlayerId nbrOwner = grid.owner(nbrIdx);
                if (tileOwner != INVALID_PLAYER) {
                    if (nbrOwner == tileOwner) {
                        return true;
                    }
                } else if (nbrOwner != INVALID_PLAYER) {
                    return true;
                }
            }
            return false;
        }

        case aoc::map::ImprovementType::Observatory:
            // Observatory on hills (any land).
            return feature == aoc::map::FeatureType::Hills;

        case aoc::map::ImprovementType::Monastery:
            // Monastery on any non-water, non-mountain land. Forest/Jungle ok.
            return !aoc::map::isWater(terrain) &&
                   terrain != aoc::map::TerrainType::Mountain;

        case aoc::map::ImprovementType::HeritageSite:
            // Heritage site on any non-water, non-mountain land.
            return !aoc::map::isWater(terrain) &&
                   terrain != aoc::map::TerrainType::Mountain;

        case aoc::map::ImprovementType::TerraceFarm:
            // Terrace farm on hills (no other feature gating).
            return feature == aoc::map::FeatureType::Hills;

        case aoc::map::ImprovementType::BiogasPlant: {
            // Biogas plant on any grassland/plains tile with an adjacent farm
            // to draw food from.
            if (terrain != aoc::map::TerrainType::Grassland &&
                terrain != aoc::map::TerrainType::Plains) {
                return false;
            }
            aoc::hex::AxialCoord center = grid.toAxial(index);
            std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(center);
            for (const aoc::hex::AxialCoord& n : nbrs) {
                if (!grid.isValid(n)) { continue; }
                if (grid.improvement(grid.toIndex(n)) == aoc::map::ImprovementType::Farm) {
                    return true;
                }
            }
            return false;
        }

        case aoc::map::ImprovementType::SolarFarm:
            // Solar farm on open desert (no feature).
            return terrain == aoc::map::TerrainType::Desert &&
                   feature == aoc::map::FeatureType::None;

        case aoc::map::ImprovementType::WindFarm:
            // Wind farm on hills or plains (open ground catches wind).
            return feature == aoc::map::FeatureType::Hills ||
                   (terrain == aoc::map::TerrainType::Plains &&
                    feature == aoc::map::FeatureType::None);

        case aoc::map::ImprovementType::OffshorePlatform: {
            // Offshore platform on coast/shallow water with an oil resource.
            if (!aoc::map::isShallowWater(terrain)) { return false; }
            ResourceId resId = grid.resource(index);
            return resId.isValid() && resId.value == aoc::sim::goods::OIL;
        }

        case aoc::map::ImprovementType::RecyclingCenter:
            // Recycling center on any non-water, non-mountain land.
            return !aoc::map::isWater(terrain) &&
                   terrain != aoc::map::TerrainType::Mountain;

        case aoc::map::ImprovementType::GeothermalVent: {
            // Flag tile adjacent to a mountain, on any non-water land.
            if (aoc::map::isWater(terrain)
                || terrain == aoc::map::TerrainType::Mountain) {
                return false;
            }
            aoc::hex::AxialCoord center = grid.toAxial(index);
            std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(center);
            for (const aoc::hex::AxialCoord& n : nbrs) {
                if (!grid.isValid(n)) { continue; }
                if (grid.terrain(grid.toIndex(n)) == aoc::map::TerrainType::Mountain) {
                    return true;
                }
            }
            return false;
        }

        case aoc::map::ImprovementType::DesalinationPlant:
            // Coast tile only.
            return terrain == aoc::map::TerrainType::Coast;

        case aoc::map::ImprovementType::VerticalFarm:
            // Any land, non-mountain.
            return !aoc::map::isWater(terrain)
                && terrain != aoc::map::TerrainType::Mountain;

        case aoc::map::ImprovementType::Greenhouse:
            // WP-C4: any non-water, non-mountain tile. Future work: check
            // per-crop climate metadata so off-climate crops at 50% yield.
            return !aoc::map::isWater(terrain)
                && terrain != aoc::map::TerrainType::Mountain;

        case aoc::map::ImprovementType::DataCenter:
            // Any land, non-mountain.
            return !aoc::map::isWater(terrain)
                && terrain != aoc::map::TerrainType::Mountain;

        case aoc::map::ImprovementType::TradingPost:
            // Desert or Plains open ground.
            return (terrain == aoc::map::TerrainType::Desert
                 || terrain == aoc::map::TerrainType::Plains)
                && feature != aoc::map::FeatureType::Hills
                && feature != aoc::map::FeatureType::Forest
                && feature != aoc::map::FeatureType::Jungle;

        case aoc::map::ImprovementType::MangroveNursery: {
            // Marsh feature on land, or Coast adjacent to a Marsh tile.
            if (feature == aoc::map::FeatureType::Marsh) {
                return true;
            }
            if (terrain != aoc::map::TerrainType::Coast) { return false; }
            aoc::hex::AxialCoord center = grid.toAxial(index);
            std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(center);
            for (const aoc::hex::AxialCoord& n : nbrs) {
                if (!grid.isValid(n)) { continue; }
                if (grid.feature(grid.toIndex(n)) == aoc::map::FeatureType::Marsh) {
                    return true;
                }
            }
            return false;
        }

        case aoc::map::ImprovementType::KelpFarm:
            // Coast, no resource on the tile.
            return terrain == aoc::map::TerrainType::Coast
                && !grid.resource(index).isValid();

        case aoc::map::ImprovementType::FishFarm:
            // Coast or ShallowWater. Empty water OR a water tile that
            // already holds a natural FISH resource (fish farm augments
            // the catch rather than replacing the school).
            return (terrain == aoc::map::TerrainType::Coast
                 || terrain == aoc::map::TerrainType::ShallowWater)
                && (!grid.resource(index).isValid()
                    || grid.resource(index).value == aoc::sim::goods::FISH);

        case aoc::map::ImprovementType::Railway:
        case aoc::map::ImprovementType::Highway:
        case aoc::map::ImprovementType::Dam:
        case aoc::map::ImprovementType::Vineyard:
        case aoc::map::ImprovementType::SilkFarm:
        case aoc::map::ImprovementType::SpiceFarm:
        case aoc::map::ImprovementType::DyeWorks:
        case aoc::map::ImprovementType::CottonField:
        case aoc::map::ImprovementType::Workshop:
        case aoc::map::ImprovementType::Canal:
        case aoc::map::ImprovementType::None:
        case aoc::map::ImprovementType::Count:
            return false;

        case aoc::map::ImprovementType::Encampment:
            // WP-S: military supply depot. Any non-water, non-mountain tile.
            // Owner gating handled by caller (8-hex check from own city).
            return !aoc::map::isWater(terrain)
                && terrain != aoc::map::TerrainType::Mountain;
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
        // Coast w/o resource: alternate KelpFarm / FishFarm for variety.
        if (terrain == aoc::map::TerrainType::Coast) {
            return (index % 2 == 0) ? aoc::map::ImprovementType::FishFarm
                                    : aoc::map::ImprovementType::KelpFarm;
        }
        if (terrain == aoc::map::TerrainType::ShallowWater
            && !grid.resource(index).isValid()) {
            return aoc::map::ImprovementType::FishFarm;
        }
        return aoc::map::ImprovementType::None;
    }

    // Mountains: only the Mountain Mine is allowed, and only when the tile hosts a
    // metal resource and has an adjacent non-mountain land tile that is owned.
    if (terrain == aoc::map::TerrainType::Mountain) {
        if (canPlaceImprovement(grid, index, aoc::map::ImprovementType::MountainMine)) {
            return aoc::map::ImprovementType::MountainMine;
        }
        return aoc::map::ImprovementType::None;
    }

    // Hills -> Mine. If no resource, occasionally pick Observatory (science tile).
    if (feature == aoc::map::FeatureType::Hills) {
        if (!grid.resource(index).isValid() && (index % 4 == 0)) {
            return aoc::map::ImprovementType::Observatory;
        }
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

    // Grassland/plains -> Farm, occasional Monastery/HeritageSite for diversity.
    if (terrain == aoc::map::TerrainType::Grassland ||
        terrain == aoc::map::TerrainType::Plains) {
        const int32_t bucket = index % 12;
        if (bucket == 0) { return aoc::map::ImprovementType::Monastery; }
        if (bucket == 1) { return aoc::map::ImprovementType::HeritageSite; }
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
        // WP-C2 cut GEMS; tundra prospect now redirects to LITHIUM on the
        // non-Coal branch so prospector still finds a strategic good.
        baseChance = 0.15f;
        uint32_t tundraRoll = (hash >> 12) % 100u;
        if (tundraRoll < 60) { discovered = ResourceId{aoc::sim::goods::COAL}; }
        else                 { discovered = ResourceId{aoc::sim::goods::LITHIUM}; }
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

int32_t countSameImprovementNeighbors(const aoc::map::HexGrid& grid,
                                      int32_t index,
                                      aoc::map::ImprovementType type) {
    aoc::hex::AxialCoord center = grid.toAxial(index);
    std::array<aoc::hex::AxialCoord, 6> neighbors = aoc::hex::neighbors(center);
    int32_t count = 0;
    for (const aoc::hex::AxialCoord& nbr : neighbors) {
        if (!grid.isValid(nbr)) { continue; }
        if (grid.improvement(grid.toIndex(nbr)) == type) {
            ++count;
        }
    }
    return count;
}

bool plantGreenhouseCrop(aoc::map::HexGrid& grid,
                          aoc::sim::CityStockpileComponent& cityStockpile,
                          int32_t tileIndex,
                          uint16_t cropGoodId) {
    if (grid.improvement(tileIndex) != aoc::map::ImprovementType::Greenhouse) {
        return false;
    }
    if (cropGoodId == 0xFFFFu) { return false; }
    if (!cityStockpile.consumeGoods(cropGoodId, 1)) {
        return false;
    }
    grid.setGreenhouseCrop(tileIndex, cropGoodId);
    return true;
}

aoc::map::TileYield computeImprovementClusterBonus(const aoc::map::HexGrid& grid,
                                                    int32_t index) {
    aoc::map::TileYield bonus{};
    const aoc::map::ImprovementType type = grid.improvement(index);

    // WP-G2 biogas: each adjacent BiogasPlant adds +1 prod. At 3+ it also
    // offsets the base -1 food penalty (+1 food), so a full cluster flips
    // biogas into a renewable match for a single OIL tile on prod while
    // staying neutral on food. Cap at +3 prod from adjacency alone.
    if (type == aoc::map::ImprovementType::BiogasPlant) {
        const int32_t n = countSameImprovementNeighbors(grid, index, type);
        bonus.production = static_cast<int8_t>(std::min(n, 3));
        if (n >= 3) {
            bonus.food = 1;
        }
        return bonus;
    }

    // WP-G3 solar: +1 gold +1 sci per adjacent SolarFarm, cap +2 each.
    if (type == aoc::map::ImprovementType::SolarFarm) {
        const int32_t n = countSameImprovementNeighbors(grid, index, type);
        const int32_t capped = std::min(n, 2);
        bonus.gold    = static_cast<int8_t>(capped);
        bonus.science = static_cast<int8_t>(capped);
        return bonus;
    }

    // WP-G3 wind: +1 prod per adjacent WindFarm, cap +2.
    if (type == aoc::map::ImprovementType::WindFarm) {
        const int32_t n = countSameImprovementNeighbors(grid, index, type);
        bonus.production = static_cast<int8_t>(std::min(n, 2));
        return bonus;
    }

    return bonus;
}

} // namespace aoc::sim
