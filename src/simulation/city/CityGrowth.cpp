/**
 * @file CityGrowth.cpp
 * @brief City population growth logic.
 */

#include "aoc/simulation/city/CityGrowth.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/production/Waste.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ecs/World.hpp"

#include <cmath>

namespace aoc::sim {

float foodForGrowth(int32_t currentPopulation) {
    float pop = static_cast<float>(currentPopulation);
    float base = 15.0f + 6.0f * pop + std::pow(pop, 1.3f);
    return base * GamePace::instance().growthMultiplier;
}

void processCityGrowth(aoc::ecs::World& world,
                        const aoc::map::HexGrid& grid,
                        PlayerId player) {
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        CityComponent& city = cityPool->data()[i];
        if (city.owner != player) {
            continue;
        }

        // Calculate food from worked tiles
        float totalFood = 0.0f;
        for (const hex::AxialCoord& tileCoord : city.workedTiles) {
            if (!grid.isValid(tileCoord)) {
                continue;
            }
            int32_t tileIndex = grid.toIndex(tileCoord);
            aoc::map::TileYield yield = grid.tileYield(tileIndex);
            totalFood += static_cast<float>(yield.food);
        }

        // Food consumption: 2 per citizen
        float consumption = static_cast<float>(city.population) * 2.0f;
        float surplus = totalFood - consumption;

        // Pollution growth penalty
        const CityPollutionComponent* pollution =
            world.tryGetComponent<CityPollutionComponent>(cityPool->entities()[i]);
        if (pollution != nullptr) {
            surplus *= pollution->growthModifier();
        }

        city.foodSurplus += surplus;

        // Growth check
        float needed = foodForGrowth(city.population);
        if (city.foodSurplus >= needed) {
            city.foodSurplus -= needed;
            ++city.population;

            // Auto-assign new citizen to best unworked adjacent tile
            hex::AxialCoord center = city.location;
            std::array<hex::AxialCoord, 6> neighbors = hex::neighbors(center);
            float bestYieldValue = -1.0f;
            hex::AxialCoord bestTile = center;
            bool foundNew = false;

            for (const hex::AxialCoord& nbr : neighbors) {
                if (!grid.isValid(nbr)) {
                    continue;
                }
                // Check not already worked
                bool alreadyWorked = false;
                for (const hex::AxialCoord& worked : city.workedTiles) {
                    if (worked == nbr) {
                        alreadyWorked = true;
                        break;
                    }
                }
                if (alreadyWorked) {
                    continue;
                }

                int32_t idx = grid.toIndex(nbr);
                if (aoc::map::isWater(grid.terrain(idx)) || aoc::map::isImpassable(grid.terrain(idx))) {
                    continue;
                }

                aoc::map::TileYield yield = grid.tileYield(idx);
                float value = static_cast<float>(yield.food) * 2.0f
                            + static_cast<float>(yield.production)
                            + static_cast<float>(yield.gold) * 0.5f;
                if (value > bestYieldValue) {
                    bestYieldValue = value;
                    bestTile = nbr;
                    foundNew = true;
                }
            }

            if (foundNew) {
                city.workedTiles.push_back(bestTile);
            }

            LOG_INFO("%s grew to pop %d", city.name.c_str(), city.population);
        }

        // Clamp surplus: don't let it go below -50 (buffer against oscillation)
        if (city.foodSurplus < -50.0f) {
            city.foodSurplus = -50.0f;
        }

        // Starvation: only lose population at extreme deficit (surplus < -30)
        if (city.foodSurplus < -30.0f && city.population > 1) {
            --city.population;
            city.foodSurplus = 0.0f;
            if (!city.workedTiles.empty() && city.workedTiles.size() > 1) {
                city.workedTiles.pop_back();
            }
            LOG_WARN("%s lost population (starvation), now %d",
                     city.name.c_str(), city.population);
        }
    }
}

} // namespace aoc::sim
