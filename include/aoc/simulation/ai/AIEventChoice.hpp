#pragma once

/**
 * @file AIEventChoice.hpp
 * @brief AI scoring of WorldEvent choices based on leader personality.
 */

#include "aoc/core/Types.hpp"
#include "aoc/simulation/event/WorldEvents.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }

namespace aoc::sim::ai {

/// Score an EventChoice for a given leader. Higher score = more attractive.
/// Weights gold/production/science/culture by matching focus genes; applies
/// a risk penalty on population loss proportional to (2 - riskTolerance).
[[nodiscard]] float scoreEventChoice(const aoc::game::GameState& gameState,
                                      PlayerId player,
                                      const aoc::sim::EventChoice& choice);

/// Pick the best choice index for an AI player from a pending event.
/// Returns 0 if no choices or no behavior is available.
[[nodiscard]] int32_t chooseEventChoice(const aoc::game::GameState& gameState,
                                         PlayerId player,
                                         const aoc::sim::WorldEventDef& eventDef);

/// Resolve any pending world events for AI-controlled players using utility
/// scoring. Called from the turn pipeline after checkWorldEvents.
void resolvePendingAIEvents(aoc::game::GameState& gameState);

} // namespace aoc::sim::ai
