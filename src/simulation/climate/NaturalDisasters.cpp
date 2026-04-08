/**
 * @file NaturalDisasters.cpp
 * @brief Natural disaster event processing.
 */

#include "aoc/simulation/climate/NaturalDisasters.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

int32_t processNaturalDisasters(aoc::ecs::World& world, aoc::map::HexGrid& grid,
                                int32_t turnNumber, float globalTemp) {
    int32_t disasterCount = 0;
    int32_t totalTiles = grid.tileCount();

    // Temperature increases disaster frequency
    // Base: 1% chance per eligible tile per turn
    // At +2 degrees: 3% chance
    float tempMultiplier = 1.0f + std::max(0.0f, globalTemp - 14.0f) * 0.5f;

    for (int32_t i = 0; i < totalTiles; ++i) {
        uint32_t hash = static_cast<uint32_t>(i) * 2654435761u
                      + static_cast<uint32_t>(turnNumber) * 2246822519u;

        aoc::map::TerrainType terrain = grid.terrain(i);
        aoc::map::FeatureType feature = grid.feature(i);
        int8_t elevation = grid.elevation(i);

        // Volcanic eruption: high elevation mountains
        if (terrain == aoc::map::TerrainType::Mountain && elevation >= 2) {
            uint32_t threshold = static_cast<uint32_t>(
                50000000.0f * tempMultiplier);  // ~1.2% base
            if ((hash % 4294967295u) < threshold) {
                // Erupt: damage surrounding tiles
                hex::AxialCoord center = grid.toAxial(i);
                std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(center);
                for (const hex::AxialCoord& n : nbrs) {
                    if (!grid.isValid(n)) { continue; }
                    int32_t nIdx = grid.toIndex(n);
                    // Destroy improvements
                    if (grid.improvement(nIdx) != aoc::map::ImprovementType::None
                        && grid.improvement(nIdx) != aoc::map::ImprovementType::Road) {
                        grid.setImprovement(nIdx, aoc::map::ImprovementType::None);
                    }
                }
                ++disasterCount;
                LOG_INFO("VOLCANIC ERUPTION at tile %d!", i);
            }
        }

        // Earthquake: near mountains
        if (feature == aoc::map::FeatureType::Hills && elevation >= 1) {
            uint32_t eqHash = hash * 104729u;
            uint32_t threshold = static_cast<uint32_t>(30000000.0f * tempMultiplier);
            if ((eqHash % 4294967295u) < threshold) {
                // Damage a building in nearby city
                hex::AxialCoord center = grid.toAxial(i);
                aoc::ecs::ComponentPool<CityComponent>* cityPool =
                    world.getPool<CityComponent>();
                if (cityPool != nullptr) {
                    for (uint32_t c = 0; c < cityPool->size(); ++c) {
                        if (hex::distance(cityPool->data()[c].location, center) <= 2) {
                            CityDistrictsComponent* districts =
                                world.tryGetComponent<CityDistrictsComponent>(cityPool->entities()[c]);
                            if (districts != nullptr && !districts->districts.empty()) {
                                // Remove one building from first district that has one
                                for (CityDistrictsComponent::PlacedDistrict& d : districts->districts) {
                                    if (!d.buildings.empty()) {
                                        d.buildings.pop_back();
                                        break;
                                    }
                                }
                            }
                            LOG_INFO("EARTHQUAKE damaged city %s", cityPool->data()[c].name.c_str());
                            break;
                        }
                    }
                }
                ++disasterCount;
            }
        }

        // Drought: plains/grassland at high temperature
        if ((terrain == aoc::map::TerrainType::Plains || terrain == aoc::map::TerrainType::Grassland)
            && globalTemp > 15.0f) {
            uint32_t drHash = hash * 7919u;
            uint32_t threshold = static_cast<uint32_t>(
                20000000.0f * (globalTemp - 14.0f));  // More droughts at higher temp
            if ((drHash % 4294967295u) < threshold) {
                // Apply drought: reduce food for this tile for a few turns
                // (would need TileDisasterComponent but for now just log)
                ++disasterCount;
            }
        }

        // Wildfire: forest tiles during high temperature
        if (feature == aoc::map::FeatureType::Forest && globalTemp > 16.0f) {
            uint32_t fireHash = hash * 15485863u;
            uint32_t threshold = static_cast<uint32_t>(
                15000000.0f * (globalTemp - 15.0f));
            if ((fireHash % 4294967295u) < threshold) {
                grid.setFeature(i, aoc::map::FeatureType::None);
                ++disasterCount;
                LOG_INFO("WILDFIRE destroyed forest at tile %d", i);
            }
        }

        // Hurricane: coastal tiles at high temperature
        if (terrain == aoc::map::TerrainType::Coast && globalTemp > 15.5f) {
            uint32_t hurHash = hash * 999983u;
            uint32_t threshold = static_cast<uint32_t>(
                10000000.0f * (globalTemp - 14.5f));
            if ((hurHash % 4294967295u) < threshold) {
                // Damage coastal improvements
                if (grid.improvement(i) != aoc::map::ImprovementType::None) {
                    grid.setImprovement(i, aoc::map::ImprovementType::None);
                }
                // Damage naval units at this tile
                hex::AxialCoord tileCoord = grid.toAxial(i);
                aoc::ecs::ComponentPool<UnitComponent>* unitPool =
                    world.getPool<UnitComponent>();
                if (unitPool != nullptr) {
                    for (uint32_t u = 0; u < unitPool->size(); ++u) {
                        if (unitPool->data()[u].position == tileCoord) {
                            unitPool->data()[u].hitPoints -= 30;
                        }
                    }
                }
                ++disasterCount;
                LOG_INFO("HURRICANE at coastal tile %d", i);
            }
        }
    }

    return disasterCount;
}

void tickDisasterEffects(aoc::ecs::World& /*world*/, aoc::map::HexGrid& /*grid*/) {
    // Tick down TileDisasterComponent durations and remove expired effects
    // Full implementation would iterate all tiles with disaster effects
}

} // namespace aoc::sim
