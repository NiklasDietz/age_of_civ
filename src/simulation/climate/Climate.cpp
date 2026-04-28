/**
 * @file Climate.cpp
 * @brief Global climate system implementation.
 */

#include "aoc/simulation/climate/Climate.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

float climateFoodMultiplier(const GlobalClimateComponent& climate) {
    const float co2 = climate.co2Level;
    if (co2 <= 3000.0f) { return 1.0f; }                  // pre-industrial / early industrial: no hit
    if (co2 <= 5000.0f) {
        // 3000→5000: 1.00→0.95 (industrial era, soft warning)
        return 1.0f - 0.05f * (co2 - 3000.0f) / 2000.0f;
    }
    if (co2 <= 7500.0f) {
        // 5000→7500: 0.95→0.85 (modern era — should be transitioning to green)
        return 0.95f - 0.10f * (co2 - 5000.0f) / 2500.0f;
    }
    // 7500→CO2_MAX (10000): 0.85→0.70 (atomic/info-era crisis if no green energy)
    return 0.85f - 0.15f * std::min(1.0f, (co2 - 7500.0f) / 2500.0f);
}

void GlobalClimateComponent::addCO2(float amount) {
    this->co2Level = std::min(CO2_MAX, this->co2Level + amount);
    // Temperature rises 0.01 degrees per 10 CO2
    this->globalTemperature = this->co2Level * 0.001f;
}

void GlobalClimateComponent::processTurn(aoc::map::HexGrid& grid, aoc::Random& rng) {
    // H6.6: natural decay each turn. Net CO2 flat when emissions ~= 0.5/turn.
    this->co2Level = std::max(0.0f, this->co2Level - CO2_DECAY_PER_TURN);
    this->globalTemperature = this->co2Level * 0.001f;
    // No climate damage until meaningful warming. Previous 1.0 degree threshold
    // fired at co2 ~1000 which happens too early; by mid-game the entire coast
    // is ocean and the map looks progressively drabber. Push the floor up and
    // throttle the per-turn rate so climate change is a slow background effect
    // rather than a per-turn coastal wipe.
    if (this->globalTemperature < 1.5f) {
        return;
    }

    // Cap total terrain conversions per turn so a big map cannot lose hundreds
    // of coast tiles in one pass.
    constexpr int32_t MAX_FLOODS_PER_TURN   = 4;
    constexpr int32_t MAX_DROUGHTS_PER_TURN = 4;
    int32_t floodsThisTurn   = 0;
    int32_t droughtsThisTurn = 0;

    const int32_t tileCount = grid.tileCount();

    for (int32_t i = 0; i < tileCount; ++i) {
        if (floodsThisTurn >= MAX_FLOODS_PER_TURN
            && droughtsThisTurn >= MAX_DROUGHTS_PER_TURN) {
            break;
        }
        const aoc::map::TerrainType terrain = grid.terrain(i);

        if (terrain == aoc::map::TerrainType::Coast
            && floodsThisTurn < MAX_FLOODS_PER_TURN) {
            // Audit 2026-04: coast flood scaled to 10.9k events at 1000t.
            // Further drop 2× — flood is a strong narrative event that
            // shouldn't blanket every coast over a full game.
            const float floodChance = (this->globalTemperature >= 2.5f) ? 0.0004f : 0.0002f;
            if (rng.chance(floodChance)) {
                grid.setTerrain(i, aoc::map::TerrainType::Ocean);
                ++this->seaLevelRise;
                ++floodsThisTurn;
                LOG_INFO("Climate: coastal tile %d flooded (temp=%.2f)",
                         i, static_cast<double>(this->globalTemperature));
            }
        }

        if (this->globalTemperature >= 2.5f
            && droughtsThisTurn < MAX_DROUGHTS_PER_TURN) {
            if (terrain == aoc::map::TerrainType::Grassland ||
                terrain == aoc::map::TerrainType::Plains) {
                constexpr float DROUGHT_CHANCE = 0.003f;
                if (rng.chance(DROUGHT_CHANCE)) {
                    grid.setTerrain(i, aoc::map::TerrainType::Desert);
                    ++droughtsThisTurn;
                    LOG_INFO("Climate: drought at tile %d (temp=%.2f)",
                             i, static_cast<double>(this->globalTemperature));
                }
            }
        }
    }
}

} // namespace aoc::sim
