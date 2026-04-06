#pragma once

/**
 * @file CurrencyWar.hpp
 * @brief Competitive devaluation and currency war mechanics.
 *
 * A fiat player can deliberately weaken their currency to make exports
 * cheaper (beggar-thy-neighbor policy). Trade partners can retaliate.
 *
 * Devalue action:
 *   - Increases money supply by 20%, making exports 15% cheaper for 5 turns.
 *   - Imports become 15% more expensive (hurts domestic consumption).
 *   - Other civs detect the devaluation immediately.
 *   - Retaliation: partners can devalue in response (tit-for-tat).
 *   - If 3+ civs devalue simultaneously: "race to the bottom" event.
 *     Global trade volume shrinks 20% for 10 turns.
 *
 * World Congress can pass a "Currency Stability Pact" resolution
 * to prohibit devaluation. Violators face automatic sanctions.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <cstdint>

namespace aoc::ecs { class World; }

namespace aoc::sim {

struct MonetaryStateComponent;

// ============================================================================
// Per-player devaluation state
// ============================================================================

struct CurrencyDevaluationComponent {
    PlayerId owner = INVALID_PLAYER;
    bool     isDevalued = false;         ///< Currently running a devaluation
    int32_t  devaluationTurnsLeft = 0;   ///< Turns remaining on current devaluation
    float    exportBonus = 0.0f;         ///< Export price reduction (0.15 = 15% cheaper)
    float    importPenalty = 0.0f;       ///< Import price increase (0.15 = 15% more expensive)
    int32_t  devaluationCount = 0;       ///< Total times devalued (escalating distrust)

    /// Export price multiplier (applied to goods this player sells).
    [[nodiscard]] float exportPriceMultiplier() const {
        return this->isDevalued ? (1.0f - this->exportBonus) : 1.0f;
    }

    /// Import price multiplier (applied to goods this player buys).
    [[nodiscard]] float importPriceMultiplier() const {
        return this->isDevalued ? (1.0f + this->importPenalty) : 1.0f;
    }
};

/// Global state for "race to the bottom" detection.
struct GlobalCurrencyWarState {
    bool     isRaceToBottom = false;     ///< 3+ civs devalued simultaneously
    int32_t  raceToBottomTurns = 0;      ///< Turns remaining on global trade shrinkage
    float    globalTradeMultiplier = 1.0f; ///< Applied to all trade routes (0.8 during race)
    bool     stabilityPactActive = false;  ///< World Congress pact prohibits devaluation
};

// ============================================================================
// Actions
// ============================================================================

/**
 * @brief Deliberately devalue currency (Fiat only).
 *
 * Increases money supply by 20%, grants export bonus for 5 turns.
 * Cannot devalue during an active devaluation or if stability pact is active.
 *
 * @param world  ECS world.
 * @param state  Player's monetary state.
 * @param deval  Player's devaluation component.
 * @param global Global currency war state.
 * @return Ok if successful.
 */
[[nodiscard]] ErrorCode devalueCurrency(aoc::ecs::World& world,
                                        MonetaryStateComponent& state,
                                        CurrencyDevaluationComponent& deval,
                                        const GlobalCurrencyWarState& global);

/**
 * @brief Per-turn processing of devaluation effects and race-to-bottom checks.
 *
 * Ticks down devaluation timers, checks if 3+ civs are devaluing
 * simultaneously (triggers race-to-bottom), and processes the global
 * trade shrinkage.
 *
 * @param world  ECS world.
 * @param global Global currency war state (will be mutated).
 */
void processCurrencyWar(aoc::ecs::World& world, GlobalCurrencyWarState& global);

} // namespace aoc::sim
