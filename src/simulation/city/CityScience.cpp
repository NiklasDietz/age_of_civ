/**
 * @file CityScience.cpp
 * @brief Science and culture computation from cities.
 */

#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/empire/CommunicationSpeed.hpp"
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

        // 2. Population base science (1.0 per citizen for faster early game)
        cityScience += static_cast<float>(city.population) * 1.0f;

        // 3. Palace bonus (capital gets extra science)
        if (city.isOriginalCapital) {
            cityScience += 5.0f;
        }

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

        // Communication speed science penalty for distant cities
        const aoc::ecs::ComponentPool<PlayerCommunicationComponent>* commPool =
            world.getPool<PlayerCommunicationComponent>();
        if (commPool != nullptr) {
            for (uint32_t cp = 0; cp < commPool->size(); ++cp) {
                if (commPool->data()[cp].owner != player) { continue; }
                const PlayerCommunicationComponent& comm = commPool->data()[cp];
                for (int32_t cc = 0; cc < comm.cityCount; ++cc) {
                    if (comm.cities[cc].cityEntity == cityEntity) {
                        CityCommModifiers mods = computeCityCommModifiers(comm.cities[cc]);
                        cityScience *= mods.scienceMultiplier;
                        break;
                    }
                }
                break;
            }
        }

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
