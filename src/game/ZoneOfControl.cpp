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

bool isInEnemyZoneOfControl(const GameState& gameState, aoc::hex::AxialCoord tile,
                            PlayerId movingPlayer) {
    const std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(tile);

    for (const std::unique_ptr<Player>& player : gameState.players()) {
        if (player->id() == movingPlayer) {
            continue;
        }
        for (const std::unique_ptr<Unit>& unit : player->units()) {
            if (!unit->isMilitary()) {
                continue;
            }
            for (const aoc::hex::AxialCoord& nbr : nbrs) {
                if (unit->position() == nbr) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace aoc::game
