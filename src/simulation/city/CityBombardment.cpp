/**
 * @file CityBombardment.cpp
 * @brief City ranged bombardment implementation.
 */

#include "aoc/simulation/city/CityBombardment.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ecs/World.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

void processCityBombardment(aoc::ecs::World& world, const aoc::map::HexGrid& grid,
                             PlayerId player, aoc::Random& rng) {
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    aoc::ecs::ComponentPool<UnitComponent>* unitPool =
        world.getPool<UnitComponent>();
    if (unitPool == nullptr) {
        return;
    }

    for (uint32_t ci = 0; ci < cityPool->size(); ++ci) {
        const CityComponent& city = cityPool->data()[ci];
        if (city.owner != player) {
            continue;
        }

        const EntityId cityEntity = cityPool->entities()[ci];

        // Check if the city has Walls (BuildingId 17)
        const CityDistrictsComponent* districts =
            world.tryGetComponent<CityDistrictsComponent>(cityEntity);
        if (districts == nullptr) {
            continue;
        }

        // Count wall-type buildings for strength scaling
        int32_t wallBuildingCount = 0;
        if (districts->hasBuilding(BuildingId{17})) {
            ++wallBuildingCount;
        }

        if (wallBuildingCount == 0) {
            continue;
        }

        // Bombardment strength: 15 + 5 per wall building
        const float bombardStrength = 15.0f + 5.0f * static_cast<float>(wallBuildingCount);

        // Find adjacent enemy military units
        const std::array<hex::AxialCoord, 6> neighbors = hex::neighbors(city.location);

        for (const hex::AxialCoord& nbr : neighbors) {
            if (!grid.isValid(nbr)) {
                continue;
            }

            // Find an enemy unit on this tile
            for (uint32_t ui = 0; ui < unitPool->size(); ++ui) {
                UnitComponent& targetUnit = unitPool->data()[ui];
                if (targetUnit.owner == player || targetUnit.owner == INVALID_PLAYER) {
                    continue;
                }
                if (targetUnit.position != nbr) {
                    continue;
                }

                const UnitTypeDef& targetDef = unitTypeDef(targetUnit.typeId);
                if (!isMilitary(targetDef.unitClass)) {
                    continue;
                }

                // Compute damage using ranged combat formula: city never takes retaliation
                const float defenseStrength = static_cast<float>(targetDef.combatStrength);
                float ratio = bombardStrength / std::max(defenseStrength, 0.01f);
                float randomFactor = 0.8f + rng.nextFloat() * 0.4f;
                float baseDamage = 30.0f * ratio * randomFactor;
                const int32_t damage = std::clamp(static_cast<int32_t>(baseDamage), 0, 100);

                targetUnit.hitPoints -= damage;

                LOG_INFO("City %s bombarded %.*s at (%d,%d) for %d damage (HP: %d)",
                         city.name.c_str(),
                         static_cast<int>(targetDef.name.size()), targetDef.name.data(),
                         nbr.q, nbr.r, damage, targetUnit.hitPoints);

                // Destroy the unit if HP <= 0
                if (targetUnit.hitPoints <= 0) {
                    const EntityId targetEntity = unitPool->entities()[ui];
                    world.destroyEntity(targetEntity);
                    LOG_INFO("City bombardment destroyed enemy unit");
                    break; // Only bombard one unit per neighbor tile
                }

                break; // Only one unit per tile
            }
        }
    }
}

} // namespace aoc::sim
