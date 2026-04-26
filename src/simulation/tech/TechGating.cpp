/**
 * @file TechGating.cpp
 * @brief Implementation of tech gating helpers for production availability.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"

namespace aoc::sim {

bool canBuildUnit(const aoc::game::GameState& gameState, PlayerId player, UnitTypeId unitType) {
    const aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return false; }

    const PlayerTechComponent& playerTech = gsPlayer->tech();

    // Check if any tech gates this unit type
    bool gatedByTech = false;
    bool techResearched = false;
    const std::vector<TechDef>& techs = allTechs();
    for (const TechDef& tech : techs) {
        for (const UnitTypeId& uid : tech.unlockedUnits) {
            if (uid == unitType) {
                gatedByTech = true;
                if (playerTech.hasResearched(tech.id)) {
                    techResearched = true;
                }
                break;
            }
        }
        if (techResearched) { break; }
    }

    // If gated by tech but not researched, cannot build
    if (gatedByTech && !techResearched) {
        return false;
    }

    // Also check the unit's own requiredTech field
    const UnitTypeDef& udef = unitTypeDef(unitType);
    if (udef.requiredTech.isValid()) {
        if (!playerTech.hasResearched(udef.requiredTech)) {
            return false;
        }
    }

    // Naval units require the player to have at least one city with a Harbor district
    if (isNaval(udef.unitClass)) {
        bool hasHarbor = false;
        for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
            if (city->hasDistrict(DistrictType::Harbor)) {
                hasHarbor = true;
                break;
            }
        }
        if (!hasHarbor) {
            return false;
        }
    }

    return true;
}

bool canBuildBuilding(const aoc::game::GameState& gameState, PlayerId player,
                       const aoc::game::City& city, BuildingId buildingId,
                       const aoc::map::HexGrid* grid) {
    // Check if the city already has this building
    if (city.hasBuilding(buildingId)) {
        return false;
    }

    // Check if the city has the required district
    const BuildingDef& bdef = buildingDef(buildingId);
    if (bdef.requiredDistrict != DistrictType::CityCenter) {
        if (!city.hasDistrict(bdef.requiredDistrict)) {
            return false;
        }
    }

    // Settlement-stage gate.
    // Hamlet: nothing buildable yet (wait to grow into Village).
    // Village: only a short allowlist of bootstrapping buildings.
    //   15 = Granary, 17 = Ancient Walls, 36 = Shrine, 24 = Mint.
    // Town/City: no stage restriction.
    const aoc::game::CitySize stage = city.stage();
    if (stage == aoc::game::CitySize::Hamlet) {
        return false;
    }
    if (stage == aoc::game::CitySize::Village) {
        const uint16_t bid = buildingId.value;
        if (bid != 15 && bid != 17 && bid != 36 && bid != 24) {
            return false;
        }
    }

    // Check tech prerequisite
    const aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return false; }
    const PlayerTechComponent& playerTech = gsPlayer->tech();

    bool gatedByTech = false;
    bool techResearched = false;
    const std::vector<TechDef>& techs = allTechs();
    for (const TechDef& tech : techs) {
        for (const BuildingId& bid : tech.unlockedBuildings) {
            if (bid == buildingId) {
                gatedByTech = true;
                if (playerTech.hasResearched(tech.id)) {
                    techResearched = true;
                }
                break;
            }
        }
        if (techResearched) { break; }
    }

    if (gatedByTech && !techResearched) {
        return false;
    }

    // Spatial prerequisite: GeothermalPlant (BuildingId{34}) requires a
    // GeothermalVent improvement on at least one tile within the city's
    // workable radius. Without a grid we cannot verify this; UI callers that
    // omit the grid get a permissive answer, but actual production enqueue
    // paths pass the grid.
    if (grid != nullptr && buildingId.value == 34) {
        bool hasVent = false;
        std::vector<aoc::hex::AxialCoord> nearby;
        nearby.reserve(64);
        aoc::hex::spiral(city.location(), aoc::sim::CITY_WORK_RADIUS,
                         std::back_inserter(nearby));
        for (const aoc::hex::AxialCoord& t : nearby) {
            if (!grid->isValid(t)) { continue; }
            const int32_t ti = grid->toIndex(t);
            if (grid->improvement(ti) == aoc::map::ImprovementType::GeothermalVent) {
                hasVent = true;
                break;
            }
        }
        if (!hasVent) { return false; }
    }

    return true;
}

bool canBuildWonder(const aoc::game::GameState& gameState, PlayerId player, uint8_t wonderId) {
    if (wonderId >= WONDER_COUNT) {
        return false;
    }

    // Check if already built globally
    if (gameState.wonderTracker().isBuilt(wonderId)) {
        return false;
    }

    const aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return false; }

    // Check tech prerequisite
    const WonderDef& wdef = wonderDef(wonderId);
    if (wdef.prerequisiteTech.isValid()) {
        if (!gsPlayer->tech().hasResearched(wdef.prerequisiteTech)) {
            return false;
        }
    }

    // H4.10: prevent the same civ from owning the wonder or queueing it in
    // multiple cities at once. Without this, a rush-happy player could queue
    // Pyramids in 5 cities and waste the losing four's production.
    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
        if (city == nullptr) { continue; }
        if (city->wonders().hasWonder(wonderId)) {
            return false;
        }
        for (const ProductionQueueItem& it : city->production().queue) {
            if (it.type == ProductionItemType::Wonder
                && it.itemId == wonderId) {
                return false;
            }
        }
    }

    return true;
}

std::vector<BuildableItem> getBuildableItems(const aoc::game::GameState& gameState,
                                              PlayerId player,
                                              const aoc::game::City& city) {
    std::vector<BuildableItem> items;

    // Units
    for (const UnitTypeDef& unitDef : UNIT_TYPE_DEFS) {
        if (!canBuildUnit(gameState, player, unitDef.id)) {
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
            if (city.population() <= 1) {
                continue;
            }
        }

        BuildableItem item{};
        item.type = ProductionItemType::Unit;
        item.id   = unitDef.id.value;
        // Civ-6 style display: if civ has a unique unit replacing this base
        // unit, show the unique name (Legion, Samurai, Hwacha, etc).
        const aoc::game::Player* gsPlayer = gameState.player(player);
        const CivilizationDef& civSpec = (gsPlayer != nullptr)
            ? civDef(gsPlayer->civId()) : civDef(0);
        if (civSpec.uniqueUnit.baseUnit == unitDef.id
            && !civSpec.uniqueUnit.name.empty()) {
            item.name = civSpec.uniqueUnit.name;
        } else {
            item.name = unitDef.name;
        }
        item.cost = static_cast<float>(unitDef.productionCost);
        items.push_back(item);
    }

    // Buildings
    for (const BuildingDef& bdef : BUILDING_DEFS) {
        if (!canBuildBuilding(gameState, player, city, bdef.id)) {
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
    const std::array<WonderDef, WONDER_COUNT>& allWonders = allWonderDefs();
    for (const WonderDef& wonderDef : allWonders) {
        if (!canBuildWonder(gameState, player, wonderDef.id)) {
            continue;
        }

        BuildableItem witem{};
        witem.type = ProductionItemType::Wonder;
        witem.id   = wonderDef.id;
        witem.name = wonderDef.name;
        witem.cost = static_cast<float>(wonderDef.productionCost);
        items.push_back(witem);
    }

    // Districts -- show district types the city doesn't have yet.
    // Stage gate: only Town and City can place non-CityCenter districts.
    const bool allowDistricts = city.stage() == aoc::game::CitySize::Town
                             || city.stage() == aoc::game::CitySize::City;
    for (uint8_t d = 1; d < DISTRICT_TYPE_COUNT; ++d) {
        const DistrictType dtype = static_cast<DistrictType>(d);
        if (city.hasDistrict(dtype)) {
            continue;
        }
        if (!allowDistricts) {
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


// Legacy EntityId overloads -- find city through ECS, delegate to City& version
bool canBuildBuilding(const aoc::game::GameState& gameState, PlayerId player,
                       [[maybe_unused]] EntityId cityEntity, BuildingId buildingId) {
    const aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return false; }
    // Find city by iterating player's cities (EntityId is no longer meaningful without ECS)
    // For now, check all cities - the building check uses tech + district state
    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
        // Try each city - the first one works since canBuildBuilding only checks tech + district presence
        return canBuildBuilding(gameState, player, *city, buildingId);
    }
    return false;
}

std::vector<BuildableItem> getBuildableItems(const aoc::game::GameState& gameState,
                                              PlayerId player, EntityId /*cityEntity*/) {
    const aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr || gsPlayer->cities().empty()) { return {}; }
    // Use first city as fallback
    return getBuildableItems(gameState, player, *gsPlayer->cities().front());
}

} // namespace aoc::sim
