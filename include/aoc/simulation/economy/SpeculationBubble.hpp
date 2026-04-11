#pragma once

/**
 * @file SpeculationBubble.hpp
 * @brief Asset speculation bubbles, euphoria, and dramatic crashes.
 *
 * Bubbles form when sustained economic growth creates overconfidence.
 * The bubble inflates asset values (gold bonus, production bonus) but
 * is fragile -- any shock (war, resource depletion, interest rate hike)
 * can pop it, causing a crash that cascades through connected economies.
 *
 * Lifecycle:
 *   1. Formation: 10+ turns of GDP growth > 3% triggers bubble risk
 *   2. Inflation: assets appreciate 5-15% per turn above fundamentals
 *   3. Euphoria: foreign investment floods in, growth accelerates
 *   4. Pop trigger: any negative shock (war, default, resource loss, rate hike)
 *   5. Crash: asset values drop 30-60%, investment flees, recession follows
 *   6. Recovery: 10-20 turns to return to pre-bubble levels
 *
 * Player strategy:
 *   - Ride the bubble for temporary wealth (risky)
 *   - Central bank can preemptively raise rates to deflate gradually
 *   - Sell investments before the pop (requires timing)
 *   - Short other players' bubbles (currency attack when they're vulnerable)
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }

namespace aoc::sim {

enum class BubblePhase : uint8_t {
    None,         ///< No bubble forming
    Formation,    ///< GDP growth streak building pressure
    Inflation,    ///< Bubble actively inflating (bonuses apply)
    Euphoria,     ///< Peak bubble -- maximum bonuses, maximum fragility
    Crash,        ///< Bubble has popped -- severe penalties
    Recovery,     ///< Post-crash slow recovery

    Count
};

struct PlayerBubbleComponent {
    PlayerId owner = INVALID_PLAYER;

    BubblePhase phase = BubblePhase::None;

    /// Consecutive turns of GDP growth above threshold.
    int32_t growthStreak = 0;

    /// How inflated above fundamentals (1.0 = no bubble, 2.0 = 100% overvalued).
    float bubbleMagnitude = 1.0f;

    /// Turns in current phase.
    int32_t phaseTimer = 0;

    /// Previous turn GDP for growth calculation.
    CurrencyAmount previousGDP = 0;

    /// GDP multiplier during bubble (bonus during inflation, penalty during crash).
    [[nodiscard]] float gdpModifier() const {
        switch (this->phase) {
            case BubblePhase::Inflation: return 1.0f + (this->bubbleMagnitude - 1.0f) * 0.5f;
            case BubblePhase::Euphoria:  return 1.0f + (this->bubbleMagnitude - 1.0f) * 0.8f;
            case BubblePhase::Crash:     return 0.70f;
            case BubblePhase::Recovery:  return 0.85f + static_cast<float>(this->phaseTimer) * 0.01f;
            default:                     return 1.0f;
        }
    }

    /// Gold income modifier (euphoria attracts money, crash repels it).
    [[nodiscard]] float goldModifier() const {
        switch (this->phase) {
            case BubblePhase::Inflation: return 1.10f;
            case BubblePhase::Euphoria:  return 1.25f;
            case BubblePhase::Crash:     return 0.60f;
            case BubblePhase::Recovery:  return 0.80f;
            default:                     return 1.0f;
        }
    }

    /// Happiness modifier (euphoria = happy, crash = very unhappy).
    [[nodiscard]] float happinessModifier() const {
        switch (this->phase) {
            case BubblePhase::Euphoria:  return 2.0f;   // +2 amenities
            case BubblePhase::Crash:     return -4.0f;  // -4 amenities
            case BubblePhase::Recovery:  return -1.0f;
            default:                     return 0.0f;
        }
    }
};

/**
 * @brief Process bubble dynamics for a player. Called once per turn.
 *
 * Checks GDP growth streak, manages phase transitions, and handles
 * natural bubble deflation from interest rate hikes.
 */
void processSpeculationBubble(PlayerBubbleComponent& bubble,
                               CurrencyAmount currentGDP,
                               float interestRate,
                               bool hasNegativeShock);

} // namespace aoc::sim
