/**
 * @file Climate.cpp
 * @brief Global climate system implementation.
 */

#include "aoc/simulation/climate/Climate.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

void GlobalClimateComponent::addCO2(float amount) {
    this->co2Level += amount;
    // Temperature rises 0.01 degrees per 10 CO2
    this->globalTemperature = this->co2Level * 0.001f;
}

void GlobalClimateComponent::processTurn(aoc::map::HexGrid& grid, aoc::Random& rng) {
    if (this->globalTemperature < 1.0f) {
        // No climate disasters below 1.0 degrees
        return;
    }

    const int32_t tileCount = grid.tileCount();

    for (int32_t i = 0; i < tileCount; ++i) {
        const aoc::map::TerrainType terrain = grid.terrain(i);

        if (terrain == aoc::map::TerrainType::Coast) {
            // Flood chance based on temperature
            const float floodChance = (this->globalTemperature >= 2.0f) ? 0.10f : 0.05f;
            if (rng.chance(floodChance)) {
                // Coast tile floods and becomes ocean
                grid.setTerrain(i, aoc::map::TerrainType::Ocean);
                ++this->seaLevelRise;
                LOG_INFO("Climate: coastal tile %d flooded (temp=%.2f)",
                         i, static_cast<double>(this->globalTemperature));
            }
        }

        // Droughts on grassland/plains at temperature >= 2.0
        if (this->globalTemperature >= 2.0f) {
            if (terrain == aoc::map::TerrainType::Grassland ||
                terrain == aoc::map::TerrainType::Plains) {
                constexpr float DROUGHT_CHANCE = 0.05f;
                if (rng.chance(DROUGHT_CHANCE)) {
                    // Drought: convert to desert
                    grid.setTerrain(i, aoc::map::TerrainType::Desert);
                    LOG_INFO("Climate: drought at tile %d (temp=%.2f)",
                             i, static_cast<double>(this->globalTemperature));
                }
            }
        }
    }
}

} // namespace aoc::sim
