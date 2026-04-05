#pragma once

/**
 * @file VictoryCondition.hpp
 * @brief Victory condition types, tracker component, and evaluation logic.
 *
 * Supports multiple victory paths: Science (research all techs), Domination
 * (control all original capitals), Culture (accumulate 2000 culture), and
 * Score (highest score at turn 500).
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::ecs {
class World;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

/// Types of victory that can be achieved.
enum class VictoryType : uint8_t {
    None,
    Science,
    Domination,
    Culture,
    Score,
    Religion,
};

/// Per-player victory progress tracker (ECS component).
struct VictoryTrackerComponent {
    PlayerId owner          = INVALID_PLAYER;
    int32_t  scienceProgress = 0;            ///< Count of completed techs.
    float    totalCultureAccumulated = 0.0f; ///< Lifetime culture earned.
    int32_t  score           = 0;            ///< Computed composite score.
    bool     hasLaunchedSpaceProgram = false; ///< Reserved for future use.
};

/// Result of a victory condition check.
struct VictoryResult {
    VictoryType type   = VictoryType::None;
    PlayerId    winner = INVALID_PLAYER;
};

/**
 * @brief Update all victory tracker scores based on current game state.
 *
 * Called once per turn to recompute scores, culture totals, and tech counts.
 *
 * @param world  ECS world containing all player/city/tech components.
 * @param grid   Hex grid for city counting.
 */
void updateVictoryTrackers(aoc::ecs::World& world, const aoc::map::HexGrid& grid);

/**
 * @brief Check all victory conditions for all players.
 *
 * Evaluates Science, Domination, Culture, and (if turn >= 500) Score victories.
 *
 * @param world      ECS world with all game state.
 * @param currentTurn  The current turn number.
 * @return VictoryResult with type != None if a winner is found.
 */
[[nodiscard]] VictoryResult checkVictoryConditions(const aoc::ecs::World& world,
                                                    TurnNumber currentTurn);

} // namespace aoc::sim
