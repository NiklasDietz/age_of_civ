/**
 * @file FiscalPolicy.cpp
 * @brief Taxation, spending, deficit, and debt management.
 */

#include "aoc/simulation/monetary/FiscalPolicy.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

void setTaxRate(MonetaryStateComponent& state, Percentage rate) {
    state.taxRate = std::clamp(rate, 0.0f, 0.60f);
}

void setGovernmentSpending(MonetaryStateComponent& state, CurrencyAmount amount) {
    state.governmentSpending = std::max(static_cast<CurrencyAmount>(0), amount);
}

void executeFiscalPolicy(MonetaryStateComponent& state, CurrencyAmount gdp) {
    state.gdp = gdp;

    // Revenue = taxRate * GDP
    state.taxRevenue = static_cast<CurrencyAmount>(
        state.taxRate * static_cast<float>(gdp));

    // Deficit = spending - revenue
    state.deficit = state.governmentSpending - state.taxRevenue;

    if (state.deficit > 0) {
        // Running a deficit: borrow (add to debt)
        state.governmentDebt += state.deficit;
    } else {
        // Running a surplus: pay down debt
        CurrencyAmount surplus = -state.deficit;
        CurrencyAmount debtPayment = std::min(surplus, state.governmentDebt);
        state.governmentDebt -= debtPayment;
        // Remaining surplus goes to treasury
        state.treasury += (surplus - debtPayment);
    }

    // Interest on debt: annual rate applied per turn
    // Simplification: interestRate is per-turn (game turns ~ quarters/years)
    if (state.governmentDebt > 0) {
        CurrencyAmount interest = static_cast<CurrencyAmount>(
            static_cast<float>(state.governmentDebt) * state.interestRate);
        interest = std::max(static_cast<CurrencyAmount>(1), interest);
        state.governmentDebt += interest;
    }

    // In non-barter systems, government spending enters the money supply
    if (state.system != MonetarySystemType::Barter) {
        state.moneySupply += state.governmentSpending;
        // Tax revenue removes money from circulation
        state.moneySupply -= state.taxRevenue;
        state.moneySupply = std::max(static_cast<CurrencyAmount>(0), state.moneySupply);
    }
}

ErrorCode monetizeDebt(MonetaryStateComponent& state, CurrencyAmount amount) {
    if (state.system != MonetarySystemType::FiatMoney) {
        return ErrorCode::InvalidMonetaryTransition;
    }
    if (amount <= 0 || amount > state.governmentDebt) {
        return ErrorCode::InvalidArgument;
    }

    // Print money to pay debt
    state.governmentDebt -= amount;
    state.moneySupply += amount;  // New money enters circulation

    return ErrorCode::Ok;
}

float taxHappinessModifier(Percentage taxRate) {
    if (taxRate <= 0.10f) {
        return 1.0f;   // Low taxes: +1 amenity equivalent
    }
    if (taxRate <= 0.20f) {
        return 0.0f;   // Moderate: neutral
    }
    if (taxRate <= 0.30f) {
        return -1.0f;   // Getting uncomfortable
    }
    if (taxRate <= 0.40f) {
        return -2.0f;
    }
    if (taxRate <= 0.50f) {
        return -3.0f;
    }
    return -5.0f;  // Confiscatory taxation
}

float spendingGrowthModifier(const MonetaryStateComponent& state) {
    if (state.gdp <= 0) {
        return 1.0f;
    }

    // Spending-to-GDP ratio
    float spendingRatio = static_cast<float>(state.governmentSpending)
                        / static_cast<float>(state.gdp);

    // Keynesian multiplier: spending up to 20% of GDP stimulates growth
    if (spendingRatio <= 0.20f) {
        return 1.0f + spendingRatio * 0.5f;  // Up to 1.1x growth
    }

    // Beyond 20%: diminishing returns, and debt-financed spending has drag
    float debtToGDP = (state.gdp > 0)
        ? static_cast<float>(state.governmentDebt) / static_cast<float>(state.gdp)
        : 0.0f;

    // High debt-to-GDP reduces the stimulative effect
    float debtDrag = std::clamp(debtToGDP * 0.1f, 0.0f, 0.3f);
    float stimulus = 1.1f - (spendingRatio - 0.20f) * 0.5f - debtDrag;

    return std::max(0.7f, stimulus);
}

} // namespace aoc::sim
