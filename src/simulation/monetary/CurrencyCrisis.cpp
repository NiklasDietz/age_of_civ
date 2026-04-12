/**
 * @file CurrencyCrisis.cpp
 * @brief Currency crisis detection, processing, and resolution.
 */

#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

// ============================================================================
// Crisis detection thresholds
// ============================================================================

/// Bank run triggers when debt exceeds this ratio of gold reserves.
constexpr float BANK_RUN_DEBT_RATIO = 1.5f;
/// Bank run also requires inflation above this level.
constexpr float BANK_RUN_INFLATION_THRESHOLD = 0.08f;
/// Gold drain rate during bank run (fraction of reserves lost per turn).
constexpr float BANK_RUN_DRAIN_RATE = 0.20f;
/// Bank run duration in turns.
constexpr int32_t BANK_RUN_DURATION = 3;

/// Hyperinflation triggers after this many consecutive turns above threshold.
constexpr int32_t HYPERINFLATION_CONSEC_TURNS = 3;
/// Inflation rate that counts toward hyperinflation trigger.
constexpr float HYPERINFLATION_THRESHOLD = 0.25f;
/// Hyperinflation crisis duration.
constexpr int32_t HYPERINFLATION_DURATION = 5;

/// Default cooldown: turns where no loans are available after default.
constexpr int32_t DEFAULT_COOLDOWN_TURNS = 10;
/// Default crisis duration (the acute phase with trade penalties).
constexpr int32_t DEFAULT_DURATION = 5;

// ============================================================================
// Processing
// ============================================================================

bool processCurrencyCrisis(aoc::game::GameState& /*gameState*/,
                           MonetaryStateComponent& state,
                           CurrencyCrisisComponent& crisis) {
    bool newCrisis = false;

    // Tick down default cooldown regardless of active crisis
    if (crisis.defaultCooldown > 0) {
        --crisis.defaultCooldown;
    }

    // ================================================================
    // Process active crisis effects
    // ================================================================
    if (crisis.activeCrisis != CrisisType::None) {
        --crisis.turnsRemaining;

        switch (crisis.activeCrisis) {
            case CrisisType::BankRun: {
                // Gold reserves drain each turn
                int32_t drain = std::max(1, static_cast<int32_t>(
                    static_cast<float>(state.goldCoinReserves) * BANK_RUN_DRAIN_RATE));
                state.goldCoinReserves = std::max(0, state.goldCoinReserves - drain);
                state.updateCoinTier();

                // If reserves hit zero: forced devaluation
                if (state.goldCoinReserves <= 0
                    && state.system == MonetarySystemType::GoldStandard) {
                    // Paper currency loses 50% of value
                    state.moneySupply /= 2;
                    state.goldBackingRatio = 0.0f;
                    state.priceLevel *= 2.0f;  // Prices double
                    LOG_INFO("Player %u: bank run depleted gold reserves, forced devaluation!",
                             static_cast<unsigned>(state.owner));
                }
                break;
            }
            case CrisisType::Hyperinflation: {
                // Inflation stays elevated during crisis (floor at 20%)
                state.inflationRate = std::max(state.inflationRate, 0.20f);
                break;
            }
            case CrisisType::SovereignDefault: {
                // No special per-turn effect beyond the modifiers
                break;
            }
            default:
                break;
        }

        // Check if crisis has resolved
        if (crisis.turnsRemaining <= 0) {
            CrisisType resolved = crisis.activeCrisis;
            crisis.activeCrisis = CrisisType::None;

            if (resolved == CrisisType::Hyperinflation) {
                // Auto-execute currency reform at end of hyperinflation
                executeCurrencyReform(state, crisis);
            }

            LOG_INFO("Player %u: %s crisis resolved",
                     static_cast<unsigned>(state.owner),
                     resolved == CrisisType::BankRun ? "bank run" :
                     resolved == CrisisType::Hyperinflation ? "hyperinflation" :
                     "sovereign default");
        }

        return false;  // Don't trigger new crises while one is active
    }

    // ================================================================
    // Track consecutive high-inflation turns
    // ================================================================
    if (state.inflationRate > HYPERINFLATION_THRESHOLD) {
        ++crisis.turnsHighInflation;
    } else {
        crisis.turnsHighInflation = 0;
    }

    // ================================================================
    // Check for new crisis triggers (highest severity first)
    // ================================================================

    // 1. Hyperinflation (Fiat only)
    if (state.system == MonetarySystemType::FiatMoney
        && crisis.turnsHighInflation >= HYPERINFLATION_CONSEC_TURNS) {
        crisis.activeCrisis = CrisisType::Hyperinflation;
        crisis.turnsRemaining = HYPERINFLATION_DURATION;
        crisis.turnsHighInflation = 0;
        newCrisis = true;
        LOG_INFO("Player %u: HYPERINFLATION CRISIS! Inflation >25%% for %d consecutive turns",
                 static_cast<unsigned>(state.owner), HYPERINFLATION_CONSEC_TURNS);
    }
    // 2. Bank Run (Gold Standard only)
    else if (state.system == MonetarySystemType::GoldStandard
             && state.goldCoinReserves > 0
             && state.inflationRate > BANK_RUN_INFLATION_THRESHOLD) {
        float debtToGold = (state.goldCoinReserves > 0)
            ? static_cast<float>(state.governmentDebt)
              / static_cast<float>(state.goldCoinReserves)
            : 999.0f;
        if (debtToGold > BANK_RUN_DEBT_RATIO) {
            crisis.activeCrisis = CrisisType::BankRun;
            crisis.turnsRemaining = BANK_RUN_DURATION;
            newCrisis = true;
            LOG_INFO("Player %u: BANK RUN! Debt/gold ratio %.1f, inflation %.0f%%",
                     static_cast<unsigned>(state.owner), debtToGold,
                     state.inflationRate * 100.0f);
        }
    }
    // 3. Sovereign Default (any system with debt)
    else if (state.governmentDebt > 0 && state.system != MonetarySystemType::Barter) {
        // Interest payment check: can the player afford it?
        CurrencyAmount interestDue = static_cast<CurrencyAmount>(
            static_cast<float>(state.governmentDebt) * state.interestRate);
        interestDue = std::max(static_cast<CurrencyAmount>(1), interestDue);

        if (state.treasury < interestDue && !crisis.hasDefaulted) {
            crisis.activeCrisis = CrisisType::SovereignDefault;
            crisis.turnsRemaining = DEFAULT_DURATION;
            crisis.hasDefaulted = true;
            crisis.defaultCooldown = DEFAULT_COOLDOWN_TURNS;
            newCrisis = true;
            LOG_INFO("Player %u: SOVEREIGN DEFAULT! Cannot pay interest %lld, treasury %lld",
                     static_cast<unsigned>(state.owner),
                     static_cast<long long>(interestDue),
                     static_cast<long long>(state.treasury));
        }
    }

    // Reset default flag once cooldown expires (allows re-default later)
    if (crisis.defaultCooldown <= 0) {
        crisis.hasDefaulted = false;
    }

    return newCrisis;
}

void executeCurrencyReform(MonetaryStateComponent& state,
                           CurrencyCrisisComponent& crisis) {
    // Reset price level to baseline
    state.priceLevel = 1.0f;
    // Wipe 50% of money supply (the "new currency" is worth 2x the old)
    state.moneySupply /= 2;
    // Reset inflation
    state.inflationRate = 0.0f;
    // Reset consecutive inflation counter
    crisis.turnsHighInflation = 0;

    LOG_INFO("Player %u: currency reform executed. Money supply halved, prices reset.",
             static_cast<unsigned>(state.owner));
}

} // namespace aoc::sim
