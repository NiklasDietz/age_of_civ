/**
 * @file ZoneOfControl.cpp
 */

#include "aoc/game/ZoneOfControl.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"

#include <array>
#include <memory>

namespace aoc::game {

namespace {

/// True when a military unit of `player` stands on one of `nbrs`.
[[nodiscard]] bool exertsControl(const Player* player, PlayerId movingPlayer,
                                 const std::array<aoc::hex::AxialCoord, 6>& nbrs) {
    if (player == nullptr || player->id() == movingPlayer) {
        return false;
    }
    for (const std::unique_ptr<Unit>& unit : player->units()) {
        if (unit == nullptr || !unit->isMilitary()) {
            continue;
        }
        for (const aoc::hex::AxialCoord& nbr : nbrs) {
            if (unit->position() == nbr) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

bool isInEnemyZoneOfControl(const GameState& gameState, aoc::hex::AxialCoord tile,
                            PlayerId movingPlayer) {
    const std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(tile);

    for (const std::unique_ptr<Player>& player : gameState.players()) {
        if (exertsControl(player.get(), movingPlayer, nbrs)) {
            return true;
        }
    }

    // players() holds the major seats only, by contract -- city-states live in
    // cityStatePlayers() and the barbarians in barbarianPlayer(). Both were
    // therefore bound by everyone else's zone of control while projecting none
    // of their own: a warband or a city-state garrison could be walked straight
    // past. Nothing about zone of control is meant to be one-sided.
    for (const std::unique_ptr<Player>& cityState : gameState.cityStatePlayers()) {
        if (exertsControl(cityState.get(), movingPlayer, nbrs)) {
            return true;
        }
    }
    return exertsControl(gameState.barbarianPlayer(), movingPlayer, nbrs);
}

} // namespace aoc::game
