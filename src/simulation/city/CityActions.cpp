/**
 * @file CityActions.cpp
 * @brief Validated city actions for the human (see CityActions.hpp).
 */

#include "aoc/simulation/city/CityActions.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/city/CityGrowth.hpp"
#include "aoc/simulation/city/ProductionSystem.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

namespace {

aoc::game::City* ownedCity(aoc::game::GameState& gameState, PlayerId player,
                           hex::AxialCoord cityAt, aoc::game::Player** ownerOut) {
    aoc::game::Player* owner = gameState.player(player);
    if (owner == nullptr) {
        return nullptr;
    }
    aoc::game::City* city = owner->cityAt(cityAt);
    if (city == nullptr || city->owner() != player) {
        return nullptr;
    }
    if (ownerOut != nullptr) {
        *ownerOut = owner;
    }
    return city;
}

constexpr int32_t CITY_WORK_DISTANCE = 3;

} // namespace

ErrorCode requestPurchase(aoc::game::GameState& gameState, const aoc::map::HexGrid* grid,
                          PlayerId player, hex::AxialCoord cityAt, ProductionItemType type,
                          uint16_t itemId) {
    aoc::game::Player* owner = nullptr;
    aoc::game::City* city    = ownedCity(gameState, player, cityAt, &owner);
    if (city == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    if (type == ProductionItemType::Unit) {
        if (!canBuildUnit(gameState, player, UnitTypeId{itemId})) {
            return ErrorCode::TechPrerequisiteNotMet;
        }
    } else if (type == ProductionItemType::Building) {
        if (!canBuildBuilding(gameState, player, *city, BuildingId{itemId}, grid)) {
            return ErrorCode::TechPrerequisiteNotMet;
        }
    } else {
        return ErrorCode::InvalidArgument;   // districts and wonders are built, not bought
    }
    return purchaseInCity(gameState, *owner, *city, type, itemId);
}

ErrorCode requestFaithPurchase(aoc::game::GameState& gameState, PlayerId player,
                               hex::AxialCoord cityAt, UnitTypeId unitId) {
    aoc::game::Player* owner = nullptr;
    aoc::game::City* city    = ownedCity(gameState, player, cityAt, &owner);
    if (city == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    if (unitId.value != 19 && unitId.value != 20 && unitId.value != 21) {
        return ErrorCode::InvalidArgument;
    }
    if (owner->faith().foundedReligion == NO_RELIGION) {
        return ErrorCode::InvalidState;   // no religion to spread
    }
    const UnitTypeDef& def = unitTypeDef(unitId);
    const float cost = religiousUnitFaithCost(unitId);
    PlayerFaithComponent& faith = owner->faith();
    if (faith.faith < cost) {
        return ErrorCode::InsufficientResources;
    }
    faith.faith -= cost;
    owner->addUnit(unitId, city->location());
    LOG_INFO("Player %u bought %.*s in %s for %.0f faith", static_cast<unsigned>(player),
             static_cast<int>(def.name.size()), def.name.data(), city->name().c_str(),
             static_cast<double>(cost));
    return ErrorCode::Ok;
}

float religiousUnitFaithCost(UnitTypeId unitId) {
    float base = 0.0f;
    if (unitId.value == 19 || unitId.value == 21) { base = 100.0f; }
    if (unitId.value == 20) { base = 200.0f; }
    return base * GamePace::instance().costMultiplier;
}

ErrorCode requestFaithRush(aoc::game::GameState& gameState, PlayerId player,
                           hex::AxialCoord cityAt) {
    aoc::game::Player* owner = nullptr;
    aoc::game::City* city    = ownedCity(gameState, player, cityAt, &owner);
    if (city == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    return rushBuildingWithFaith(*owner, *city, gameState.currentTurn());
}

WorkerFocus workerFocusFor(CityFocus focus) {
    switch (focus) {
        case CityFocus::Growth:     return WorkerFocus::Food;
        case CityFocus::Production: return WorkerFocus::Production;
        case CityFocus::Military:   return WorkerFocus::Production;
        case CityFocus::Science:    return WorkerFocus::Science;
        case CityFocus::Gold:       return WorkerFocus::Gold;
        case CityFocus::Balanced:
        case CityFocus::Count:
        default:                    return WorkerFocus::Balanced;
    }
}

ErrorCode requestSetCityFocus(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                              PlayerId player, hex::AxialCoord cityAt, CityFocus focus) {
    aoc::game::Player* owner = nullptr;
    aoc::game::City* city    = ownedCity(gameState, player, cityAt, &owner);
    if (city == nullptr || focus >= CityFocus::Count) {
        return ErrorCode::InvalidArgument;
    }
    city->governor().focus = focus;
    city->autoAssignWorkers(grid, workerFocusFor(focus), owner);
    LOG_INFO("City %s focus set to %s", city->name().c_str(), cityFocusName(focus));
    return ErrorCode::Ok;
}

ErrorCode requestToggleTileLock(aoc::game::GameState& gameState, PlayerId player,
                                hex::AxialCoord cityAt, hex::AxialCoord tile) {
    aoc::game::City* city = ownedCity(gameState, player, cityAt, nullptr);
    if (city == nullptr || tile == city->location()) {
        return ErrorCode::InvalidArgument;
    }
    city->toggleTileLock(tile);
    if (city->isTileLocked(tile) && !city->isTileWorked(tile) && city->availableCitizens() > 0) {
        city->assignWorker(tile);
    }
    return ErrorCode::Ok;
}

ErrorCode requestToggleWorkedTile(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                                  PlayerId player, hex::AxialCoord cityAt, hex::AxialCoord tile) {
    aoc::game::City* city = ownedCity(gameState, player, cityAt, nullptr);
    if (city == nullptr || !grid.isValid(tile) || tile == city->location()) {
        return ErrorCode::InvalidArgument;
    }
    const int32_t tileIdx = grid.toIndex(tile);
    if (grid.owner(tileIdx) != player || grid.distance(city->location(), tile) > CITY_WORK_DISTANCE
        || grid.movementCost(tileIdx) == 0) {
        return ErrorCode::InvalidArgument;
    }
    if (city->isTileWorked(tile)) {
        city->removeWorker(tile);
        return ErrorCode::Ok;
    }
    if (city->availableCitizens() <= 0) {
        return ErrorCode::InvalidState;   // every citizen is already working
    }
    city->assignWorker(tile);
    return ErrorCode::Ok;
}

ErrorCode requestRemoveQueueItem(aoc::game::GameState& gameState, PlayerId player,
                                 hex::AxialCoord cityAt, int32_t index) {
    aoc::game::City* city = ownedCity(gameState, player, cityAt, nullptr);
    if (city == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    std::vector<ProductionQueueItem>& queue = city->production().queue;
    if (index < 0 || index >= static_cast<int32_t>(queue.size())) {
        return ErrorCode::InvalidArgument;
    }
    LOG_INFO("City %s dropped %s from its queue", city->name().c_str(),
             queue[static_cast<std::size_t>(index)].name.c_str());
    queue.erase(queue.begin() + index);
    return ErrorCode::Ok;
}

bool cityProjectAvailable(const aoc::game::City& city, CityProjectType project) {
    if (project >= CityProjectType::Count) {
        return false;
    }
    const CityProjectDef& def = CITY_PROJECT_DEFS[static_cast<std::size_t>(project)];
    return city.hasDistrict(def.requiredDistrict);
}

ErrorCode requestQueueProject(aoc::game::GameState& gameState, PlayerId player,
                              hex::AxialCoord cityAt, CityProjectType project) {
    aoc::game::City* city = ownedCity(gameState, player, cityAt, nullptr);
    if (city == nullptr || !cityProjectAvailable(*city, project)) {
        return ErrorCode::InvalidArgument;
    }
    const CityProjectDef& def = CITY_PROJECT_DEFS[static_cast<std::size_t>(project)];
    ProductionQueueItem item{};
    item.type      = ProductionItemType::Project;
    item.itemId    = static_cast<uint16_t>(project);
    item.name      = std::string(def.name);
    item.totalCost = static_cast<float>(def.productionCost) * GamePace::instance().costMultiplier;
    item.progress  = 0.0f;
    city->production().queue.push_back(std::move(item));
    return ErrorCode::Ok;
}

bool buildingUpgradeAvailable(const aoc::game::City& city, BuildingId building) {
    if (!city.hasBuilding(building)) {
        return false;
    }
    return city.buildingLevels().getLevel(building) < MAX_BUILDING_LEVEL;
}

ErrorCode requestUpgradeBuilding(aoc::game::GameState& gameState, PlayerId player,
                                 hex::AxialCoord cityAt, BuildingId building) {
    aoc::game::City* city = ownedCity(gameState, player, cityAt, nullptr);
    if (city == nullptr || !buildingUpgradeAvailable(*city, building)) {
        return ErrorCode::InvalidArgument;
    }
    // One upgrade of a given building in the queue at a time: two would spend
    // twice for one level, since the second finds it already raised.
    for (const ProductionQueueItem& queued : city->production().queue) {
        if (queued.type == ProductionItemType::BuildingUpgrade && queued.itemId == building.value) {
            return ErrorCode::InvalidState;
        }
    }
    const int32_t cost = city->buildingLevels().upgradeCost(building);
    if (cost <= 0) {
        return ErrorCode::InvalidArgument;
    }
    const int32_t nextLevel = city->buildingLevels().getLevel(building) + 1;
    ProductionQueueItem item{};
    item.type      = ProductionItemType::BuildingUpgrade;
    item.itemId    = building.value;
    item.name      = std::string(buildingDef(building).name) + " Lv" + std::to_string(nextLevel);
    item.totalCost = static_cast<float>(cost) * GamePace::instance().costMultiplier;
    item.progress  = 0.0f;
    city->production().queue.push_back(std::move(item));
    return ErrorCode::Ok;
}

std::string_view amenityTierName(float happiness) {
    if (happiness >= 3.0f)  { return "Ecstatic"; }
    if (happiness >= 1.0f)  { return "Happy"; }
    if (happiness >= 0.0f)  { return "Content"; }
    if (happiness >= -2.0f) { return "Displeased"; }
    if (happiness >= -4.0f) { return "Unhappy"; }
    return "Unrest";
}

std::string cityProductionSummary(const aoc::game::City& city, float productionPerTurn) {
    const ProductionQueueItem* head = city.production().currentItem();
    if (head == nullptr) {
        return "idle";
    }
    std::string text = head->name;
    const int32_t turns = turnsToComplete(city, productionPerTurn);
    if (turns > 0) {
        text += " " + std::to_string(turns) + "t";
    }
    return text;
}

std::string cityBannerSummary(const aoc::game::City& city, float productionPerTurn) {
    return "Pop " + std::to_string(city.population()) + " | "
         + cityProductionSummary(city, productionPerTurn);
}

float cityGrowthFraction(const aoc::game::City& city) {
    const float needed = foodForGrowth(city.population());
    if (needed <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(city.foodSurplus() / needed, 0.0f, 1.0f);
}

int32_t turnsToComplete(const aoc::game::City& city, float productionPerTurn) {
    const ProductionQueueItem* head = city.production().currentItem();
    if (head == nullptr || productionPerTurn <= 0.0f) {
        return -1;
    }
    const float remaining = std::max(0.0f, head->totalCost - head->progress);
    return std::max(1, static_cast<int32_t>(std::ceil(remaining / productionPerTurn)));
}

} // namespace aoc::sim
