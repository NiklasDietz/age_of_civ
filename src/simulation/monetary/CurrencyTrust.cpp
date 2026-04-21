/**
 * @file CurrencyTrust.cpp
 * @brief Currency trust scoring and reserve currency mechanics.
 */

#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/ForexMarket.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

// ============================================================================
// Trust score computation
// ============================================================================

void computeCurrencyTrust(const aoc::game::GameState& gameState,
                          const MonetaryStateComponent& state,
                          CurrencyTrustComponent& trust,
                          int32_t playerCount) {
    if (state.system != MonetarySystemType::FiatMoney
        && state.system != MonetarySystemType::Digital) {
        return;
    }

    ++trust.turnsOnFiat;

    // Track inflation stability
    if (std::abs(state.inflationRate) < 0.05f) {
        ++trust.turnsStable;
    } else {
        trust.turnsStable = 0;
    }

    // ================================================================
    // Factor 1: Inflation discipline (0.0 to 0.25)
    // Low inflation = high trust. Hyperinflation = trust collapse.
    // ================================================================
    float inflationFactor = 0.0f;
    float absInflation = std::abs(state.inflationRate);
    if (absInflation < 0.03f) {
        inflationFactor = 0.25f;  // Excellent monetary discipline
    } else if (absInflation < 0.05f) {
        inflationFactor = 0.20f;  // Acceptable
    } else if (absInflation < 0.10f) {
        inflationFactor = 0.10f;  // Concerning
    } else if (absInflation < 0.20f) {
        inflationFactor = 0.0f;   // Losing trust
    } else {
        inflationFactor = -0.15f; // Hyperinflation destroys trust
    }

    // ================================================================
    // Factor 2: GDP rank (0.0 to 0.25)
    // Larger economies are harder to ignore.
    // ================================================================
    float gdpFactor = 0.0f;
    if (playerCount > 0) {
        // Count how many players have higher GDP
        int32_t gdpRank = 1;
        for (const std::unique_ptr<aoc::game::Player>& otherPtr : gameState.players()) {
            if (otherPtr == nullptr) {
                continue;
            }
            const MonetaryStateComponent& other = otherPtr->monetary();
            if (other.owner != state.owner && other.gdp > state.gdp) {
                ++gdpRank;
            }
        }

        // Rank 1 = 0.25, Rank 2 = 0.20, Rank 3 = 0.10, else = 0.05
        if (gdpRank == 1) {
            gdpFactor = 0.25f;
        } else if (gdpRank == 2) {
            gdpFactor = 0.20f;
        } else if (gdpRank == 3) {
            gdpFactor = 0.10f;
        } else {
            gdpFactor = 0.05f;
        }
    }

    // ================================================================
    // Factor 3: Debt-to-GDP ratio (0.0 to 0.20)
    // Low debt = responsible government.
    // ================================================================
    float debtFactor = 0.0f;
    if (state.gdp > 0) {
        float debtToGDP = static_cast<float>(state.governmentDebt)
                        / static_cast<float>(state.gdp);
        if (debtToGDP < 0.50f) {
            debtFactor = 0.20f;
        } else if (debtToGDP < 1.0f) {
            debtFactor = 0.10f;
        } else if (debtToGDP < 2.0f) {
            debtFactor = 0.0f;
        } else {
            debtFactor = -0.10f;  // Debt crisis territory
        }
    }

    // ================================================================
    // Factor 4: Track record / time on fiat (0.0 to 0.15)
    // Longer time with stable fiat = proven system.
    // ================================================================
    float trackRecordFactor = 0.0f;
    if (trust.turnsStable >= 20) {
        trackRecordFactor = 0.15f;
    } else if (trust.turnsStable >= 10) {
        trackRecordFactor = 0.10f;
    } else if (trust.turnsStable >= 5) {
        trackRecordFactor = 0.05f;
    }

    // ================================================================
    // Factor 5: Reserve currency bonus (0.0 to 0.10)
    // Holding reserve status makes it self-reinforcing.
    // ================================================================
    float reserveBonus = 0.0f;
    if (trust.isReserveCurrency) {
        reserveBonus = 0.10f;
        ++trust.turnsAsReserve;
    }

    // ================================================================
    // Combine factors. Trust adjusts slowly toward the target (20% per turn).
    // This prevents instant trust swings from single-turn events.
    // ================================================================
    float targetTrust = inflationFactor + gdpFactor + debtFactor
                      + trackRecordFactor + reserveBonus;
    targetTrust = std::clamp(targetTrust, 0.0f, 1.0f);

    // Slow drift: 20% toward target each turn
    trust.trustScore = trust.trustScore * 0.80f + targetTrust * 0.20f;
    trust.trustScore = std::clamp(trust.trustScore, 0.0f, 1.0f);
}

// ============================================================================
// Reserve currency determination
// ============================================================================

void updateReserveCurrencyStatus(aoc::game::GameState& gameState) {
    // Find the current reserve holder and the best candidate
    PlayerId currentReserve = INVALID_PLAYER;
    PlayerId bestCandidate  = INVALID_PLAYER;
    float bestTrust         = 0.0f;

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) {
            continue;
        }
        const CurrencyTrustComponent& trust = playerPtr->currencyTrust();
        if (trust.isReserveCurrency) {
            currentReserve = playerPtr->id();
        }
        if (trust.trustScore > bestTrust) {
            bestTrust     = trust.trustScore;
            bestCandidate = playerPtr->id();
        }
    }

    // Hysteresis: current holder keeps status above 0.7, new candidate needs 0.8
    constexpr float ACQUIRE_THRESHOLD = 0.80f;
    constexpr float LOSE_THRESHOLD    = 0.70f;

    // Check if current holder should lose status
    if (currentReserve != INVALID_PLAYER) {
        aoc::game::Player* reservePlayer = gameState.player(currentReserve);
        if (reservePlayer != nullptr) {
            CurrencyTrustComponent& trust = reservePlayer->currencyTrust();
            if (trust.trustScore < LOSE_THRESHOLD) {
                trust.isReserveCurrency = false;
                trust.turnsAsReserve    = 0;
                currentReserve          = INVALID_PLAYER;
                LOG_INFO("Player %u lost reserve currency status (trust %.2f < %.2f)",
                         static_cast<unsigned>(reservePlayer->id()),
                         static_cast<double>(trust.trustScore),
                         static_cast<double>(LOSE_THRESHOLD));
            }
        }
    }

    // If no current holder, check if a new candidate qualifies
    if (currentReserve == INVALID_PLAYER && bestCandidate != INVALID_PLAYER
        && bestTrust >= ACQUIRE_THRESHOLD) {
        aoc::game::Player* newReserve = gameState.player(bestCandidate);
        if (newReserve != nullptr) {
            CurrencyTrustComponent& trust = newReserve->currencyTrust();
            trust.isReserveCurrency = true;
            LOG_INFO("Player %u gained reserve currency status (trust %.2f)",
                     static_cast<unsigned>(newReserve->id()),
                     static_cast<double>(trust.trustScore));
        }
    }
}

// ============================================================================
// Trade efficiency
// ============================================================================

float fiatTradeEfficiency(const CurrencyTrustComponent& trust) {
    // Base fiat efficiency is 1.0 (best possible), modified by trust.
    // Trust < 0.3: severe penalty (0.4 - 0.55 efficiency)
    // Trust 0.3-0.6: moderate (0.55 - 0.80)
    // Trust 0.6-0.8: near full (0.80 - 1.0)
    // Trust > 0.8: full + reserve bonus (1.0 - 1.05)
    float efficiency = 0.40f + trust.trustScore * 0.65f;

    if (trust.isReserveCurrency) {
        efficiency += 0.05f;  // Reserve currency premium
    }

    return std::clamp(efficiency, 0.40f, 1.05f);
}

float bilateralTradeEfficiency(const aoc::game::GameState& gameState,
                               PlayerId playerA, PlayerId playerB) {
    float efficiencyA = 0.50f;  // Default barter
    float efficiencyB = 0.50f;

    const aoc::game::Player* pa = gameState.player(playerA);
    const aoc::game::Player* pb = gameState.player(playerB);

    if (pa != nullptr) {
        const MonetaryStateComponent& msA = pa->monetary();
        efficiencyA = msA.tradeEfficiency();
        if (msA.system == MonetarySystemType::FiatMoney
            || msA.system == MonetarySystemType::Digital) {
            efficiencyA = fiatTradeEfficiency(pa->currencyTrust());
        }
    }

    if (pb != nullptr) {
        const MonetaryStateComponent& msB = pb->monetary();
        efficiencyB = msB.tradeEfficiency();
        if (msB.system == MonetarySystemType::FiatMoney
            || msB.system == MonetarySystemType::Digital) {
            efficiencyB = fiatTradeEfficiency(pb->currencyTrust());
        }
    }

    // Bilateral efficiency = minimum of both players' capabilities.
    // You can only trade as efficiently as the weakest monetary link.
    float baseEfficiency = std::min(efficiencyA, efficiencyB);

    // Exchange rate volatility penalty: if both are on fiat and their rates
    // differ significantly, trade is less efficient (exchange rate risk).
    if (pa != nullptr && pb != nullptr) {
        const CurrencyExchangeComponent& forexA = pa->currencyExchange();
        const CurrencyExchangeComponent& forexB = pb->currencyExchange();
        float xRate    = bilateralExchangeRate(forexA, forexB);
        float deviation = std::abs(xRate - 1.0f);
        if (deviation > 0.5f) {
            // More than 50% deviation: up to 10% trade efficiency penalty
            float penalty = std::min(0.10f, (deviation - 0.5f) * 0.10f);
            baseEfficiency *= (1.0f - penalty);
        }
    }

    return baseEfficiency;
}

} // namespace aoc::sim
