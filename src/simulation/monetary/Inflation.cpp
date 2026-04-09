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

    // Velocity changes slowly. High interest rates slow velocity (people save more).
    // Low rates increase velocity (people spend/invest more).
    float targetVelocity = 1.0f;
    if (state.system == MonetarySystemType::FiatMoney) {
        // In fiat, velocity is more responsive to interest rates
        targetVelocity = 1.2f - state.interestRate * 2.0f;  // Range ~0.2 to 1.2
        targetVelocity = std::clamp(targetVelocity, 0.3f, 1.5f);
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

    // Core inflation calculation
    state.inflationRate = moneyGrowth + velocityChange - gdpGrowth;

    // Treasury hoarding pressure: very large treasuries relative to GDP
    // cause inflationary pressure (excess money in the economy)
    if (currentGDP > 0 && state.treasury > 0) {
        float treasuryToGDP = static_cast<float>(state.treasury) / static_cast<float>(currentGDP);
        if (treasuryToGDP > 5.0f) {
            // Treasury is 5x+ GDP -- this excess money creates inflation
            float excessPressure = (treasuryToGDP - 5.0f) * 0.005f;
            state.inflationRate += excessPressure;
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
    state.priceLevel = std::max(state.priceLevel, 0.1f);  // Floor at 10% of base

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

} // namespace aoc::sim
