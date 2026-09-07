/**
 * @file BuilderActions.cpp
 * @brief Validated builder orders (see BuilderActions.hpp).
 */

#include "aoc/simulation/unit/BuilderActions.hpp"
#include "aoc/simulation/tech/EurekaBoost.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

#include <limits>

namespace aoc::sim {

namespace {

constexpr TechId CHOP_FOREST_TECH{0};   // Mining
constexpr TechId CHOP_JUNGLE_TECH{4};   // Bronze Working

bool isEngineerImprovement(aoc::map::ImprovementType type) {
    return type == aoc::map::ImprovementType::Road || type == aoc::map::ImprovementType::Railway
        || type == aoc::map::ImprovementType::Fort;
}

/// The civilian unit of `player` standing on `at` with a charge left, or null.
aoc::game::Unit* builderAt(aoc::game::GameState& gameState, PlayerId player, hex::AxialCoord at,
                           aoc::game::Player** ownerOut) {
    aoc::game::Player* owner = gameState.player(player);
    if (owner == nullptr) {
        return nullptr;
    }
    for (const std::unique_ptr<aoc::game::Unit>& unit : owner->units()) {
        if (unit->position() == at && unit->typeDef().unitClass == UnitClass::Civilian
            && unit->hasCharges()) {
            if (ownerOut != nullptr) { *ownerOut = owner; }
            return unit.get();
        }
    }
    return nullptr;
}

/// The owner's nearest city within BUILDER_YIELD_RANGE of `at`, or null.
aoc::game::City* nearestCity(aoc::game::Player& owner, const aoc::map::HexGrid& grid,
                             hex::AxialCoord at) {
    aoc::game::City* best = nullptr;
    int32_t bestDist      = std::numeric_limits<int32_t>::max();
    for (const std::unique_ptr<aoc::game::City>& city : owner.cities()) {
        if (city == nullptr || city->owner() != owner.id()) { continue; }
        const int32_t d = grid.distance(city->location(), at);
        if (d <= BUILDER_YIELD_RANGE && d < bestDist) {
            bestDist = d;
            best     = city.get();
        }
    }
    return best;
}

} // namespace

bool isMilitaryEngineer(const aoc::game::Unit& unit) {
    return unit.typeId() == MILITARY_ENGINEER_ID;
}

int32_t builderYield(uint16_t era) {
    return 20 + 10 * static_cast<int32_t>(era);
}

std::vector<aoc::map::ImprovementType> placeableImprovements(const aoc::map::HexGrid& grid,
                                                             int32_t tileIndex,
                                                             const aoc::game::Unit& unit,
                                                             const PlayerTechComponent& tech) {
    std::vector<aoc::map::ImprovementType> out;
    const bool engineer = isMilitaryEngineer(unit);
    const aoc::map::ImprovementType existing = grid.improvement(tileIndex);
    for (const ImprovementDef& def : IMPROVEMENT_DEFS) {
        if (def.type == aoc::map::ImprovementType::None) { continue; }
        if (isEngineerImprovement(def.type) != engineer) { continue; }
        const bool replacesRoad = def.type == aoc::map::ImprovementType::Railway
                               && existing == aoc::map::ImprovementType::Road;
        if (existing != aoc::map::ImprovementType::None && !replacesRoad) { continue; }
        if (!canPlaceImprovement(grid, tileIndex, def.type, &tech)) { continue; }
        out.push_back(def.type);
    }
    return out;
}

ErrorCode requestPlaceImprovement(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                                  PlayerId player, hex::AxialCoord at,
                                  aoc::map::ImprovementType type) {
    aoc::game::Player* owner = nullptr;
    aoc::game::Unit* unit    = builderAt(gameState, player, at, &owner);
    if (unit == nullptr || !grid.isValid(at)) {
        return ErrorCode::InvalidArgument;
    }
    const int32_t tileIndex = grid.toIndex(at);
    const std::vector<aoc::map::ImprovementType> options =
        placeableImprovements(grid, tileIndex, *unit, owner->tech());
    bool allowed = false;
    for (aoc::map::ImprovementType option : options) {
        if (option == type) { allowed = true; }
    }
    if (!allowed) {
        return ErrorCode::InvalidUnitAction;
    }
    grid.setImprovement(tileIndex, type);
    // Cutting stone teaches you something about stone.
    if (type == aoc::map::ImprovementType::Quarry) {
        checkEurekaConditions(*owner, EurekaCondition::BuildQuarry);
    }
    unit->useCharge();
    LOG_INFO("Player %u placed improvement %u at (%d,%d)", static_cast<unsigned>(player),
             static_cast<unsigned>(type), at.q, at.r);
    return ErrorCode::Ok;
}

bool canChopAt(const aoc::map::HexGrid& grid, int32_t tileIndex) {
    const aoc::map::FeatureType feature = grid.feature(tileIndex);
    return feature == aoc::map::FeatureType::Forest || feature == aoc::map::FeatureType::Jungle
        || feature == aoc::map::FeatureType::Marsh;
}

bool canHarvestAt(const aoc::map::HexGrid& grid, int32_t tileIndex) {
    const ResourceId resource = grid.resource(tileIndex);
    if (!resource.isValid()) {
        return false;
    }
    return goodDef(resource.value).category == GoodCategory::RawBonus;
}

ErrorCode requestChop(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, PlayerId player,
                      hex::AxialCoord at) {
    aoc::game::Player* owner = nullptr;
    aoc::game::Unit* unit    = builderAt(gameState, player, at, &owner);
    if (unit == nullptr || isMilitaryEngineer(*unit) || !grid.isValid(at)) {
        return ErrorCode::InvalidArgument;
    }
    const int32_t tileIndex = grid.toIndex(at);
    if (!canChopAt(grid, tileIndex)) {
        return ErrorCode::InvalidArgument;
    }
    const aoc::map::FeatureType feature = grid.feature(tileIndex);
    if (feature == aoc::map::FeatureType::Forest && !owner->tech().hasResearched(CHOP_FOREST_TECH)) {
        return ErrorCode::TechPrerequisiteNotMet;
    }
    if (feature == aoc::map::FeatureType::Jungle && !owner->tech().hasResearched(CHOP_JUNGLE_TECH)) {
        return ErrorCode::TechPrerequisiteNotMet;
    }
    aoc::game::City* city = nearestCity(*owner, grid, at);
    if (city == nullptr || city->production().isEmpty()) {
        return ErrorCode::InvalidState;   // the yield needs a city that is building something
    }
    const int32_t yield = builderYield(effectiveEraFromTech(*owner).value);
    [[maybe_unused]] const bool completed = city->production().addProgress(static_cast<float>(yield));
    grid.setFeature(tileIndex, aoc::map::FeatureType::None);
    unit->useCharge();
    LOG_INFO("Player %u chopped (%d,%d): +%d production to %s", static_cast<unsigned>(player), at.q,
             at.r, yield, city->name().c_str());
    return ErrorCode::Ok;
}

ErrorCode requestHarvest(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, PlayerId player,
                         hex::AxialCoord at) {
    aoc::game::Player* owner = nullptr;
    aoc::game::Unit* unit    = builderAt(gameState, player, at, &owner);
    if (unit == nullptr || isMilitaryEngineer(*unit) || !grid.isValid(at)) {
        return ErrorCode::InvalidArgument;
    }
    const int32_t tileIndex = grid.toIndex(at);
    if (!canHarvestAt(grid, tileIndex)) {
        return ErrorCode::InvalidArgument;
    }
    aoc::game::City* city = nearestCity(*owner, grid, at);
    if (city == nullptr) {
        return ErrorCode::InvalidState;
    }
    const int32_t yield = builderYield(effectiveEraFromTech(*owner).value);
    city->setFoodSurplus(city->foodSurplus() + static_cast<float>(yield));
    grid.setResource(tileIndex, ResourceId{});
    unit->useCharge();
    LOG_INFO("Player %u harvested (%d,%d): +%d food to %s", static_cast<unsigned>(player), at.q,
             at.r, yield, city->name().c_str());
    return ErrorCode::Ok;
}

} // namespace aoc::sim
