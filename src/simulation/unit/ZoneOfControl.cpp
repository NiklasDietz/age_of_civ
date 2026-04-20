/**
 * @file ZoneOfControl.cpp
 * @brief Zone of Control implementation.
 */

#include "aoc/simulation/unit/ZoneOfControl.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexCoord.hpp"

namespace aoc::sim {

bool isInEnemyZoC(const aoc::game::GameState& gameState,
                   aoc::hex::AxialCoord targetTile,
                   PlayerId movingPlayer,
                   const DiplomacyManager& diplomacy) {
    const std::array<aoc::hex::AxialCoord, 6> neighbors = aoc::hex::neighbors(targetTile);

    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        if (player->id() == movingPlayer) { continue; }

        // Open borders: this player's units don't exert ZoC against us.
        // Guard: movingPlayer may be a city-state (CS units exist); CS are
        // not part of the diplomacy matrix, so skip the relation lookup.
        if (movingPlayer < aoc::sim::CITY_STATE_PLAYER_BASE
            && diplomacy.haveMet(movingPlayer, player->id())) {
            const PairwiseRelation& rel = diplomacy.relation(movingPlayer, player->id());
            if (rel.hasOpenBorders && !rel.isAtWar) { continue; }
        }

        for (const std::unique_ptr<aoc::game::Unit>& unit : player->units()) {
            if (!isMilitary(unit->typeDef().unitClass)) { continue; }

            for (const aoc::hex::AxialCoord& nbr : neighbors) {
                if (unit->position() == nbr) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool shouldConsumeMovementByZoC(const aoc::game::Unit& unit,
                                 aoc::hex::AxialCoord targetTile,
                                 const aoc::game::GameState& gameState,
                                 const DiplomacyManager& diplomacy) {
    // Civilians bypass ZoC
    const UnitTypeDef& def = unit.typeDef();
    if (!isMilitary(def.unitClass)) { return false; }

    // Embarked units bypass ZoC
    if (unit.state() == UnitState::Embarked) { return false; }

    return isInEnemyZoC(gameState, targetTile, unit.owner(), diplomacy);
}

} // namespace aoc::sim
