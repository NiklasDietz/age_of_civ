/**
 * @file CityScience.cpp
 * @brief Science and culture computation from cities.
 */

#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/empire/CommunicationSpeed.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
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

        // 2. Population base science (0.5 per citizen -- buildings are the real driver)
        cityScience += static_cast<float>(city.population) * 0.5f;

        // 3. Palace bonus (capital gets extra science, matching Civ 6 Palace)
        if (city.isOriginalCapital) {
            cityScience += 3.0f;
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

    // Economic stability bonus: stable monetary systems enable scholarship
    const aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool != nullptr) {
        for (uint32_t mi = 0; mi < monetaryPool->size(); ++mi) {
            if (monetaryPool->data()[mi].owner == player) {
                totalScience *= economicStabilityMultiplier(monetaryPool->data()[mi]);
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

        // Population base culture (1.0 per citizen, includes implicit Monument)
        totalCulture += static_cast<float>(city.population) * 1.0f;

        // Palace bonus: capital generates extra culture (+2, matching Civ 6)
        if (city.isOriginalCapital) {
            totalCulture += 2.0f;
        }
    }

    // Apply civilization culture multiplier
    const aoc::ecs::ComponentPool<PlayerCivilizationComponent>* civPool2 =
        world.getPool<PlayerCivilizationComponent>();
    if (civPool2 != nullptr) {
        for (uint32_t ci = 0; ci < civPool2->size(); ++ci) {
            const PlayerCivilizationComponent& civ = civPool2->data()[ci];
            if (civ.owner == player) {
                totalCulture *= civDef(civ.civId).modifiers.cultureMultiplier;
                break;
            }
        }
    }

    // Economic stability bonus: stable economies enable cultural flourishing
    const aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool2 =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool2 != nullptr) {
        for (uint32_t mi = 0; mi < monetaryPool2->size(); ++mi) {
            if (monetaryPool2->data()[mi].owner == player) {
                totalCulture *= economicStabilityMultiplier(monetaryPool2->data()[mi]);
                break;
            }
        }
    }

    return totalCulture;
}

// ============================================================================
// GameState-native overloads (Phase 3 migration)
// ============================================================================

float computePlayerScience(const aoc::game::Player& player,
                            const aoc::map::HexGrid& grid) {
    float totalScience = 0.0f;

    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        float cityScience = 0.0f;

        // 1. Science from worked tiles
        for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
            if (grid.isValid(tile)) {
                aoc::map::TileYield yield = grid.tileYield(grid.toIndex(tile));
                cityScience += static_cast<float>(yield.science);
            }
        }

        // 2. Population base science (0.5 per citizen -- buildings are the real driver)
        cityScience += static_cast<float>(city->population()) * 0.5f;

        // 3. Palace bonus (capital gets extra science, matching Civ 6 Palace)
        if (city->isOriginalCapital()) {
            cityScience += 3.0f;
        }

        // 4. Building bonuses and multiplier
        float bestMultiplier = 1.0f;
        const CityDistrictsComponent& districts = city->districts();
        for (const CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
            for (BuildingId bid : district.buildings) {
                if (bid.value < BUILDING_DEFS.size()) {
                    const BuildingDef& bdef = buildingDef(bid);
                    cityScience += static_cast<float>(bdef.scienceBonus);
                    bestMultiplier = std::max(bestMultiplier, bdef.scienceMultiplier);
                }
            }
        }

        // 5. Apply multiplier
        cityScience *= bestMultiplier;

        totalScience += cityScience;
    }

    // Apply government science multiplier
    GovernmentModifiers govMods = computeGovernmentModifiers(player.government());
    totalScience *= govMods.scienceMultiplier;

    // Apply civilization science multiplier
    totalScience *= civDef(player.civId()).modifiers.scienceMultiplier;

    // Economic stability bonus
    totalScience *= economicStabilityMultiplier(player.monetary());

    return totalScience;
}

float computePlayerCulture(const aoc::game::Player& player,
                            const aoc::map::HexGrid& grid) {
    float totalCulture = 0.0f;

    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        // Culture from worked tiles
        for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
            if (grid.isValid(tile)) {
                aoc::map::TileYield yield = grid.tileYield(grid.toIndex(tile));
                totalCulture += static_cast<float>(yield.culture);
            }
        }

        // Population base culture (1.0 per citizen, includes implicit Monument)
        totalCulture += static_cast<float>(city->population()) * 1.0f;

        // Palace bonus: capital generates extra culture (+2, matching Civ 6)
        if (city->isOriginalCapital()) {
            totalCulture += 2.0f;
        }
    }

    // Apply civilization culture multiplier
    totalCulture *= civDef(player.civId()).modifiers.cultureMultiplier;

    // Economic stability bonus
    totalCulture *= economicStabilityMultiplier(player.monetary());

    return totalCulture;
}

} // namespace aoc::sim
