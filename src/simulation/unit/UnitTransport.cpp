/**
 * @file UnitTransport.cpp
 * @brief Naval units carrying land units (see UnitTransport.hpp).
 */

#include "aoc/simulation/unit/UnitTransport.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

namespace aoc::sim {

int32_t transportCapacity(UnitTypeId typeId) {
    if (typeId.value >= UNIT_TYPE_COUNT) {
        return 0;
    }
    const UnitTypeDef& def = unitTypeDef(typeId);
    if (def.unitClass != UnitClass::Naval) {
        return 0;
    }
    // Matches the capacity UnitComponent has always assigned at creation, which
    // nothing ever read.
    switch (typeId.value) {
    case 6:
        return 1; // Galley
    case 7:
        return 2; // Caravel
    case 106:
        return 4; // Troop Ship: the Galley and Caravel were the only hulls, and
                  // both pre-gunpowder, so an industrial civ could carry nobody
    default:
        return 0; // Battleship and the rest are warships, not hulls for hire
    }
}

bool isTransport(const aoc::game::Unit& unit) {
    return transportCapacity(unit.typeId()) > 0;
}

std::vector<aoc::game::Unit*> unitsAboard(aoc::game::Player& owner,
                                          const aoc::game::Unit& transport) {
    std::vector<aoc::game::Unit*> aboard;
    for (const std::unique_ptr<aoc::game::Unit>& u : owner.units()) {
        if (u == nullptr || u.get() == &transport) {
            continue;
        }
        // A passenger is an embarked land unit on the transport's own tile.
        if (u->position() == transport.position() && u->state() == aoc::sim::UnitState::Embarked &&
            u->typeDef().unitClass != UnitClass::Naval) {
            aboard.push_back(u.get());
        }
    }
    return aboard;
}

int32_t freeTransportSlots(aoc::game::Player& owner, const aoc::game::Unit& transport) {
    const int32_t capacity = transportCapacity(transport.typeId());
    if (capacity <= 0) {
        return 0;
    }
    const int32_t used = static_cast<int32_t>(unitsAboard(owner, transport).size());
    return (capacity > used) ? capacity - used : 0;
}

namespace {

/// The unit of `player` standing on `at`, or null.
aoc::game::Unit* unitOf(aoc::game::GameState& gameState, PlayerId player, hex::AxialCoord at,
                        aoc::game::Player** ownerOut) {
    aoc::game::Player* owner = gameState.player(player);
    if (owner == nullptr) {
        return nullptr;
    }
    if (ownerOut != nullptr) {
        *ownerOut = owner;
    }
    return owner->unitAt(at);
}

} // namespace

ErrorCode requestLoadUnit(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                          PlayerId player, hex::AxialCoord unitAt, hex::AxialCoord transportAt) {
    aoc::game::Player* owner   = nullptr;
    aoc::game::Unit* transport = unitOf(gameState, player, transportAt, &owner);
    if (transport == nullptr || owner == nullptr || !isTransport(*transport)) {
        return ErrorCode::InvalidArgument;
    }
    if (!grid.isValid(unitAt) || !grid.isValid(transportAt)) {
        return ErrorCode::InvalidArgument;
    }
    if (grid.distance(unitAt, transportAt) > 1) {
        return ErrorCode::InvalidArgument; // must be alongside
    }

    // The passenger is whichever of this player's land units stands on unitAt
    // and is not the transport itself.
    aoc::game::Unit* passenger = nullptr;
    for (const std::unique_ptr<aoc::game::Unit>& u : owner->units()) {
        if (u == nullptr || u.get() == transport) {
            continue;
        }
        if (u->position() == unitAt && u->typeDef().unitClass != UnitClass::Naval) {
            passenger = u.get();
            break;
        }
    }
    if (passenger == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    if (passenger->state() == aoc::sim::UnitState::Embarked &&
        passenger->position() == transportAt) {
        return ErrorCode::InvalidState; // already aboard this hull
    }
    if (freeTransportSlots(*owner, *transport) <= 0) {
        return ErrorCode::InvalidState; // full
    }
    if (passenger->movementRemaining() <= 0) {
        return ErrorCode::InvalidUnitAction;
    }

    passenger->setPosition(transportAt);
    passenger->setState(aoc::sim::UnitState::Embarked);
    passenger->setMovementRemaining(0); // boarding takes the turn
    LOG_INFO("Player %u loaded a unit aboard a transport at (%d,%d)", static_cast<unsigned>(player),
             transportAt.q, transportAt.r);
    return ErrorCode::Ok;
}

ErrorCode requestUnloadUnit(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                            PlayerId player, hex::AxialCoord transportAt, hex::AxialCoord landAt) {
    aoc::game::Player* owner   = nullptr;
    aoc::game::Unit* transport = unitOf(gameState, player, transportAt, &owner);
    if (transport == nullptr || owner == nullptr || !isTransport(*transport)) {
        return ErrorCode::InvalidArgument;
    }
    if (!grid.isValid(landAt) || grid.distance(transportAt, landAt) != 1) {
        return ErrorCode::InvalidArgument; // ashore means alongside
    }
    const int32_t landIdx = grid.toIndex(landAt);
    if (aoc::map::isWater(grid.terrain(landIdx)) || aoc::map::isImpassable(grid.terrain(landIdx))) {
        return ErrorCode::InvalidArgument; // nowhere to stand
    }

    std::vector<aoc::game::Unit*> aboard = unitsAboard(*owner, *transport);
    if (aboard.empty()) {
        return ErrorCode::InvalidState;
    }
    // The tile must be free of anyone else's unit as well as our own.
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        if (p != nullptr && p->unitAt(landAt) != nullptr) {
            return ErrorCode::InvalidState;
        }
    }

    aoc::game::Unit* passenger = aboard.front();
    passenger->setPosition(landAt);
    passenger->setState(aoc::sim::UnitState::Idle);
    passenger->setMovementRemaining(0); // landing takes the turn
    LOG_INFO("Player %u put a unit ashore at (%d,%d)", static_cast<unsigned>(player), landAt.q,
             landAt.r);
    return ErrorCode::Ok;
}

void carryPassengers(aoc::game::Player& owner, const aoc::game::Unit& transport,
                     hex::AxialCoord from, hex::AxialCoord to) {
    if (from == to || transportCapacity(transport.typeId()) <= 0) {
        return;
    }
    // Passengers are found at the tile the transport has just left.
    for (const std::unique_ptr<aoc::game::Unit>& u : owner.units()) {
        if (u == nullptr || u.get() == &transport) {
            continue;
        }
        if (u->position() == from && u->state() == aoc::sim::UnitState::Embarked &&
            u->typeDef().unitClass != UnitClass::Naval) {
            u->setPosition(to);
        }
    }
}

void drownPassengers(aoc::game::Player& owner, const aoc::game::Unit& transport) {
    if (transportCapacity(transport.typeId()) <= 0) {
        return;
    }
    std::vector<aoc::game::Unit*> aboard = unitsAboard(owner, transport);
    for (aoc::game::Unit* passenger : aboard) {
        LOG_INFO("A passenger went down with its transport at (%d,%d)", transport.position().q,
                 transport.position().r);
        owner.removeUnit(passenger);
    }
}

} // namespace aoc::sim
