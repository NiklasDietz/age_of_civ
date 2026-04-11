/**
 * @file TechGating.cpp
 * @brief Implementation of tech gating helpers for production availability.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/ecs/World.hpp"

namespace aoc::sim {

bool canBuildUnit(const aoc::game::GameState& gameState, PlayerId player, UnitTypeId unitType) {
    // Find the player's tech component
    const aoc::ecs::ComponentPool<PlayerTechComponent>* techPool =
        world.getPool<PlayerTechComponent>();
    const PlayerTechComponent* playerTech = nullptr;
    if (techPool != nullptr) {
        for (uint32_t i = 0; i < techPool->size(); ++i) {
            if (techPool->data()[i].owner == player) {
                playerTech = &techPool->data()[i];
                break;
            }
        }
    }

    // Check if any tech gates this unit type
    bool gatedByTech = false;
    bool techResearched = false;
    const std::vector<TechDef>& techs = allTechs();
    for (const TechDef& tech : techs) {
        for (const UnitTypeId& uid : tech.unlockedUnits) {
            if (uid == unitType) {
                gatedByTech = true;
                if (playerTech != nullptr && playerTech->hasResearched(tech.id)) {
                    techResearched = true;
                }
                break;
            }
        }
        if (techResearched) {
            break;
        }
    }

    // If gated by tech but not researched, cannot build
    if (gatedByTech && !techResearched) {
        return false;
    }

    // Also check the unit's own requiredTech field (new expanded unit roster)
    const UnitTypeDef& udef = unitTypeDef(unitType);
    if (udef.requiredTech.isValid()) {
        if (playerTech == nullptr || !playerTech->hasResearched(udef.requiredTech)) {
            return false;
        }
    }

    // Naval units require the player to have at least one city with a Harbor district
    if (isNaval(udef.unitClass)) {
        bool hasHarbor = false;
        const aoc::ecs::ComponentPool<CityDistrictsComponent>* distPool =
            world.getPool<CityDistrictsComponent>();
        const aoc::ecs::ComponentPool<CityComponent>* cityPool =
            world.getPool<CityComponent>();
        if (distPool != nullptr && cityPool != nullptr) {
            for (uint32_t ci = 0; ci < distPool->size(); ++ci) {
                EntityId cityEnt = distPool->entities()[ci];
                const CityComponent* city = world.tryGetComponent<CityComponent>(cityEnt);
                if (city != nullptr && city->owner == player
                    && distPool->data()[ci].hasDistrict(DistrictType::Harbor)) {
                    hasHarbor = true;
                    break;
                }
            }
        }
        if (!hasHarbor) {
            return false;
        }
    }

    return true;
}

bool canBuildBuilding(const aoc::game::GameState& gameState, PlayerId player,
                       EntityId cityEntity, BuildingId buildingId) {
    aoc::ecs::World& world = gameState.legacyWorld();
    // Check if the city already has this building
    const CityDistrictsComponent* districts =
        world.tryGetComponent<CityDistrictsComponent>(cityEntity);
    if (districts != nullptr && districts->hasBuilding(buildingId)) {
        return false;
    }

    // Check if the city has the required district (CityCenter buildings are always possible)
    const BuildingDef& bdef = buildingDef(buildingId);
    if (bdef.requiredDistrict != DistrictType::CityCenter) {
        if (districts == nullptr || !districts->hasDistrict(bdef.requiredDistrict)) {
            return false;
        }
    }

    // Check tech prerequisite
    const aoc::ecs::ComponentPool<PlayerTechComponent>* techPool =
        world.getPool<PlayerTechComponent>();
    const PlayerTechComponent* playerTech = nullptr;
    if (techPool != nullptr) {
        for (uint32_t i = 0; i < techPool->size(); ++i) {
            if (techPool->data()[i].owner == player) {
                playerTech = &techPool->data()[i];
                break;
            }
        }
    }

    bool gatedByTech = false;
    bool techResearched = false;
    const std::vector<TechDef>& techs = allTechs();
    for (const TechDef& tech : techs) {
        for (const BuildingId& bid : tech.unlockedBuildings) {
            if (bid == buildingId) {
                gatedByTech = true;
                if (playerTech != nullptr && playerTech->hasResearched(tech.id)) {
                    techResearched = true;
                }
                break;
            }
        }
        if (techResearched) {
            break;
        }
    }

    if (gatedByTech && !techResearched) {
        return false;
    }

    return true;
}

bool canBuildWonder(const aoc::game::GameState& gameState, PlayerId player, uint8_t wonderId) {
    if (wonderId >= WONDER_COUNT) {
        return false;
    }

    // Check if already built globally
    const aoc::ecs::ComponentPool<GlobalWonderTracker>* trackerPool =
        world.getPool<GlobalWonderTracker>();
    if (trackerPool != nullptr && trackerPool->size() > 0) {
        const GlobalWonderTracker& tracker = trackerPool->data()[0];
        if (tracker.isBuilt(wonderId)) {
            return false;
        }
    }

    // Check tech prerequisite
    const WonderDef& wdef = wonderDef(wonderId);
    if (wdef.prerequisiteTech.isValid()) {
        const aoc::ecs::ComponentPool<PlayerTechComponent>* techPool =
            world.getPool<PlayerTechComponent>();
        if (techPool != nullptr) {
            for (uint32_t i = 0; i < techPool->size(); ++i) {
                if (techPool->data()[i].owner == player) {
                    if (!techPool->data()[i].hasResearched(wdef.prerequisiteTech)) {
                        return false;
                    }
                    break;
                }
            }
        }
    }

    return true;
}

std::vector<BuildableItem> getBuildableItems(const aoc::game::GameState& gameState,
                                              PlayerId player, EntityId cityEntity) {
    aoc::ecs::World& world = gameState.legacyWorld();
    std::vector<BuildableItem> items;

    // Units
    for (const UnitTypeDef& unitDef : UNIT_TYPE_DEFS) {
        aoc::ecs::World& world = gameState.legacyWorld();
        if (!canBuildUnit(world, player, unitDef.id)) {
            continue;
        }

        // Skip faith-purchased units (Religious units with 0 production cost)
        if (unitDef.unitClass == UnitClass::Religious && unitDef.productionCost == 0) {
            continue;
        }

        // Skip units with 0 production cost (Great People, etc.)
        if (unitDef.productionCost <= 0) {
            continue;
        }

        // Special case: Settler requires pop > 1 in the producing city
        if (unitDef.unitClass == UnitClass::Settler) {
            const CityComponent* city = world.tryGetComponent<CityComponent>(cityEntity);
            if (city == nullptr || city->population <= 1) {
                continue;
            }
        }

        BuildableItem item{};
        item.type = ProductionItemType::Unit;
        item.id   = unitDef.id.value;
        item.name = unitDef.name;
        item.cost = static_cast<float>(unitDef.productionCost);
        items.push_back(item);
    }

    // Buildings
    for (const BuildingDef& bdef : BUILDING_DEFS) {
        if (!canBuildBuilding(world, player, cityEntity, bdef.id)) {
            continue;
        }

        BuildableItem item{};
        item.type = ProductionItemType::Building;
        item.id   = bdef.id.value;
        item.name = bdef.name;
        item.cost = static_cast<float>(bdef.productionCost);
        items.push_back(item);
    }

    // Wonders
    const std::array<WonderDef, WONDER_COUNT>& wonders = allWonderDefs();
    for (const WonderDef& wdef : wonders) {
        if (!canBuildWonder(world, player, wdef.id)) {
            continue;
        }

        BuildableItem item{};
        item.type = ProductionItemType::Wonder;
        item.id   = wdef.id;
        item.name = wdef.name;
        item.cost = static_cast<float>(wdef.productionCost);
        items.push_back(item);
    }

    // Districts -- show district types the city doesn't have yet
    const CityDistrictsComponent* districts =
        world.tryGetComponent<CityDistrictsComponent>(cityEntity);
    for (uint8_t d = 1; d < DISTRICT_TYPE_COUNT; ++d) {
        const DistrictType dtype = static_cast<DistrictType>(d);
        if (districts != nullptr && districts->hasDistrict(dtype)) {
            continue;
        }
        BuildableItem item{};
        item.type = ProductionItemType::District;
        item.id   = d;
        item.name = districtTypeName(dtype);
        item.cost = 60.0f;  // Base district cost
        items.push_back(item);
    }

    return items;
}

} // namespace aoc::sim
