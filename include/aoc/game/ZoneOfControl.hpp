#pragma once

/**
 * @file ZoneOfControl.hpp
 * @brief The one zone-of-control test shared by pathfinding and movement.
 *
 * Pathfinding.cpp and Movement.cpp each carried a private copy of this loop
 * until 2026-09-04; the earlier shared module under simulation/unit had no
 * callers and was deleted. This lives in the game layer because both callers
 * already depend on it and nothing lower does.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

namespace aoc::game {

class GameState;

/// True when a military unit of any player other than `movingPlayer` stands on
/// one of the six neighbours of `tile`. Diplomacy is not consulted: allies and
/// neutrals exert zone of control exactly like enemies (pre-existing rule).
[[nodiscard]] bool isInEnemyZoneOfControl(const GameState& gameState, aoc::hex::AxialCoord tile,
                                          PlayerId movingPlayer);

} // namespace aoc::game
