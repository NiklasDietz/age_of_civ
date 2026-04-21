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
    // G4: tick post-reform penalties. Lockout blocks new borrowing; trust cap
    // holds fiatTrust at 0.3 so the civ cannot immediately rebuild reserve
    // currency status after hyperinflating its debt away.
    if (crisis.reformLockoutTurns > 0) {
        --crisis.reformLockoutTurns;
    }
    if (crisis.reformTrustCapTurns > 0) {
        --crisis.reformTrustCapTurns;
        state.fiatTrust = std::min(state.fiatTrust, 0.3f);
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
                    static_cast<float>(state.goldBarReserves) * BANK_RUN_DRAIN_RATE));
                state.goldBarReserves = std::max(0, state.goldBarReserves - drain);
                state.updateCoinTier();

                // If reserves hit zero: forced devaluation
                if (state.goldBarReserves <= 0
                    && state.system == MonetarySystemType::GoldStandard) {
                    // Paper currency loses 50% of value
                    adjustMoneySupply(state, -state.moneySupply / 2,
                                      "bankRunDevaluation");
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

    // 1. Hyperinflation (Fiat/Digital only)
    if ((state.system == MonetarySystemType::FiatMoney
         || state.system == MonetarySystemType::Digital)
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
             && state.goldBarReserves > 0
             && state.inflationRate > BANK_RUN_INFLATION_THRESHOLD) {
        float debtToGold = (state.goldBarReserves > 0)
            ? static_cast<float>(state.governmentDebt)
              / static_cast<float>(state.goldBarReserves)
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
    adjustMoneySupply(state, -state.moneySupply / 2, "currencyReform");
    // G4: halve governmentDebt too so the real debt burden is preserved across
    // redenomination. Without this pairing, deliberate hyperinflation is a
    // free 50% debt wipe and +EV whenever debt exceeds ~20% of GDP.
    state.governmentDebt /= 2;
    // Reset inflation
    state.inflationRate = 0.0f;
    // Reset consecutive inflation counter
    crisis.turnsHighInflation = 0;
    // G4: post-reform penalties. Borrow lockout matches default cooldown
    // convention (turns-remaining counter). Trust cap locks fiatTrust at 0.3
    // for the full duration.
    crisis.reformLockoutTurns  = 30;
    crisis.reformTrustCapTurns = 50;
    state.fiatTrust = std::min(state.fiatTrust, 0.3f);

    LOG_INFO("Player %u: currency reform executed. Money supply halved, debt halved, borrow lockout 30t, trust cap 50t.",
             static_cast<unsigned>(state.owner));
}

// ============================================================================
// Reserve-ratio stress on GoldStandard civs
// ============================================================================

// The gold standard enters with `moneySupply = 2 * strength`, so designed
// backing ratio at entry is 0.5. Stress kicks in only once printing drives the
// ratio meaningfully below that equilibrium. Collapse is reserved for genuine
// reserve exhaustion.
constexpr float RESERVE_STRESS_THRESHOLD   = 0.40f;  // below: stress starts
constexpr float RESERVE_RUN_THRESHOLD      = 0.25f;  // below: redemption drain
constexpr float RESERVE_COLLAPSE_THRESHOLD = 0.10f;  // below: forced suspension
constexpr float REDEMPTION_RUN_DRAIN       = 0.05f;  // 5%/turn foreign redemption
constexpr int32_t RESERVE_STRESS_SUSPEND_TURNS = 10; // long-stress suspends

void processReserveStress(MonetaryStateComponent& state) {
    if (state.system != MonetarySystemType::GoldStandard) {
        state.reserveStressTurns   = 0;
        state.redemptionRunActive  = false;
        state.suspensionPending    = false;
        return;
    }

    if (state.moneySupply <= 0) {
        return;
    }

    // Grace window after entering GoldStandard. Reserves take a few turns to
    // settle (trade, minting, war reparations) before redemption dynamics kick
    // in. Without this, the very first stress check can trigger a spurious
    // suspension.
    constexpr int32_t GRACE_TURNS = 5;
    if (state.turnsInCurrentSystem < GRACE_TURNS) {
        state.reserveStressTurns  = 0;
        state.redemptionRunActive = false;
        return;
    }

    // Historic gold-standard era was bimetallic: silver coins in circulation
    // counted toward backing alongside gold bars held at the central bank.
    const int32_t metalBacking = state.silverCoinReserves * SILVER_COIN_VALUE
                               + state.goldBarReserves   * GOLD_BAR_VALUE;
    const float ratio = static_cast<float>(metalBacking)
                      / static_cast<float>(state.moneySupply);

    // Mirror the true ratio so trade partners see current backing.
    state.goldBackingRatio = std::clamp(ratio, 0.0f, 1.0f);

    if (ratio >= RESERVE_STRESS_THRESHOLD) {
        state.reserveStressTurns  = 0;
        state.redemptionRunActive = false;
        return;
    }

    ++state.reserveStressTurns;

    if (ratio < RESERVE_RUN_THRESHOLD && state.goldBarReserves > 0) {
        // Redemption run: foreign holders redeem paper for gold.
        const int32_t drain = std::max(
            1, static_cast<int32_t>(
                   static_cast<float>(state.goldBarReserves) * REDEMPTION_RUN_DRAIN));
        state.goldBarReserves = std::max(0, state.goldBarReserves - drain);
        state.updateCoinTier();
        if (!state.redemptionRunActive) {
            state.redemptionRunActive = true;
            LOG_INFO("Player %u: REDEMPTION RUN! Reserve ratio %.2f, %d bars drained",
                     static_cast<unsigned>(state.owner),
                     static_cast<double>(ratio), drain);
        }
    }

    const bool collapsed   = ratio < RESERVE_COLLAPSE_THRESHOLD;
    const bool exhausted   = state.reserveStressTurns >= RESERVE_STRESS_SUSPEND_TURNS;
    if (collapsed || exhausted) {
        // Suspension of convertibility: forced transition to Fiat with a
        // trust penalty. Matches the historical Nixon-shock dynamic.
        LOG_INFO("Player %u: SUSPENSION OF CONVERTIBILITY. Ratio %.2f, stress %d turns. "
                 "Forced onto FiatMoney.",
                 static_cast<unsigned>(state.owner),
                 static_cast<double>(ratio), state.reserveStressTurns);
        state.transitionTo(MonetarySystemType::FiatMoney);
        state.reserveStressTurns  = 0;
        state.redemptionRunActive = false;
        state.suspensionPending   = false;
        // Start at below-baseline fiat trust: world just watched us default
        // on a gold pledge.
        state.fiatTrust = std::max(0.15f, state.fiatTrust - 0.20f);
    }
}

} // namespace aoc::sim
