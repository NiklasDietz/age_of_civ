/**
 * @file CityScience.cpp
 * @brief Science and culture computation from cities.
 */

#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ecs/World.hpp"

#include <algorithm>

namespace aoc::sim {

float computePlayerScience(const aoc::ecs::World& world,
                            const aoc::map::HexGrid& grid,
                            PlayerId player) {
    const aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return 0.0f;
    }

    float totalScience = 0.0f;

    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        const CityComponent& city = cityPool->data()[i];
        if (city.owner != player) {
            continue;
        }
        EntityId cityEntity = cityPool->entities()[i];

        float cityScience = 0.0f;

        // 1. Science from worked tiles
        for (const hex::AxialCoord& tile : city.workedTiles) {
            if (grid.isValid(tile)) {
                aoc::map::TileYield yield = grid.tileYield(grid.toIndex(tile));
                cityScience += static_cast<float>(yield.science);
            }
        }

        // 2. Population base science
        cityScience += static_cast<float>(city.population) * 0.5f;

        // 3. Building bonuses and multiplier
        float bestMultiplier = 1.0f;
        const CityDistrictsComponent* districts =
            world.tryGetComponent<CityDistrictsComponent>(cityEntity);
        if (districts != nullptr) {
            for (const CityDistrictsComponent::PlacedDistrict& district : districts->districts) {
                for (BuildingId bid : district.buildings) {
                    if (bid.value < BUILDING_DEFS.size()) {
                        const BuildingDef& bdef = buildingDef(bid);
                        cityScience += static_cast<float>(bdef.scienceBonus);
                        bestMultiplier = std::max(bestMultiplier, bdef.scienceMultiplier);
                    }
                }
            }
        }

        // 4. Apply multiplier (e.g., Research Lab gives 1.5x)
        cityScience *= bestMultiplier;

        totalScience += cityScience;
    }

    // Apply government science multiplier
    GovernmentModifiers govMods = computeGovernmentModifiers(world, player);
    totalScience *= govMods.scienceMultiplier;

    // Apply civilization science multiplier
    const aoc::ecs::ComponentPool<PlayerCivilizationComponent>* civPool =
        world.getPool<PlayerCivilizationComponent>();
    if (civPool != nullptr) {
        for (uint32_t ci = 0; ci < civPool->size(); ++ci) {
            const PlayerCivilizationComponent& civ = civPool->data()[ci];
            if (civ.owner == player) {
                totalScience *= civDef(civ.civId).modifiers.scienceMultiplier;
                break;
            }
        }
    }

    return totalScience;
}

float computePlayerCulture(const aoc::ecs::World& world,
                            const aoc::map::HexGrid& grid,
                            PlayerId player) {
    const aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return 0.0f;
    }

    float totalCulture = 0.0f;

    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        const CityComponent& city = cityPool->data()[i];
        if (city.owner != player) {
            continue;
        }

        // Culture from worked tiles
        for (const hex::AxialCoord& tile : city.workedTiles) {
            if (grid.isValid(tile)) {
                aoc::map::TileYield yield = grid.tileYield(grid.toIndex(tile));
                totalCulture += static_cast<float>(yield.culture);
            }
        }

        // Population base culture
        totalCulture += static_cast<float>(city.population) * 0.3f;
    }

    // Apply civilization culture multiplier
    const aoc::ecs::ComponentPool<PlayerCivilizationComponent>* civPool =
        world.getPool<PlayerCivilizationComponent>();
    if (civPool != nullptr) {
        for (uint32_t ci = 0; ci < civPool->size(); ++ci) {
            const PlayerCivilizationComponent& civ = civPool->data()[ci];
            if (civ.owner == player) {
                totalCulture *= civDef(civ.civId).modifiers.cultureMultiplier;
                break;
            }
        }
    }

    return totalCulture;
}

} // namespace aoc::sim
