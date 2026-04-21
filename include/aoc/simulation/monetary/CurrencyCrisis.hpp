#pragma once

/**
 * @file CurrencyCrisis.hpp
 * @brief Currency crisis detection and resolution mechanics.
 *
 * Three crisis types, each triggered by threshold breaches:
 *
 * 1. Bank Run (Gold Standard only):
 *    Trigger: debt > 1.5x gold reserves AND inflation > 8%
 *    Effect: gold reserves drain 20% per turn for 3 turns.
 *    If reserves hit zero: forced devaluation (paper loses 50% value,
 *    trust craters, forced transition back to commodity money or severe penalty).
 *
 * 2. Hyperinflation Spiral (Fiat only):
 *    Trigger: inflation > 25% for 3 consecutive turns.
 *    Effect: production -30%, science -20%, loyalty -30 in all cities for 5 turns.
 *    Resolution: forced currency reform (reset price level, wipe 50% of money supply).
 *
 * 3. Sovereign Default:
 *    Trigger: cannot pay debt interest (treasury < interest payment).
 *    Effect: no new loans for 10 turns, trade partners demand gold settlement,
 *    -3 amenities in all cities, all existing trade routes lose 30% efficiency.
 *
 * Crises are tracked per-player. Only one crisis can be active at a time
 * (the most severe one). Crises resolve after their duration expires,
 * but the underlying conditions may trigger a new crisis immediately.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

struct MonetaryStateComponent;

// ============================================================================
// Crisis types and state
// ============================================================================

enum class CrisisType : uint8_t {
    None             = 0,
    BankRun          = 1,   ///< Gold standard: reserves draining
    Hyperinflation   = 2,   ///< Fiat: runaway inflation spiral
    SovereignDefault = 3,   ///< Any system: cannot service debt

    Count
};

struct CurrencyCrisisComponent {
    PlayerId  owner = INVALID_PLAYER;
    CrisisType activeCrisis = CrisisType::None;
    int32_t   turnsRemaining = 0;        ///< Turns until crisis resolves
    int32_t   turnsHighInflation = 0;    ///< Consecutive turns with inflation > 25% (for hyperinflation trigger)
    bool      hasDefaulted = false;      ///< True if currently in default
    int32_t   defaultCooldown = 0;       ///< Turns remaining where no loans are available

    // -- Modifiers applied during active crisis --

    /// Production multiplier (1.0 = normal, applied to all cities).
    [[nodiscard]] float productionModifier() const {
        if (this->activeCrisis == CrisisType::Hyperinflation) {
            return 0.70f;  // -30%
        }
        return 1.0f;
    }

    /// Science multiplier.
    [[nodiscard]] float scienceModifier() const {
        if (this->activeCrisis == CrisisType::Hyperinflation) {
            return 0.80f;  // -20%
        }
        return 1.0f;
    }

    /// Amenity penalty (subtracted from city amenities).
    [[nodiscard]] int32_t amenityPenalty() const {
        if (this->activeCrisis == CrisisType::SovereignDefault) {
            return 3;
        }
        if (this->activeCrisis == CrisisType::Hyperinflation) {
            return 2;
        }
        return 0;
    }

    /// Loyalty penalty applied to all cities.
    [[nodiscard]] float loyaltyPenalty() const {
        if (this->activeCrisis == CrisisType::Hyperinflation) {
            return 30.0f;
        }
        if (this->activeCrisis == CrisisType::BankRun) {
            return 10.0f;
        }
        return 0.0f;
    }

    /// Trade efficiency multiplier (stacks with other modifiers).
    [[nodiscard]] float tradeEfficiencyModifier() const {
        if (this->activeCrisis == CrisisType::SovereignDefault) {
            return 0.70f;  // -30%
        }
        if (this->activeCrisis == CrisisType::BankRun) {
            return 0.80f;  // -20%
        }
        return 1.0f;
    }

    /// Whether loans are currently blocked.
    [[nodiscard]] bool areLoansBlocked() const {
        return this->defaultCooldown > 0;
    }
};

// ============================================================================
// Crisis detection and processing
// ============================================================================

/**
 * @brief Check for crisis triggers and process active crises.
 *
 * Called once per turn per player. Detects threshold breaches,
 * triggers new crises, processes ongoing crisis effects (e.g. gold drain),
 * and resolves expired crises.
 *
 * @param world  ECS world.
 * @param state  Player's monetary state (may be mutated during crisis).
 * @param crisis Player's crisis component (will be mutated).
 * @return true if a new crisis was triggered this turn.
 */
bool processCurrencyCrisis(aoc::game::GameState& gameState,
                           MonetaryStateComponent& state,
                           CurrencyCrisisComponent& crisis);

/**
 * @brief Force a currency reform to end hyperinflation.
 *
 * Resets the price level to 1.0, wipes 50% of money supply,
 * clears the crisis, but the economic damage lingers (production
 * penalty continues for 2 more turns).
 *
 * @param state  Player's monetary state.
 * @param crisis Player's crisis component.
 */
void executeCurrencyReform(MonetaryStateComponent& state,
                           CurrencyCrisisComponent& crisis);

/**
 * @brief Track reserve-ratio stress on GoldStandard civs and force the
 *        organic gold->fiat cascade when backing collapses.
 *
 * Mirrors the historical transition mechanic (UK 1931, Nixon shock 1971):
 *
 *   ratio = goldBarReserves * GOLD_BAR_VALUE / moneySupply
 *
 *   ratio >= 0.7 : healthy, counter resets.
 *   0.5 <= ratio < 0.7 : stressed. Counter accrues, `goldBackingRatio`
 *                        mirrors the true ratio so trade partners see the
 *                        strain.
 *   0.2 <= ratio < 0.5 : redemption run. 5% of gold reserves drain per turn
 *                        (foreign holders redeeming paper for gold).
 *   ratio < 0.2 OR stress >= 10 turns : suspension of convertibility.
 *                        Auto-transition to FiatMoney with a trust penalty.
 *
 * No-op for non-GoldStandard civs. Call once per player per turn.
 */
void processReserveStress(MonetaryStateComponent& state);

} // namespace aoc::sim
