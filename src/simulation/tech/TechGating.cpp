/**
 * @file TechGating.cpp
 * @brief Implementation of tech gating helpers for production availability.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"

namespace aoc::sim {

// Forward decl — definition lower in file.
static uint8_t checkSpatial(const aoc::map::HexGrid* grid,
                             const aoc::game::City& city,
                             const aoc::sim::WonderAdjacencyReq& req);

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

    // Civic prerequisite (e.g. Spy after Cryptography civic).
    if (udef.requiredCivic.isValid()
     && !gsPlayer->civics().hasCompleted(udef.requiredCivic)) {
        return false;
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

    // Civic prereq.
    if (bdef.requiredCivic.isValid()
     && !gsPlayer->civics().hasCompleted(bdef.requiredCivic)) {
        return false;
    }

    // Generic spatial requirement (BuildingDef::spatial + extern lookup).
    // Only enforced when a grid is supplied; UI fallbacks remain permissive.
    if (grid != nullptr) {
        aoc::sim::WonderAdjacencyReq req = bdef.spatial;
        const aoc::sim::WonderAdjacencyReq extra = buildingSpatialReq(buildingId);
        if (extra.requiresMountain)      req.requiresMountain      = true;
        if (extra.requiresCoast)         req.requiresCoast         = true;
        if (extra.requiresRiver)         req.requiresRiver         = true;
        if (extra.requiresForest)        req.requiresForest        = true;
        if (extra.requiresJungle)        req.requiresJungle        = true;
        if (extra.requiresNaturalWonder) req.requiresNaturalWonder = true;
        if (extra.requiresDesert)        req.requiresDesert        = true;
        if (extra.requiresHill)          req.requiresHill          = true;
        if (extra.requiresFlat)          req.requiresFlat          = true;
        const uint8_t sp = checkSpatial(grid, city, req);
        if (sp != static_cast<uint8_t>(BuildLockReason::None)) { return false; }
    }
    return true;
}

// Shared spatial-requirement checker. Returns BuildLockReason or None.
static uint8_t checkSpatial(const aoc::map::HexGrid* grid,
                             const aoc::game::City& city,
                             const aoc::sim::WonderAdjacencyReq& req) {
    if (grid == nullptr) {
        return static_cast<uint8_t>(WonderLockReason::None);
    }
    const aoc::hex::AxialCoord center = city.location();
    if (!grid->isValid(center)) {
        return static_cast<uint8_t>(WonderLockReason::None);
    }
    const std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(center);
    const int32_t centerIdx = grid->toIndex(center);
    auto hasNeighbor = [&](auto pred) {
        for (const auto& n : nbrs) {
            if (!grid->isValid(n)) { continue; }
            if (pred(grid->toIndex(n))) { return true; }
        }
        return false;
    };
    if (req.requiresMountain
     && !hasNeighbor([&](int32_t i){ return grid->terrain(i) == aoc::map::TerrainType::Mountain; })) {
        return static_cast<uint8_t>(WonderLockReason::NeedMountain);
    }
    if (req.requiresCoast
     && !hasNeighbor([&](int32_t i){
            const auto t = grid->terrain(i);
            return t == aoc::map::TerrainType::Coast || t == aoc::map::TerrainType::Ocean;
        })) {
        return static_cast<uint8_t>(WonderLockReason::NeedCoast);
    }
    if (req.requiresRiver && grid->riverEdges(centerIdx) == 0u) {
        return static_cast<uint8_t>(WonderLockReason::NeedRiver);
    }
    if (req.requiresForest
     && !hasNeighbor([&](int32_t i){ return grid->feature(i) == aoc::map::FeatureType::Forest; })) {
        return static_cast<uint8_t>(WonderLockReason::NeedForest);
    }
    if (req.requiresJungle
     && !hasNeighbor([&](int32_t i){ return grid->feature(i) == aoc::map::FeatureType::Jungle; })) {
        return static_cast<uint8_t>(WonderLockReason::NeedJungle);
    }
    if (req.requiresNaturalWonder
     && !hasNeighbor([&](int32_t i){
            return grid->naturalWonder(i) != aoc::map::NaturalWonderType::None;
        })) {
        return static_cast<uint8_t>(WonderLockReason::NeedNaturalWonder);
    }
    if (req.requiresDesert) {
        const bool centerDesert = (grid->terrain(centerIdx) == aoc::map::TerrainType::Desert);
        const bool nearDesert = hasNeighbor([&](int32_t i){
            return grid->terrain(i) == aoc::map::TerrainType::Desert;
        });
        if (!centerDesert && !nearDesert) {
            return static_cast<uint8_t>(WonderLockReason::NeedDesert);
        }
    }
    if (req.requiresHill && grid->feature(centerIdx) != aoc::map::FeatureType::Hills) {
        return static_cast<uint8_t>(WonderLockReason::NeedHill);
    }
    if (req.requiresFlat
     && (grid->terrain(centerIdx) == aoc::map::TerrainType::Mountain
         || grid->feature(centerIdx) == aoc::map::FeatureType::Hills)) {
        return static_cast<uint8_t>(WonderLockReason::NeedFlat);
    }
    return static_cast<uint8_t>(WonderLockReason::None);
}

uint8_t wonderLockReason(const aoc::game::GameState& gameState,
                          PlayerId player,
                          const aoc::game::City& city,
                          uint8_t wonderId,
                          const aoc::map::HexGrid* grid) {
    if (wonderId >= WONDER_COUNT) {
        return static_cast<uint8_t>(WonderLockReason::AlreadyBuilt);
    }
    if (gameState.wonderTracker().isBuilt(wonderId)) {
        return static_cast<uint8_t>(WonderLockReason::AlreadyBuilt);
    }
    const aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return static_cast<uint8_t>(WonderLockReason::AlreadyBuilt);
    }
    const WonderDef& wdef = wonderDef(wonderId);

    if (wdef.prerequisiteTech.isValid()
     && !gsPlayer->tech().hasResearched(wdef.prerequisiteTech)) {
        return static_cast<uint8_t>(WonderLockReason::TechMissing);
    }
    if (wdef.prerequisiteCivic.isValid()
     && !gsPlayer->civics().hasCompleted(wdef.prerequisiteCivic)) {
        return static_cast<uint8_t>(WonderLockReason::CivicMissing);
    }
    // Already-owned-by-this-civ check (queued or built).
    for (const std::unique_ptr<aoc::game::City>& c : gsPlayer->cities()) {
        if (c == nullptr) { continue; }
        if (c->wonders().hasWonder(wonderId)) {
            return static_cast<uint8_t>(WonderLockReason::AlreadyOwned);
        }
        for (const ProductionQueueItem& it : c->production().queue) {
            if (it.type == ProductionItemType::Wonder && it.itemId == wonderId) {
                return static_cast<uint8_t>(WonderLockReason::AlreadyOwned);
            }
        }
    }

    // Adjacency / spatial requirements (shared helper).
    const uint8_t spatial = checkSpatial(grid, city, wdef.adjacency);
    if (spatial != static_cast<uint8_t>(WonderLockReason::None)) {
        return spatial;
    }

    return static_cast<uint8_t>(WonderLockReason::None);
}

uint8_t buildingLockReason(const aoc::game::GameState& gameState,
                            PlayerId player,
                            const aoc::game::City& city,
                            BuildingId buildingId,
                            const aoc::map::HexGrid* grid) {
    if (city.hasBuilding(buildingId)) {
        return static_cast<uint8_t>(BuildLockReason::AlreadyOwned);
    }
    const BuildingDef& bdef = buildingDef(buildingId);

    // District presence.
    if (bdef.requiredDistrict != DistrictType::CityCenter
     && !city.hasDistrict(bdef.requiredDistrict)) {
        return static_cast<uint8_t>(BuildLockReason::NeedDistrict);
    }

    const aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return static_cast<uint8_t>(BuildLockReason::TechMissing);
    }

    // Tech prereq via tech tree's unlockedBuildings list.
    {
        const PlayerTechComponent& playerTech = gsPlayer->tech();
        bool gated = false;
        bool researched = false;
        for (const TechDef& tech : allTechs()) {
            for (const BuildingId& bid : tech.unlockedBuildings) {
                if (bid == buildingId) {
                    gated = true;
                    if (playerTech.hasResearched(tech.id)) { researched = true; }
                    break;
                }
            }
            if (researched) { break; }
        }
        if (gated && !researched) {
            return static_cast<uint8_t>(BuildLockReason::TechMissing);
        }
    }
    // Civic prereq.
    if (bdef.requiredCivic.isValid()
     && !gsPlayer->civics().hasCompleted(bdef.requiredCivic)) {
        return static_cast<uint8_t>(BuildLockReason::CivicMissing);
    }
    // Spatial: union of per-def `spatial` field and external lookup
    // table buildingSpatialReq (for pre-existing constexpr entries that
    // didn't get designated-init updates).
    {
        aoc::sim::WonderAdjacencyReq req = bdef.spatial;
        const aoc::sim::WonderAdjacencyReq extra = buildingSpatialReq(buildingId);
        if (extra.requiresMountain)      req.requiresMountain      = true;
        if (extra.requiresCoast)         req.requiresCoast         = true;
        if (extra.requiresRiver)         req.requiresRiver         = true;
        if (extra.requiresForest)        req.requiresForest        = true;
        if (extra.requiresJungle)        req.requiresJungle        = true;
        if (extra.requiresNaturalWonder) req.requiresNaturalWonder = true;
        if (extra.requiresDesert)        req.requiresDesert        = true;
        if (extra.requiresHill)          req.requiresHill          = true;
        if (extra.requiresFlat)          req.requiresFlat          = true;
        const uint8_t sp = checkSpatial(grid, city, req);
        if (sp != static_cast<uint8_t>(BuildLockReason::None)) { return sp; }
    }
    return static_cast<uint8_t>(BuildLockReason::None);
}

uint8_t districtLockReason(const aoc::game::GameState& gameState,
                            PlayerId player,
                            const aoc::game::City& city,
                            uint8_t districtTypeIdx,
                            const aoc::map::HexGrid* grid) {
    (void)gameState;
    (void)player;
    if (districtTypeIdx >= DISTRICT_TYPE_COUNT) {
        return static_cast<uint8_t>(BuildLockReason::AlreadyOwned);
    }
    const DistrictType dtype = static_cast<DistrictType>(districtTypeIdx);
    if (city.hasDistrict(dtype)) {
        return static_cast<uint8_t>(BuildLockReason::AlreadyOwned);
    }
    return checkSpatial(grid, city, districtSpatialReq(dtype));
}

bool canBuildWonder(const aoc::game::GameState& gameState, PlayerId player, uint8_t wonderId) {
    if (wonderId >= WONDER_COUNT) {
        return false;
    }
    // Use detailed lock check — without grid we miss adjacency, but UI
    // listing path (via getBuildableItems) does pass grid. Production
    // enqueue path will need grid passed too (see getBuildableItems).
    const aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return false; }
    if (gsPlayer->cities().empty()) { return false; }
    return wonderLockReason(gameState, player, *gsPlayer->cities().front(),
                            wonderId, nullptr)
           == static_cast<uint8_t>(WonderLockReason::None);
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

    // Buildings: grey out when only spatial/civic prereq missing so player
    // sees future upgrade path. Hide if already-owned, no-district, or
    // tech-locked (those reflect deeper game-state and would be noisy).
    for (const BuildingDef& bdef : BUILDING_DEFS) {
        const uint8_t reason = buildingLockReason(gameState, player, city,
                                                   bdef.id, nullptr);
        if (reason == static_cast<uint8_t>(BuildLockReason::AlreadyOwned)
         || reason == static_cast<uint8_t>(BuildLockReason::NeedDistrict)
         || reason == static_cast<uint8_t>(BuildLockReason::TechMissing)) {
            continue;
        }
        BuildableItem item{};
        item.type   = ProductionItemType::Building;
        item.id     = bdef.id.value;
        item.name   = bdef.name;
        item.cost   = static_cast<float>(bdef.productionCost);
        item.locked = (reason != static_cast<uint8_t>(BuildLockReason::None));
        item.lockReason = reason;
        items.push_back(item);
    }

    // Wonders: list ALL with lock-status flag so UI can grey out.
    // Hide only wonders already-built (irreversibly unavailable) and
    // already-owned by this civ to avoid clutter.
    const std::array<WonderDef, WONDER_COUNT>& allWonders = allWonderDefs();
    for (const WonderDef& wonderDef : allWonders) {
        const uint8_t reason = wonderLockReason(gameState, player, city,
                                                 wonderDef.id, nullptr);
        if (reason == static_cast<uint8_t>(WonderLockReason::AlreadyBuilt)
         || reason == static_cast<uint8_t>(WonderLockReason::AlreadyOwned)) {
            continue;
        }
        BuildableItem witem{};
        witem.type   = ProductionItemType::Wonder;
        witem.id     = wonderDef.id;
        witem.name   = wonderDef.name;
        witem.cost   = static_cast<float>(wonderDef.productionCost);
        witem.locked = (reason != static_cast<uint8_t>(WonderLockReason::None));
        witem.lockReason = reason;
        items.push_back(witem);
    }

    // Districts -- show district types the city doesn't have yet, with
    // grey-out for spatial requirements (Harbor needs coast, etc).
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
        const uint8_t reason = districtLockReason(gameState, player, city,
                                                   d, nullptr);
        BuildableItem item{};
        item.type       = ProductionItemType::District;
        item.id         = d;
        item.name       = districtTypeName(dtype);
        item.cost       = 60.0f;  // Base district cost
        item.locked     = (reason != static_cast<uint8_t>(BuildLockReason::None));
        item.lockReason = reason;
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
