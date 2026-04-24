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
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

// WP-A4: apply a lasting amenity hit to the given city. Accumulates into the
// city's disasterUnhappiness pool (decays 10%/turn in computeCityHappiness),
// so repeat disasters stack but a single hit does not linger forever.
static void applyDisasterAmenityHit(aoc::game::City& city, float delta) {
    CityHappinessComponent& hp = city.happiness();
    hp.disasterUnhappiness = std::min(10.0f, hp.disasterUnhappiness + delta);
}

int32_t processNaturalDisasters(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                                int32_t turnNumber, float globalTemp) {
    int32_t disasterCount = 0;
    const int32_t totalTiles = grid.tileCount();

    // WP-A4: climate globalTemp is a 0-10 delta (co2Level * 0.001). Old code
    // compared against 14/15/16 as if it were absolute °C, so the delta-based
    // triggers never fired. Rescale: +5% disaster rate per +0.1 temperature
    // delta, capped at +250% (globalTemp == 5.0). Beyond that, late-game
    // warming is already saturating the map with base disasters.
    const float tempMultiplier = 1.0f + std::min(5.0f, std::max(0.0f, globalTemp)) * 0.5f;

    for (int32_t i = 0; i < totalTiles; ++i) {
        const uint32_t hash = static_cast<uint32_t>(i) * 2654435761u
                            + static_cast<uint32_t>(turnNumber) * 2246822519u;

        const aoc::map::TerrainType terrain = grid.terrain(i);
        const aoc::map::FeatureType feature = grid.feature(i);
        const int8_t elevation = grid.elevation(i);

        // Volcanic eruption: high-elevation mountain tiles.
        if (terrain == aoc::map::TerrainType::Mountain && elevation >= 2) {
            const uint32_t threshold = static_cast<uint32_t>(5000000.0f * tempMultiplier);
            if (hash < threshold) {
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
            if (eqHash < threshold) {
                const hex::AxialCoord center = grid.toAxial(i);
                bool damaged = false;
                for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
                    for (const std::unique_ptr<aoc::game::City>& city : playerPtr->cities()) {
                        if (grid.distance(city->location(), center) <= 2) {
                            CityDistrictsComponent& districts = city->districts();
                            for (CityDistrictsComponent::PlacedDistrict& d : districts.districts) {
                                if (!d.buildings.empty()) {
                                    d.buildings.pop_back();
                                    break;
                                }
                            }
                            applyDisasterAmenityHit(*city, 1.5f);
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

        // Drought: plains/grassland tiles above a climate-delta threshold.
        // WP-A4: rescaled to the 0-10 globalTemp delta. Fires once climate
        // rises >+0.8; lasting effect (Grassland→Plains, Plains→Desert).
        if ((terrain == aoc::map::TerrainType::Plains
             || terrain == aoc::map::TerrainType::Grassland)
            && globalTemp > 0.8f) {
            const uint32_t drHash = hash * 7919u;
            const uint32_t threshold = static_cast<uint32_t>(
                2000000.0f * (globalTemp - 0.5f));
            if (drHash < threshold) {
                if (terrain == aoc::map::TerrainType::Grassland) {
                    grid.setTerrain(i, aoc::map::TerrainType::Plains);
                } else {
                    grid.setTerrain(i, aoc::map::TerrainType::Desert);
                }
                ++disasterCount;
                LOG_INFO("DROUGHT at tile %d (temp=%.2f)",
                         i, static_cast<double>(globalTemp));
            }
        }

        // Wildfire: forest tiles at high climate delta.
        if (feature == aoc::map::FeatureType::Forest && globalTemp > 1.2f) {
            const uint32_t fireHash = hash * 15485863u;
            const uint32_t threshold = static_cast<uint32_t>(
                15000000.0f * (globalTemp - 1.0f));
            if (fireHash < threshold) {
                grid.setFeature(i, aoc::map::FeatureType::None);
                ++disasterCount;
                LOG_INFO("WILDFIRE destroyed forest at tile %d", i);
            }
        }

        // Hurricane: coastal tiles at high climate delta; damages improvements,
        // naval units, and the nearest coastal city (amenity hit).
        // Audit 2026-04: Hurricane still fired 44k events across 12 × 1000t
        // sims (proportional to 9k @ 500t). globalTemp saturates at 10 for
        // most of mid-to-late game so a per-tile coefficient of 2M * 8.8
        // still yields ~18/turn per sim. Drop another 4× to 500k.
        if (terrain == aoc::map::TerrainType::Coast && globalTemp > 1.5f) {
            const uint32_t hurHash = hash * 999983u;
            // Audit 2026-04 second pass: 13k events at 1000t still ~10×
            // earthquake rate. Drop coefficient another 3× so hurricane
            // lands in the 3-5k range per batch, comparable to volcanic.
            const uint32_t threshold = static_cast<uint32_t>(
                150000.0f * (globalTemp - 1.2f));
            if (hurHash < threshold) {
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
                    // Amenity hit to nearest coastal-adjacent city (<=2 hexes).
                    for (const std::unique_ptr<aoc::game::City>& city : playerPtr->cities()) {
                        if (grid.distance(city->location(), tileCoord) <= 2) {
                            applyDisasterAmenityHit(*city, 0.75f);
                            break;
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
