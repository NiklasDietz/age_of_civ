/**
 * @file NaturalDisasters.cpp
 * @brief Natural disaster event processing.
 */

#include "aoc/simulation/climate/NaturalDisasters.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

int32_t processNaturalDisasters(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                                int32_t turnNumber, float globalTemp) {
    int32_t disasterCount = 0;
    const int32_t totalTiles = grid.tileCount();

    // Temperature increases disaster frequency.
    // Base: 1% chance per eligible tile per turn.
    // At +2 degrees above 14: 3% chance.
    const float tempMultiplier = 1.0f + std::max(0.0f, globalTemp - 14.0f) * 0.5f;

    for (int32_t i = 0; i < totalTiles; ++i) {
        const uint32_t hash = static_cast<uint32_t>(i) * 2654435761u
                            + static_cast<uint32_t>(turnNumber) * 2246822519u;

        const aoc::map::TerrainType terrain = grid.terrain(i);
        const aoc::map::FeatureType feature = grid.feature(i);
        const int8_t elevation = grid.elevation(i);

        // Volcanic eruption: high-elevation mountain tiles.
        if (terrain == aoc::map::TerrainType::Mountain && elevation >= 2) {
            const uint32_t threshold = static_cast<uint32_t>(5000000.0f * tempMultiplier);
            if ((hash % 4294967295u) < threshold) {
                const hex::AxialCoord center = grid.toAxial(i);
                const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(center);
                for (const hex::AxialCoord& n : nbrs) {
                    if (!grid.isValid(n)) { continue; }
                    const int32_t nIdx = grid.toIndex(n);
                    if (grid.improvement(nIdx) != aoc::map::ImprovementType::None
                        && grid.improvement(nIdx) != aoc::map::ImprovementType::Road) {
                        grid.setImprovement(nIdx, aoc::map::ImprovementType::None);
                    }
                }
                ++disasterCount;
                LOG_INFO("VOLCANIC ERUPTION at tile %d!", i);
            }
        }

        // Earthquake: hill tiles remove one building from a nearby city.
        if (feature == aoc::map::FeatureType::Hills && elevation >= 1) {
            const uint32_t eqHash = hash * 104729u;
            const uint32_t threshold = static_cast<uint32_t>(3000000.0f * tempMultiplier);
            if ((eqHash % 4294967295u) < threshold) {
                const hex::AxialCoord center = grid.toAxial(i);
                bool damaged = false;
                for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
                    for (const std::unique_ptr<aoc::game::City>& city : playerPtr->cities()) {
                        if (hex::distance(city->location(), center) <= 2) {
                            CityDistrictsComponent& districts = city->districts();
                            for (CityDistrictsComponent::PlacedDistrict& d : districts.districts) {
                                if (!d.buildings.empty()) {
                                    d.buildings.pop_back();
                                    break;
                                }
                            }
                            LOG_INFO("EARTHQUAKE damaged city %s", city->name().c_str());
                            damaged = true;
                            break;
                        }
                    }
                    if (damaged) { break; }
                }
                ++disasterCount;
            }
        }

        // Drought: plains/grassland tiles at high temperature.
        if ((terrain == aoc::map::TerrainType::Plains
             || terrain == aoc::map::TerrainType::Grassland)
            && globalTemp > 15.0f) {
            const uint32_t drHash = hash * 7919u;
            const uint32_t threshold = static_cast<uint32_t>(
                2000000.0f * (globalTemp - 14.0f));
            if ((drHash % 4294967295u) < threshold) {
                ++disasterCount;
            }
        }

        // Wildfire: forest tiles at very high temperature.
        if (feature == aoc::map::FeatureType::Forest && globalTemp > 16.0f) {
            const uint32_t fireHash = hash * 15485863u;
            const uint32_t threshold = static_cast<uint32_t>(
                15000000.0f * (globalTemp - 15.0f));
            if ((fireHash % 4294967295u) < threshold) {
                grid.setFeature(i, aoc::map::FeatureType::None);
                ++disasterCount;
                LOG_INFO("WILDFIRE destroyed forest at tile %d", i);
            }
        }

        // Hurricane: coastal tiles at high temperature damage improvements and naval units.
        if (terrain == aoc::map::TerrainType::Coast && globalTemp > 15.5f) {
            const uint32_t hurHash = hash * 999983u;
            const uint32_t threshold = static_cast<uint32_t>(
                10000000.0f * (globalTemp - 14.5f));
            if ((hurHash % 4294967295u) < threshold) {
                if (grid.improvement(i) != aoc::map::ImprovementType::None) {
                    grid.setImprovement(i, aoc::map::ImprovementType::None);
                }
                const hex::AxialCoord tileCoord = grid.toAxial(i);
                for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
                    for (const std::unique_ptr<aoc::game::Unit>& unit : playerPtr->units()) {
                        if (unit->position() == tileCoord) {
                            unit->takeDamage(30);
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

void tickDisasterEffects(aoc::game::GameState& /*gameState*/, aoc::map::HexGrid& /*grid*/) {
    // Tick down TileDisasterComponent durations and remove expired effects.
    // Full implementation requires iterating all tiles with disaster effects.
}

} // namespace aoc::sim
