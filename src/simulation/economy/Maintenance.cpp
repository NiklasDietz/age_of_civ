/**
 * @file Maintenance.cpp
 * @brief Unit and building maintenance cost processing implementation.
 */

#include "aoc/simulation/economy/Maintenance.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <vector>

namespace aoc::sim {

void processUnitMaintenance(aoc::ecs::World& world, PlayerId player) {
    // Count cities
    int32_t cityCount = 0;
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].owner == player) {
                ++cityCount;
            }
        }
    }

    // Count units
    int32_t unitCount = 0;
    aoc::ecs::ComponentPool<UnitComponent>* unitPool =
        world.getPool<UnitComponent>();
    if (unitPool != nullptr) {
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            if (unitPool->data()[i].owner == player) {
                ++unitCount;
            }
        }
    }

    const int32_t freeUnits = cityCount;
    const int32_t extraUnits = (unitCount > freeUnits) ? (unitCount - freeUnits) : 0;
    const CurrencyAmount maintenanceCost = static_cast<CurrencyAmount>(extraUnits);

    if (maintenanceCost <= 0) {
        return;
    }

    // Deduct from treasury
    PlayerEconomyComponent* econ = nullptr;
    world.forEach<PlayerEconomyComponent>(
        [player, &econ](EntityId, PlayerEconomyComponent& ec) {
            if (ec.owner == player) {
                econ = &ec;
            }
        });

    if (econ == nullptr) {
        return;
    }

    econ->treasury -= maintenanceCost;
    LOG_INFO("Player %u unit maintenance: %d units, %d free, cost %lld gold (treasury: %lld)",
             static_cast<unsigned>(player), unitCount, freeUnits,
             static_cast<long long>(maintenanceCost),
             static_cast<long long>(econ->treasury));

    // Disband a random unit if treasury is critically low
    if (econ->treasury < -20 && unitPool != nullptr) {
        // Find a unit to disband (prefer non-settler, non-builder)
        EntityId disbandTarget = NULL_ENTITY;
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            if (unitPool->data()[i].owner != player) {
                continue;
            }
            const UnitTypeDef& def = unitTypeDef(unitPool->data()[i].typeId);
            if (def.unitClass == UnitClass::Settler) {
                continue;  // Never disband settlers
            }
            disbandTarget = unitPool->entities()[i];
            break;
        }
        if (disbandTarget.isValid()) {
            world.destroyEntity(disbandTarget);
            LOG_INFO("Player %u disbanded a unit due to low treasury",
                     static_cast<unsigned>(player));
        }
    }
}

void processBuildingMaintenance(aoc::ecs::World& world, PlayerId player) {
    int32_t totalMaintenance = 0;

    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    const aoc::ecs::ComponentPool<CityDistrictsComponent>* distPool =
        world.getPool<CityDistrictsComponent>();

    if (cityPool == nullptr || distPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        if (cityPool->data()[i].owner != player) {
            continue;
        }
        EntityId cityEntity = cityPool->entities()[i];

        const CityDistrictsComponent* districts =
            world.tryGetComponent<CityDistrictsComponent>(cityEntity);
        if (districts == nullptr) {
            continue;
        }

        for (const CityDistrictsComponent::PlacedDistrict& district : districts->districts) {
            for (BuildingId bid : district.buildings) {
                const BuildingDef& bdef = buildingDef(bid);
                totalMaintenance += bdef.maintenanceCost;
            }
        }
    }

    if (totalMaintenance <= 0) {
        return;
    }

    // Deduct from treasury
    world.forEach<PlayerEconomyComponent>(
        [player, totalMaintenance](EntityId, PlayerEconomyComponent& ec) {
            if (ec.owner == player) {
                ec.treasury -= static_cast<CurrencyAmount>(totalMaintenance);
                LOG_INFO("Player %u building maintenance: %d gold (treasury: %lld)",
                         static_cast<unsigned>(player), totalMaintenance,
                         static_cast<long long>(ec.treasury));
            }
        });
}

} // namespace aoc::sim
