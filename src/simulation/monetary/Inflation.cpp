/**
 * @file Inflation.cpp
 * @brief Fisher equation-based inflation calculation.
 */

#include "aoc/simulation/monetary/Inflation.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

void computeInflation(MonetaryStateComponent& state,
                      CurrencyAmount previousGDP,
                      CurrencyAmount currentGDP,
                      CurrencyAmount previousMoneySupply) {
    // Barter has no inflation
    if (state.system == MonetarySystemType::Barter) {
        state.inflationRate = 0.0f;
        state.priceLevel = 1.0f;
        return;
    }

    // Fisher equation: inflationRate = dM/M + dV/V - dY/Y
    // Where:
    //   dM/M = money supply growth rate
    //   dV/V = velocity change rate (we model as slowly mean-reverting)
    //   dY/Y = real GDP growth rate

    float moneyGrowth = 0.0f;
    if (previousMoneySupply > 0) {
        moneyGrowth = static_cast<float>(state.moneySupply - previousMoneySupply)
                    / static_cast<float>(previousMoneySupply);
    }

    float gdpGrowth = 0.0f;
    if (previousGDP > 0) {
        gdpGrowth = static_cast<float>(currentGDP - previousGDP)
                  / static_cast<float>(previousGDP);
    }

    // currentGDP / previousGDP are nominal (coin-denominated, include price level).
    // Fisher equation needs REAL output growth: subtract prior-turn inflation to
    // deflate. Without this the -gdpGrowth term self-cancels inflation, inverting
    // the feedback loop and letting printing run undetected.
    const float realGDPGrowth = gdpGrowth - state.inflationRate;

    // Velocity changes slowly. High interest rates slow velocity (people save more).
    // Low rates increase velocity (people spend/invest more).
    float targetVelocity = 1.0f;
    if (state.system == MonetarySystemType::FiatMoney
        || state.system == MonetarySystemType::Digital) {
        // In fiat/digital, velocity is more responsive to interest rates.
        // Digital payments settle instantly, pushing the ceiling higher.
        const float ceiling = (state.system == MonetarySystemType::Digital) ? 1.7f : 1.5f;
        targetVelocity = 1.2f - state.interestRate * 2.0f;  // Range ~0.2 to 1.2
        targetVelocity = std::clamp(targetVelocity, 0.3f, ceiling);
    } else if (state.system == MonetarySystemType::GoldStandard) {
        targetVelocity = 1.0f - state.interestRate;
        targetVelocity = std::clamp(targetVelocity, 0.5f, 1.2f);
    } else {
        // Commodity money: velocity is relatively stable
        targetVelocity = 0.8f;
    }

    float previousVelocity = state.velocityOfMoney;
    // Velocity adjusts 20% toward target each turn (slow drift)
    state.velocityOfMoney = previousVelocity * 0.8f + targetVelocity * 0.2f;

    float velocityChange = 0.0f;
    if (std::abs(previousVelocity) > 0.001f) {
        velocityChange = (state.velocityOfMoney - previousVelocity) / previousVelocity;
    }

    // Core inflation calculation (Fisher equation baseline).
    // Use REAL growth, not nominal: nominal GDP includes inflation by construction,
    // so subtracting it would self-cancel the money-growth term and invert the
    // feedback sign at high inflation.
    float fisherInflation = moneyGrowth + velocityChange - realGDPGrowth;

    // MMT correction: inflation depends on spending vs productive CAPACITY,
    // not just money supply. If real output is growing (spare capacity),
    // printing is less inflationary. If shrinking, fully inflationary.
    // Capacity slope: 3.5 means ~3% real growth trims ~10% off inflation
    // (capacityPressure ~ 0.9), hitting the 0.3 floor at ~20% growth.
    // Previous 14.0 was hair-trigger — a routine 5% expansion maxed out
    // the damping and erased most Fisher inflation, masking genuine
    // money-printing pressure.
    float capacityPressure = 1.0f;
    if (realGDPGrowth > 0.0f) {
        capacityPressure = std::max(0.3f, 1.0f - realGDPGrowth * 3.5f);
    } else if (realGDPGrowth < -0.02f) {
        capacityPressure = std::min(1.5f, 1.0f + std::abs(realGDPGrowth) * 10.0f);
    }

    state.inflationRate = fisherInflation * capacityPressure;

    // Fiat money printing inflation (direct impact from printMoney())
    // This is ADDITIVE — printing always creates some inflation regardless
    // of capacity, but capacity_pressure modulates how much.
    if (state.printAmountThisTurn > 0 && currentGDP > 0) {
        float printingInflation = static_cast<float>(state.printAmountThisTurn)
                                / static_cast<float>(currentGDP);
        state.inflationRate += printingInflation * capacityPressure;
        state.printAmountThisTurn = 0;  // Reset for next turn
    }

    // Treasury hoarding: sign depends on monetary regime.
    //   GoldStandard/Commodity: hoarded coin is out of circulation. Effective
    //       M shrinks -> deflationary pressure.
    //   Fiat/Digital: hoarding signals precautionary demand. Treasury can
    //       monetize debt or spend; markets price in the future injection
    //       -> inflationary pressure.
    //   Barter: handled at top of function (returns early).
    if (currentGDP > 0 && state.treasury > 0) {
        float treasuryToGDP = static_cast<float>(state.treasury) / static_cast<float>(currentGDP);
        if (treasuryToGDP > 2.0f) {
            float excessPressure = (treasuryToGDP - 2.0f) * 0.01f;
            if (state.system == MonetarySystemType::FiatMoney
                || state.system == MonetarySystemType::Digital) {
                state.inflationRate += excessPressure;
            } else {
                // CommodityMoney / GoldStandard: hoarding is dis-inflationary.
                state.inflationRate -= excessPressure;
            }
        }
    }

    // Commodity money and gold standard have natural inflation dampening
    if (state.system == MonetarySystemType::CommodityMoney) {
        // Money supply is tied to gold -- inflation is limited to gold mining rate
        state.inflationRate *= 0.3f;
    } else if (state.system == MonetarySystemType::GoldStandard) {
        // Partial dampening from gold backing requirement
        float backingDampening = state.goldBackingRatio;  // Higher backing = more stable
        state.inflationRate *= (1.0f - backingDampening * 0.5f);
    }

    // Clamp to reasonable range (-20% to +50% per turn)
    state.inflationRate = std::clamp(state.inflationRate, -0.20f, 0.50f);
}

void applyInflationEffects(MonetaryStateComponent& state) {
    // Update cumulative price level
    state.priceLevel *= (1.0f + state.inflationRate);
    state.priceLevel = std::clamp(state.priceLevel, 0.1f, 10.0f);  // Cap at 10x base (hyperinflation ceiling)

    // Debt is eroded by inflation (real value decreases)
    if (state.inflationRate > 0.0f && state.governmentDebt > 0) {
        float realDebtReduction = state.inflationRate * 0.5f;  // Partial erosion
        CurrencyAmount reduction = static_cast<CurrencyAmount>(
            static_cast<float>(state.governmentDebt) * realDebtReduction);
        state.governmentDebt = std::max(static_cast<CurrencyAmount>(0),
                                        state.governmentDebt - reduction);
    }
}

float inflationHappinessPenalty(Percentage inflationRate) {
    float absRate = std::abs(inflationRate);

    if (absRate <= 0.03f) {
        return 0.0f;  // 0-3% is fine
    }

    if (inflationRate < 0.0f) {
        // Deflation: moderate penalty (unemployment)
        return (absRate - 0.03f) * 20.0f;  // -5% deflation = 0.4 penalty
    }

    // Inflation penalty curve:
    //   3-5%:  mild (0.0 - 1.0)
    //   5-10%: moderate (1.0 - 3.0)
    //   10-15%: severe (3.0 - 5.0)
    //   >15%: crisis (5.0+)
    if (absRate <= 0.05f) {
        return (absRate - 0.03f) * 50.0f;   // Linear 0 to 1
    }
    if (absRate <= 0.10f) {
        return 1.0f + (absRate - 0.05f) * 40.0f;  // 1 to 3
    }
    if (absRate <= 0.15f) {
        return 3.0f + (absRate - 0.10f) * 40.0f;  // 3 to 5
    }
    return 5.0f + (absRate - 0.15f) * 30.0f;  // 5+ (no cap)
}

// ============================================================================
// Price level effects on the real economy
// ============================================================================

float priceLevelMaintenanceMultiplier(float priceLevel) {
    // Maintenance scales with price level but with dampening.
    // At base (1.0): 1.0x maintenance.
    // At 2.0 (100% cumulative inflation): ~1.5x maintenance.
    // At 0.5 (50% deflation): ~0.8x maintenance (wages drop slower than prices).
    if (priceLevel <= 0.1f) {
        return 0.8f;
    }
    // Dampened scaling: sqrt growth prevents maintenance from becoming absurd
    return std::clamp(0.5f + 0.5f * std::sqrt(priceLevel), 0.8f, 3.0f);
}

float inflationProductionModifier(float inflationRate) {
    float absRate = std::abs(inflationRate);

    // Mild inflation (1-3%) greases the economy: slight production bonus
    if (inflationRate >= 0.01f && inflationRate <= 0.03f) {
        return 0.98f;  // 2% production bonus
    }

    // Deflation hurts production (unsold inventory, layoffs)
    if (inflationRate < 0.0f) {
        return 1.0f + absRate * 2.0f;  // -5% deflation = 1.10x cost (+10%)
    }

    // Moderate inflation (3-10%): rising costs
    if (absRate <= 0.10f) {
        return 1.0f + (absRate - 0.03f) * 1.5f;  // Up to ~1.1x at 10%
    }

    // High inflation (10%+): significant supply chain disruption
    return 1.1f + (absRate - 0.10f) * 3.0f;  // Gets expensive fast
}

// ============================================================================
// Banking and monetary system GDP effects
// ============================================================================

float bankingGDPMultiplier(const MonetaryStateComponent& state) {
    switch (state.system) {
        case MonetarySystemType::Barter:
            return 1.0f;  // No credit creation, pure goods exchange
        case MonetarySystemType::CommodityMoney:
            // Coins enable basic commerce but no fractional reserve banking
            return 1.05f;
        case MonetarySystemType::GoldStandard: {
            // Paper notes backed by metal enable credit creation.
            // Money multiplier from fractional reserves amplifies GDP.
            float multiplier = 1.0f / std::max(0.01f, state.reserveRequirement);
            // Cap the effect: reserve banking adds 5-15% GDP depending on reserve ratio
            float bonus = std::clamp((multiplier - 1.0f) * 0.02f, 0.05f, 0.15f);
            return 1.0f + bonus;
        }
        case MonetarySystemType::FiatMoney:
        case MonetarySystemType::Digital: {
            // Full modern banking: credit cards, commercial lending, derivatives.
            // Digital adds instant-settlement efficiency on top.
            float baseBonus = (state.system == MonetarySystemType::Digital) ? 0.25f : 0.20f;
            // High debt-to-GDP reduces the banking bonus (crowding out)
            if (state.gdp > 0) {
                float debtToGDP = static_cast<float>(state.governmentDebt)
                                / static_cast<float>(state.gdp);
                if (debtToGDP > 1.0f) {
                    baseBonus -= std::min(0.10f, (debtToGDP - 1.0f) * 0.05f);
                }
            }
            return 1.0f + std::max(0.05f, baseBonus);
        }
        default:
            return 1.0f;
    }
}

float economicStabilityMultiplier(const MonetaryStateComponent& state) {
    float stability = 1.0f;

    // Low inflation helps (scholars can plan, patrons can fund)
    float absInflation = std::abs(state.inflationRate);
    if (absInflation < 0.03f) {
        stability += 0.05f;  // +5% science/culture in stable economies
    } else if (absInflation < 0.05f) {
        stability += 0.0f;   // Neutral
    } else if (absInflation < 0.10f) {
        stability -= 0.05f;  // -5%: universities losing funding
    } else {
        stability -= 0.15f;  // -15%: brain drain, cultural stagnation
    }

    // Advanced monetary systems enable larger research institutions
    if (state.system == MonetarySystemType::GoldStandard) {
        stability += 0.05f;  // +5%: banking funds universities
    } else if (state.system == MonetarySystemType::FiatMoney) {
        stability += 0.10f;  // +10%: government research grants
    } else if (state.system == MonetarySystemType::Digital) {
        stability += 0.12f;  // +12%: efficient capital allocation
    }

    return std::clamp(stability, 0.80f, 1.15f);
}

CurrencyAmount computeSeigniorage(const MonetaryStateComponent& state,
                                   bool isReserveCurrency,
                                   CurrencyAmount totalForeignGDP) {
    if (!isReserveCurrency
        || (state.system != MonetarySystemType::FiatMoney
            && state.system != MonetarySystemType::Digital)) {
        return 0;
    }

    // Seigniorage: ~0.5% of foreign GDP that uses your currency as reserve.
    // This represents the "exorbitant privilege" of reserve currency status:
    // other nations hold your currency, effectively giving you an interest-free loan.
    float seigniorageRate = 0.005f;

    // Higher trust = more nations holding your currency = more seigniorage
    // (trust is checked externally, but we scale by the foreign GDP ratio)
    CurrencyAmount income = static_cast<CurrencyAmount>(
        static_cast<float>(totalForeignGDP) * seigniorageRate);

    return std::max(static_cast<CurrencyAmount>(0), income);
}

float realRateConsumptionMultiplier(float realRate) {
    // Clamp the input so stray hyperinflation/deflation spikes can't invert
    // the curve. [-15%, +15%] real-rate band covers every historical regime
    // the simulation produces.
    const float r = std::clamp(realRate, -0.15f, 0.15f);
    // Linear response: each 1pp of real rate shifts demand by ~2pp.
    // r = +10% -> 0.80x; r = 0 -> 1.00x; r = -10% -> 1.20x.
    return std::clamp(1.0f - r * 2.0f, 0.70f, 1.25f);
}

} // namespace aoc::sim
