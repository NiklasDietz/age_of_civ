/**
 * @file CurrencyTrust.cpp
 * @brief Currency trust scoring and reserve currency mechanics.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/ForexMarket.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/ecs/World.hpp"
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
    aoc::ecs::World& world = gameState.legacyWorld();
    if (state.system != MonetarySystemType::FiatMoney) {
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
        const aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
            world.getPool<MonetaryStateComponent>();
        if (monetaryPool != nullptr) {
            for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
                const MonetaryStateComponent& other = monetaryPool->data()[i];
                if (other.owner != state.owner && other.gdp > state.gdp) {
                    ++gdpRank;
                }
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
    aoc::ecs::ComponentPool<CurrencyTrustComponent>* trustPool =
        world.getPool<CurrencyTrustComponent>();
    if (trustPool == nullptr) {
        return;
    }

    // Find the current reserve holder and the best candidate
    PlayerId currentReserve = INVALID_PLAYER;
    PlayerId bestCandidate = INVALID_PLAYER;
    float bestTrust = 0.0f;

    for (uint32_t i = 0; i < trustPool->size(); ++i) {
        CurrencyTrustComponent& trust = trustPool->data()[i];
        if (trust.isReserveCurrency) {
            currentReserve = trust.owner;
        }
        if (trust.trustScore > bestTrust) {
            bestTrust = trust.trustScore;
            bestCandidate = trust.owner;
        }
    }

    // Hysteresis: current holder keeps status above 0.7, new candidate needs 0.8
    constexpr float ACQUIRE_THRESHOLD = 0.80f;
    constexpr float LOSE_THRESHOLD = 0.70f;

    // Check if current holder should lose status
    if (currentReserve != INVALID_PLAYER) {
        for (uint32_t i = 0; i < trustPool->size(); ++i) {
            CurrencyTrustComponent& trust = trustPool->data()[i];
            if (trust.owner == currentReserve && trust.trustScore < LOSE_THRESHOLD) {
                trust.isReserveCurrency = false;
                trust.turnsAsReserve = 0;
                currentReserve = INVALID_PLAYER;
                LOG_INFO("Player %u lost reserve currency status (trust %.2f < %.2f)",
                         static_cast<unsigned>(trust.owner), trust.trustScore, LOSE_THRESHOLD);
                break;
            }
        }
    }

    // If no current holder, check if a new candidate qualifies
    if (currentReserve == INVALID_PLAYER && bestCandidate != INVALID_PLAYER
        && bestTrust >= ACQUIRE_THRESHOLD) {
        for (uint32_t i = 0; i < trustPool->size(); ++i) {
            CurrencyTrustComponent& trust = trustPool->data()[i];
            if (trust.owner == bestCandidate) {
                trust.isReserveCurrency = true;
                LOG_INFO("Player %u gained reserve currency status (trust %.2f)",
                         static_cast<unsigned>(trust.owner), trust.trustScore);
                break;
            }
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
    aoc::ecs::World& world = gameState.legacyWorld();
    float efficiencyA = 0.50f;  // Default barter
    float efficiencyB = 0.50f;

    const aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    const aoc::ecs::ComponentPool<CurrencyTrustComponent>* trustPool =
        world.getPool<CurrencyTrustComponent>();

    if (monetaryPool != nullptr) {
        for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
            const MonetaryStateComponent& ms = monetaryPool->data()[i];
            if (ms.owner == playerA) {
                efficiencyA = ms.tradeEfficiency();
                // Apply fiat trust if applicable
                if (ms.system == MonetarySystemType::FiatMoney && trustPool != nullptr) {
                    for (uint32_t j = 0; j < trustPool->size(); ++j) {
                        if (trustPool->data()[j].owner == playerA) {
                            efficiencyA = fiatTradeEfficiency(trustPool->data()[j]);
                            break;
                        }
                    }
                }
            } else if (ms.owner == playerB) {
                efficiencyB = ms.tradeEfficiency();
                if (ms.system == MonetarySystemType::FiatMoney && trustPool != nullptr) {
                    for (uint32_t j = 0; j < trustPool->size(); ++j) {
                        if (trustPool->data()[j].owner == playerB) {
                            efficiencyB = fiatTradeEfficiency(trustPool->data()[j]);
                            break;
                        }
                    }
                }
            }
        }
    }

    // Bilateral efficiency = minimum of both players' capabilities.
    // You can only trade as efficiently as the weakest monetary link.
    float baseEfficiency = std::min(efficiencyA, efficiencyB);

    // Exchange rate volatility penalty: if both are on fiat and their rates
    // differ significantly, trade is less efficient (exchange rate risk).
    const aoc::ecs::ComponentPool<CurrencyExchangeComponent>* forexPool =
        world.getPool<CurrencyExchangeComponent>();
    if (forexPool != nullptr) {
        const CurrencyExchangeComponent* forexA = nullptr;
        const CurrencyExchangeComponent* forexB = nullptr;
        for (uint32_t i = 0; i < forexPool->size(); ++i) {
            if (forexPool->data()[i].owner == playerA) { forexA = &forexPool->data()[i]; }
            if (forexPool->data()[i].owner == playerB) { forexB = &forexPool->data()[i]; }
        }
        if (forexA != nullptr && forexB != nullptr) {
            float xRate = bilateralExchangeRate(*forexA, *forexB);
            // Exchange rate far from 1.0 creates friction (hedging costs)
            float deviation = std::abs(xRate - 1.0f);
            if (deviation > 0.5f) {
                // More than 50% deviation: up to 10% trade efficiency penalty
                float penalty = std::min(0.10f, (deviation - 0.5f) * 0.10f);
                baseEfficiency *= (1.0f - penalty);
            }
        }
    }

    return baseEfficiency;
}

} // namespace aoc::sim
