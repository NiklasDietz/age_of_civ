#pragma once

/// @file Prestige.hpp
/// @brief Prestige victory: participation-based endgame scoring.
///
/// Rewards civilizations that act like functional nation-states — trading,
/// allying, maintaining peace, keeping cities content — rather than those
/// that rocket up one axis (science/culture/domination) at the expense of
/// the rest of the world. Points accumulate every turn from every major
/// subsystem. At game end (currentTurn >= maxTurns), highest prestige wins.
///
/// Design intent: because other civs generate prestige for you through
/// trade/alliance relationships, embargoing or attacking them costs you
/// prestige too. Incentivises staying integrated, not dominating.

#include "aoc/core/Types.hpp"

namespace aoc::map { class HexGrid; }
namespace aoc::game { class GameState; }

namespace aoc::sim {

class DiplomacyManager;

/// Per-player prestige accumulator. Category totals exist only for
/// diagnostics/UI; authoritative score is `total`.
struct PlayerPrestigeComponent {
    PlayerId owner = INVALID_PLAYER;

    float science    = 0.0f;
    float culture    = 0.0f;
    float faith      = 0.0f;
    float trade      = 0.0f;
    float diplomacy  = 0.0f;
    float military   = 0.0f;
    float governance = 0.0f;
    float total      = 0.0f;
};

/// Per-turn prestige accrual for all living major players. Each category
/// has a per-turn cap so no single axis dominates the final score. The
/// per-turn total is bounded, so the achievable maximum scales naturally
/// with game length (maxTurns * per-turn cap).
void processPrestige(aoc::game::GameState& gameState,
                      const aoc::map::HexGrid& grid,
                      const DiplomacyManager* diplomacy);

} // namespace aoc::sim
