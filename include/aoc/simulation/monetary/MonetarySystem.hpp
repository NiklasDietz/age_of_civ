#pragma once

/**
 * @file MonetarySystem.hpp
 * @brief Per-player monetary system state machine and ECS component.
 *
 * The monetary system evolves through four stages, each unlocked by
 * technology/civics and a deliberate player decision:
 *
 *   Barter -> CommodityMoney -> GoldStandard -> FiatMoney
 *
 * Each stage offers more economic tools but also more risk:
 *   - Barter: direct good-for-good exchange, very limited trade range.
 *   - Commodity money (gold): universal medium, stable value tied to gold supply.
 *   - Gold standard: paper currency backed by gold reserves at a fixed rate.
 *     More flexible than commodity money but constrained by gold holdings.
 *   - Fiat money: government-declared legal tender with no commodity backing.
 *     Full control over money supply, powerful stimulus tools, but risks
 *     inflation/hyperinflation if mismanaged.
 *
 * Transitions are one-way (no going back to barter from fiat).
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <cstdint>
#include <string_view>

namespace aoc::sim {

// ============================================================================
// Monetary system type (state machine states)
// ============================================================================

enum class MonetarySystemType : uint8_t {
    Barter,          ///< No currency. Trade is direct good-for-good.
    CommodityMoney,  ///< Gold IS money. Value = gold's intrinsic value.
    GoldStandard,    ///< Paper currency backed by gold reserves at fixed rate.
    FiatMoney,       ///< Unbacked government currency. Full monetary control.

    Count
};

[[nodiscard]] constexpr std::string_view monetarySystemName(MonetarySystemType type) {
    switch (type) {
        case MonetarySystemType::Barter:         return "Barter";
        case MonetarySystemType::CommodityMoney: return "Commodity Money";
        case MonetarySystemType::GoldStandard:   return "Gold Standard";
        case MonetarySystemType::FiatMoney:      return "Fiat Money";
        default:                                 return "Unknown";
    }
}

// ============================================================================
// Transition requirements
// ============================================================================

/// What a player needs to transition to the next monetary stage.
struct MonetaryTransitionReq {
    MonetarySystemType target;
    TechId             requiredTech;      ///< Tech prerequisite (INVALID = none)
    CurrencyAmount     minGoldReserves;   ///< Minimum gold on hand
    int32_t            minCityCount;      ///< Minimum number of cities
};

/// Hard-coded transition requirements. Will be data-driven later.
inline constexpr std::array<MonetaryTransitionReq, 3> MONETARY_TRANSITIONS = {{
    {MonetarySystemType::CommodityMoney, TechId{},  50,  1},   // Barter -> Commodity
    {MonetarySystemType::GoldStandard,   TechId{},  200, 2},   // Commodity -> Gold Standard
    {MonetarySystemType::FiatMoney,      TechId{},  100, 3},   // Gold Standard -> Fiat
}};

// ============================================================================
// Per-player monetary state (ECS component)
// ============================================================================

struct MonetaryStateComponent {
    PlayerId           owner;
    MonetarySystemType system = MonetarySystemType::Barter;

    // -- Money supply --
    CurrencyAmount moneySupply    = 0;    ///< Total currency in circulation
    CurrencyAmount goldReserves   = 100;  ///< Physical gold held (backs currency in gold standard)
    Percentage     goldBackingRatio = 1.0f; ///< Currency units per gold unit (gold standard only)

    // -- Inflation --
    Percentage     inflationRate  = 0.0f; ///< Current annual CPI change (can be negative = deflation)
    Percentage     priceLevel     = 1.0f; ///< Cumulative price level (1.0 = base, 1.1 = 10% above)

    // -- Central bank controls (only meaningful in GoldStandard / Fiat) --
    Percentage     interestRate       = 0.05f; ///< Central bank rate (0.0 to 1.0)
    Percentage     reserveRequirement = 0.10f; ///< Fractional reserve ratio

    // -- Fiscal policy --
    Percentage     taxRate            = 0.15f; ///< Income tax rate (0.0 to 1.0)
    CurrencyAmount governmentSpending = 0;     ///< Per-turn spending
    CurrencyAmount governmentDebt     = 0;     ///< Accumulated deficit
    CurrencyAmount taxRevenue         = 0;     ///< Last turn's tax income (computed)
    CurrencyAmount deficit            = 0;     ///< Last turn's deficit (spending - revenue)

    // -- Derived stats for UI --
    CurrencyAmount gdp               = 0;     ///< Gross domestic product (total production value)
    Percentage     velocityOfMoney    = 1.0f;  ///< How fast money circulates

    // ========================================================================
    // Transition logic
    // ========================================================================

    /**
     * @brief Check if the player can transition to a target monetary system.
     * @return Ok if transition is valid, InvalidMonetaryTransition if not.
     */
    [[nodiscard]] ErrorCode canTransition(MonetarySystemType target,
                                           int32_t cityCount) const {
        // Must be the next stage in sequence
        uint8_t currentOrd = static_cast<uint8_t>(this->system);
        uint8_t targetOrd  = static_cast<uint8_t>(target);
        if (targetOrd != currentOrd + 1) {
            return ErrorCode::InvalidMonetaryTransition;
        }

        // Check requirements
        for (const MonetaryTransitionReq& req : MONETARY_TRANSITIONS) {
            if (req.target == target) {
                if (this->goldReserves < req.minGoldReserves) {
                    return ErrorCode::InvalidMonetaryTransition;
                }
                if (cityCount < req.minCityCount) {
                    return ErrorCode::InvalidMonetaryTransition;
                }
                return ErrorCode::Ok;
            }
        }
        return ErrorCode::InvalidMonetaryTransition;
    }

    /**
     * @brief Execute the transition to a new monetary system.
     *
     * Sets initial values for the new system. Assumes canTransition() returned Ok.
     */
    void transitionTo(MonetarySystemType target) {
        this->system = target;

        switch (target) {
            case MonetarySystemType::CommodityMoney:
                // Gold IS money. Money supply = gold reserves.
                this->moneySupply = this->goldReserves;
                this->goldBackingRatio = 1.0f;
                break;

            case MonetarySystemType::GoldStandard:
                // Issue paper currency backed by gold. Initial money supply = 2x gold.
                this->moneySupply = this->goldReserves * 2;
                this->goldBackingRatio = 0.5f;  // 50% gold backing
                break;

            case MonetarySystemType::FiatMoney:
                // Money is no longer backed by gold. Gold becomes just another commodity.
                // Keep current money supply but remove the backing constraint.
                this->goldBackingRatio = 0.0f;
                break;

            default:
                break;
        }
    }

    // ========================================================================
    // Trade modifiers based on monetary system
    // ========================================================================

    /// Multiplier on trade efficiency. Barter is very inefficient.
    [[nodiscard]] float tradeEfficiency() const {
        switch (this->system) {
            case MonetarySystemType::Barter:         return 0.5f;   // 50% of potential trade value
            case MonetarySystemType::CommodityMoney: return 0.8f;
            case MonetarySystemType::GoldStandard:   return 0.95f;
            case MonetarySystemType::FiatMoney:      return 1.0f;
            default:                                 return 1.0f;
        }
    }

    /// Maximum number of simultaneous trade routes allowed.
    [[nodiscard]] int32_t maxTradeRoutes() const {
        switch (this->system) {
            case MonetarySystemType::Barter:         return 1;
            case MonetarySystemType::CommodityMoney: return 3;
            case MonetarySystemType::GoldStandard:   return 6;
            case MonetarySystemType::FiatMoney:      return 10;
            default:                                 return 1;
        }
    }
};

} // namespace aoc::sim
