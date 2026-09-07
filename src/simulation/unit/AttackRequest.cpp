/**
 * @file AttackRequest.cpp
 * @brief Validated attack order shared by the right-click, the debug route and
 *        the MCP tool. See AttackRequest.hpp for the rules.
 */

#include "aoc/simulation/unit/AttackRequest.hpp"

#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/city/CitySiege.hpp"
#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/simulation/unit/CombatExtensions.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

#include <memory>

namespace aoc::sim {

aoc::game::Unit* enemyUnitAt(aoc::game::GameState& gameState, PlayerId viewer, hex::AxialCoord at) {
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        if (p == nullptr || p->id() == viewer) {
            continue;
        }
        aoc::game::Unit* unit = p->unitAt(at);
        if (unit != nullptr) {
            return unit;
        }
    }
    for (const std::unique_ptr<aoc::game::Player>& cityState : gameState.cityStatePlayers()) {
        if (cityState == nullptr || cityState->id() == viewer) {
            continue;
        }
        aoc::game::Unit* unit = cityState->unitAt(at);
        if (unit != nullptr) {
            return unit;
        }
    }
    if (aoc::game::Player* barbarians = gameState.barbarianPlayer();
        barbarians != nullptr && barbarians->id() != viewer) {
        if (aoc::game::Unit* unit = barbarians->unitAt(at); unit != nullptr) {
            return unit;
        }
    }
    return nullptr;
}

ErrorCode requestAttack(aoc::game::GameState& gameState, aoc::Random& rng, aoc::map::HexGrid& grid,
                        PlayerId player, hex::AxialCoord from, hex::AxialCoord to,
                        const DiplomacyManager* diplomacy) {
    aoc::game::Player* owner = gameState.player(player);
    if (owner == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    aoc::game::Unit* attacker = owner->unitAt(from);
    if (attacker == nullptr || !grid.isValid(to)) {
        return ErrorCode::InvalidArgument;
    }
    // Religious units argue rather than fight, but they do contest each other.
    // This guard used to reject them outright, which is why an Apostle's combat
    // strength sat in the unit table unusable and two faiths could walk through
    // one another.
    if (attacker->typeDef().unitClass == UnitClass::Religious) {
        return requestTheologicalCombat(gameState, rng, grid, player, from, to);
    }
    if (!attacker->isMilitary()) {
        return ErrorCode::InvalidUnitAction;
    }
    const UnitTypeDef& def = attacker->typeDef();
    const int32_t distance = grid.distance(from, to);

    if (isAirUnit(def.unitClass)) {
        // Aircraft use the air system: sorties, operational range, interception.
        const AirUnitComponent& air = attacker->airUnit();
        if (air.sortiesRemaining <= 0 || distance == 0 || distance > air.operationalRange) {
            return ErrorCode::InvalidUnitAction;
        }
        return executeBombingRun(gameState, grid, *attacker, to);
    }

    aoc::game::Unit* defender = enemyUnitAt(gameState, player, to);
    // An undefended enemy city is a target in its own right: ranged fire grinds
    // its walls and then its hit points, melee takes it once both are gone.
    aoc::game::City* targetCity =
        (defender == nullptr) ? enemyCityAt(gameState, player, to) : nullptr;
    if (defender == nullptr && targetCity == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    if (attacker->movementRemaining() <= 0) {
        return ErrorCode::InvalidUnitAction;
    }
    if (targetCity != nullptr) {
        const PlayerId cityOwner = targetCity->owner();
        if (diplomacy != nullptr && cityOwner < CITY_STATE_PLAYER_BASE
            && !diplomacy->isAtWar(player, cityOwner)) {
            return ErrorCode::InvalidState;
        }
        const bool cityRanged = def.rangedStrength > 0 && def.range > 0;
        if (distance > (cityRanged ? def.range : 1)) {
            return ErrorCode::InvalidUnitAction;
        }
        static_cast<void>(resolveAttackOnCity(gameState, rng, grid, *attacker, *targetCity,
                                              gameState.currentTurn()));
        aoc::game::Unit* afterCity = owner->unitAt(from);
        if (afterCity != nullptr) {
            afterCity->setMovementRemaining(0);
        }
        return ErrorCode::Ok;
    }
    // Civ VI: no attack on a major civ without a war. City-states and
    // barbarians need no declaration.
    const PlayerId defenderOwner = defender->owner();
    if (diplomacy != nullptr && defenderOwner < CITY_STATE_PLAYER_BASE
        && !diplomacy->isAtWar(player, defenderOwner)) {
        return ErrorCode::InvalidState;
    }
    // Civilians are captured, not killed: the unit changes hands and the
    // attacker steps onto its tile (melee reach only).
    if (!defender->isMilitary() && !isAirUnit(def.unitClass)) {
        if (distance != 1) {
            return ErrorCode::InvalidUnitAction;
        }
        aoc::game::Player* loser = gameState.player(defenderOwner);
        if (loser == nullptr) {
            return ErrorCode::InvalidArgument;
        }
        const UnitTypeId capturedType = defender->typeId();
        const int32_t charges         = defender->chargesRemaining();
        loser->removeUnit(defender);
        aoc::game::Unit& captured = owner->addUnit(capturedType, to);
        captured.setChargesRemaining(charges);
        attacker->setPosition(to);
        attacker->setMovementRemaining(0);
        LOG_INFO("Player %u captured a %.*s from player %u at (%d,%d)", static_cast<unsigned>(player),
                 static_cast<int>(captured.typeDef().name.size()), captured.typeDef().name.data(),
                 static_cast<unsigned>(defenderOwner), to.q, to.r);
        return ErrorCode::Ok;
    }
    if (def.rangedStrength > 0 && def.range > 0) {
        if (distance > def.range) {
            return ErrorCode::InvalidUnitAction;
        }
        static_cast<void>(resolveRangedCombat(gameState, rng, grid, *attacker, *defender));
    } else {
        if (distance != 1) {
            return ErrorCode::InvalidUnitAction;
        }
        if (defender->state() == UnitState::Embarked) {
            // Melee cannot reach an embarked unit (Combat.cpp returns an empty
            // result); until 2026-09-05 the attacker still lost its turn.
            return ErrorCode::InvalidUnitAction;
        }
        static_cast<void>(resolveMeleeCombat(gameState, rng, grid, *attacker, *defender));
    }
    // Either attack is the unit's action for the turn. Combat may have removed
    // the attacker (melee retaliation), so it is looked up again, never reused.
    aoc::game::Unit* survivor = owner->unitAt(from);
    if (survivor != nullptr) {
        survivor->setMovementRemaining(0);
    }
    return ErrorCode::Ok;
}

} // namespace aoc::sim
