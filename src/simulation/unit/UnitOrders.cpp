/**
 * @file UnitOrders.cpp
 * @brief Pillage, repair, delete and alert (see UnitOrders.hpp).
 */

#include "aoc/simulation/unit/BuilderActions.hpp"
#include "aoc/simulation/unit/UnitOrders.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

#include <algorithm>

namespace aoc::sim {

namespace {

aoc::game::Unit* unitOf(aoc::game::GameState& gameState, PlayerId player, hex::AxialCoord at,
                        aoc::game::Player** ownerOut) {
    aoc::game::Player* owner = gameState.player(player);
    if (owner == nullptr) {
        return nullptr;
    }
    aoc::game::Unit* unit = owner->unitAt(at);
    if (unit != nullptr && ownerOut != nullptr) {
        *ownerOut = owner;
    }
    return unit;
}

} // namespace

int32_t pillageGold(uint16_t era) {
    return 25 + 10 * static_cast<int32_t>(era);
}

ErrorCode requestPillage(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, PlayerId player,
                         hex::AxialCoord at, const DiplomacyManager* diplomacy) {
    aoc::game::Player* owner = nullptr;
    aoc::game::Unit* unit    = unitOf(gameState, player, at, &owner);
    if (unit == nullptr || !grid.isValid(at)) {
        return ErrorCode::InvalidArgument;
    }
    if (!unit->isMilitary() || unit->movementRemaining() <= 0) {
        return ErrorCode::InvalidUnitAction;
    }
    const int32_t tileIndex  = grid.toIndex(at);
    const PlayerId tileOwner = grid.owner(tileIndex);
    if (tileOwner == player || tileOwner == INVALID_PLAYER) {
        return ErrorCode::InvalidArgument; // only another seat's improvements
    }
    const aoc::map::ImprovementType improvement = grid.improvement(tileIndex);
    if (improvement == aoc::map::ImprovementType::None ||
        improvement == aoc::map::ImprovementType::Road || grid.isPillaged(tileIndex)) {
        return ErrorCode::InvalidArgument;
    }
    if (diplomacy != nullptr && tileOwner < CITY_STATE_PLAYER_BASE &&
        !diplomacy->isAtWar(player, tileOwner)) {
        return ErrorCode::InvalidState; // at peace with the tile's owner
    }
    grid.setPillaged(tileIndex, true);
    unit->heal(PILLAGE_HEAL);
    const int32_t gold = pillageGold(effectiveEraFromTech(*owner).value);
    owner->addGold(gold, aoc::sim::MoneyFlow::unbacked());
    unit->setMovementRemaining(0);
    LOG_INFO("Player %u pillaged (%d,%d): +%d gold, healed %d", static_cast<unsigned>(player), at.q,
             at.r, gold, PILLAGE_HEAL);
    return ErrorCode::Ok;
}

ErrorCode requestRepair(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, PlayerId player,
                        hex::AxialCoord at) {
    aoc::game::Player* owner = gameState.player(player);
    if (owner == nullptr || !grid.isValid(at)) {
        return ErrorCode::InvalidArgument;
    }
    aoc::game::Unit* builder = nullptr;
    for (const std::unique_ptr<aoc::game::Unit>& unit : owner->units()) {
        if (unit->position() == at && unit->typeDef().unitClass == UnitClass::Civilian &&
            unit->hasCharges()) {
            builder = unit.get();
            break;
        }
    }
    const int32_t tileIndex = grid.toIndex(at);
    if (builder == nullptr || grid.owner(tileIndex) != player || !grid.isPillaged(tileIndex)) {
        return ErrorCode::InvalidArgument;
    }
    grid.setPillaged(tileIndex, false);
    spendChargeAndRetire(*owner, *builder);
    LOG_INFO("Player %u repaired (%d,%d)", static_cast<unsigned>(player), at.q, at.r);
    return ErrorCode::Ok;
}

ErrorCode requestDeleteUnit(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                            PlayerId player, hex::AxialCoord at) {
    aoc::game::Player* owner = nullptr;
    aoc::game::Unit* unit    = unitOf(gameState, player, at, &owner);
    if (unit == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    int32_t refund = 0;
    if (grid.isValid(at) && grid.owner(grid.toIndex(at)) == player) {
        refund = unit->typeDef().productionCost / 4;
        owner->addGold(refund, aoc::sim::MoneyFlow::unbacked());
    }
    LOG_INFO("Player %u disbanded %.*s at (%d,%d) (+%d gold)", static_cast<unsigned>(player),
             static_cast<int>(unit->typeDef().name.size()), unit->typeDef().name.data(), at.q, at.r,
             refund);
    owner->removeUnit(unit);
    return ErrorCode::Ok;
}

ErrorCode requestSetAlert(aoc::game::GameState& gameState, PlayerId player, hex::AxialCoord at,
                          bool alert) {
    aoc::game::Unit* unit = unitOf(gameState, player, at, nullptr);
    if (unit == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    if (!unit->isMilitary()) {
        return ErrorCode::InvalidUnitAction;
    }
    unit->alertStance = alert;
    if (alert && unit->state() != UnitState::Fortified) {
        unit->setState(UnitState::Sleeping);
    }
    return ErrorCode::Ok;
}

} // namespace aoc::sim
