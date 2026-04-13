#pragma once

/**
 * @file MonetarySystem.hpp
 * @brief Per-player monetary system state machine and ECS component.
 *
 * The monetary system evolves through four stages, each unlocked by
 * technology, economic prerequisites, and a deliberate player decision:
 *
 *   Barter -> CommodityMoney -> GoldStandard -> FiatMoney
 *
 * In the CommodityMoney stage, currency is physical coins minted from
 * copper, silver, or gold ore. The player's effective coin tier depends
 * on which metals they actually hold:
 *
 *   CoinTier::None    -- no coins (Barter only)
 *   CoinTier::Copper  -- local trade, low efficiency
 *   CoinTier::Silver  -- regional trade, medium efficiency
 *   CoinTier::Gold    -- international trade, high efficiency
 *
 * Coins flow between civilizations through trade: the net importer pays
 * the net exporter in their highest-tier coin (price-specie flow).
 * This means a civ without gold mines can still accumulate gold coins
 * by running persistent trade surpluses -- exactly as in real economics.
 *
 * Gold Standard issues paper notes backed by gold coin reserves.
 * Fiat Money removes the gold backing but requires trust from trade
 * partners to be accepted (see CurrencyTrust.hpp).
 *
 * Transitions are one-way (no going back to barter from fiat).
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace aoc::sim {

// ============================================================================
// Monetary system type (state machine states)
// ============================================================================

enum class MonetarySystemType : uint8_t {
    Barter,          ///< No currency. Trade is direct good-for-good.
    CommodityMoney,  ///< Metal coins ARE money. Value = metal's intrinsic value.
    GoldStandard,    ///< Paper currency backed by gold coin reserves at fixed rate.
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
// Coin tier -- determined by which minted coins the player holds
// ============================================================================

enum class CoinTier : uint8_t {
    None   = 0,   ///< No coins at all (barter)
    Copper = 1,   ///< Copper coins only
    Silver = 2,   ///< Silver coins available
    Gold   = 3,   ///< Gold coins available

    Count
};

[[nodiscard]] constexpr std::string_view coinTierName(CoinTier tier) {
    switch (tier) {
        case CoinTier::None:   return "None";
        case CoinTier::Copper: return "Copper";
        case CoinTier::Silver: return "Silver";
        case CoinTier::Gold:   return "Gold";
        default:               return "Unknown";
    }
}

/// Trade efficiency multiplier for each coin tier (legacy, used as fallback).
[[nodiscard]] constexpr float coinTierTradeEfficiency(CoinTier tier) {
    switch (tier) {
        case CoinTier::None:   return 0.50f;   // Barter: 50%
        case CoinTier::Copper: return 0.65f;   // Local trade
        case CoinTier::Silver: return 0.80f;   // Regional trade
        case CoinTier::Gold:   return 0.95f;   // International trade
        default:               return 0.50f;
    }
}

/// Minimum coin reserves to qualify for a tier.
inline constexpr int32_t COIN_TIER_THRESHOLD = 3;

// ============================================================================
// Denomination value ratios (historical: Roman aureus/denarius/sestertius)
// ============================================================================

/// Value of one copper coin in base currency units.
inline constexpr int32_t COPPER_COIN_VALUE = 1;
/// Value of one silver coin in base currency units.
inline constexpr int32_t SILVER_COIN_VALUE = 5;
/// Value of one gold coin in base currency units.
inline constexpr int32_t GOLD_COIN_VALUE = 25;

/// Currency strength thresholds for trade efficiency tiers.
/// A civ with 100 copper coins (strength 100) trades as well as one with 4 gold (strength 100).
inline constexpr int32_t STRENGTH_LOCAL_TRADE    = 3;    ///< Minimal coinage
inline constexpr int32_t STRENGTH_REGIONAL_TRADE = 25;   ///< Regional commerce
inline constexpr int32_t STRENGTH_INTERNATIONAL  = 100;  ///< Full international trade

/// Currency strength to trade efficiency mapping.
/// Based on total metal-weighted coin value, not specific metal type.
[[nodiscard]] inline float currencyStrengthTradeEfficiency(int32_t currencyStrength) {
    if (currencyStrength < STRENGTH_LOCAL_TRADE) {
        return 0.50f;   // Barter-equivalent
    }
    if (currencyStrength < STRENGTH_REGIONAL_TRADE) {
        // Linear interpolation 0.65 to 0.80
        float t = static_cast<float>(currencyStrength - STRENGTH_LOCAL_TRADE)
                / static_cast<float>(STRENGTH_REGIONAL_TRADE - STRENGTH_LOCAL_TRADE);
        return 0.65f + t * 0.15f;
    }
    if (currencyStrength < STRENGTH_INTERNATIONAL) {
        // Linear interpolation 0.80 to 0.95
        float t = static_cast<float>(currencyStrength - STRENGTH_REGIONAL_TRADE)
                / static_cast<float>(STRENGTH_INTERNATIONAL - STRENGTH_REGIONAL_TRADE);
        return 0.80f + t * 0.15f;
    }
    return 0.95f;  // Full international trade efficiency
}

/// Good ID for each coin tier.
[[nodiscard]] constexpr uint16_t coinTierGoodId(CoinTier tier) {
    switch (tier) {
        case CoinTier::Copper: return goods::COPPER_COINS;
        case CoinTier::Silver: return goods::SILVER_COINS;
        case CoinTier::Gold:   return goods::GOLD_COINS;
        default:               return goods::COPPER_COINS;
    }
}

// ============================================================================
// Transition requirements
// ============================================================================

/// What a player needs to transition to the next monetary stage.
struct MonetaryTransitionReq {
    MonetarySystemType target;
    TechId             requiredTech;       ///< Tech prerequisite (INVALID = none)
    int32_t            minCurrencyStrength;///< Minimum currency strength (metal-weighted coin value)
    int32_t            minCityCount;       ///< Minimum number of cities
    int32_t            minTurnsInCurrent;  ///< Minimum turns spent in current system
    int32_t            minTradePartners;   ///< Minimum active trade partners
    float              maxInflation;       ///< Maximum inflation rate allowed (for stability check)
};

/// Transition requirements. Based on currency strength, not specific metal type.
/// A civ with 100 copper coins can reach Gold Standard just as well as one with 4 gold coins.
inline constexpr std::array<MonetaryTransitionReq, 3> MONETARY_TRANSITIONS = {{
    // Barter -> Commodity Money: need any coins worth >= 3 currency units
    {MonetarySystemType::CommodityMoney, TechId{},  3,    1, 0, 0, 1.0f},
    // Commodity -> Gold Standard: need banking tech, significant reserves, 2 cities
    {MonetarySystemType::GoldStandard,   TechId{9}, 50,   2, 0, 0, 1.0f},
    // Gold Standard -> Fiat: need economics tech, stability, sufficient economy
    {MonetarySystemType::FiatMoney,      TechId{13},100,  3, 5, 0, 0.15f},
}};

// ============================================================================
// Debasement state
// ============================================================================

struct DebasementState {
    float    debasementRatio = 0.0f;   ///< 0.0 = pure coins, up to 0.5 = 50% base metal mixed in
    int32_t  turnsDebased = 0;         ///< How many turns since last debasement (for discovery)
    bool     discoveredByPartners = false; ///< Once discovered, trade penalty applies
};

// ============================================================================
// Per-player monetary state (ECS component)
// ============================================================================

struct MonetaryStateComponent {
    PlayerId           owner;
    MonetarySystemType system = MonetarySystemType::Barter;

    // -- Coin reserves (actual physical coin stockpiles, aggregated from cities) --
    int32_t copperCoinReserves = 0;
    int32_t silverCoinReserves = 0;
    int32_t goldCoinReserves   = 0;

    // -- Current effective coin tier (recomputed each turn) --
    CoinTier effectiveCoinTier = CoinTier::None;

    // -- Money supply (paper/fiat currency in GoldStandard/Fiat) --
    CurrencyAmount moneySupply    = 0;    ///< Total currency in circulation
    CurrencyAmount treasury       = 100;  ///< Government cash on hand (spending power)
    Percentage     goldBackingRatio = 1.0f; ///< Paper currency per gold coin (gold standard only)

    // -- Inflation --
    Percentage     inflationRate  = 0.0f; ///< Current per-turn CPI change
    Percentage     priceLevel     = 1.0f; ///< Cumulative price level (1.0 = base)

    // -- Central bank controls (GoldStandard / Fiat only) --
    Percentage     interestRate       = 0.05f;
    Percentage     reserveRequirement = 0.10f;

    // -- Output allocation slider (gold/science/luxury split) --
    // Fraction of city output allocated to each purpose. Should sum to ~1.0.
    // Gold: goes to treasury. Science: boosts research. Luxury: boosts happiness.
    Percentage     goldAllocation     = 0.70f;  ///< 70% to treasury
    Percentage     scienceAllocation  = 0.20f;  ///< 20% bonus to science
    Percentage     luxuryAllocation   = 0.10f;  ///< 10% converted to happiness amenities

    // -- Fiscal policy --
    Percentage     taxRate            = 0.15f;
    CurrencyAmount governmentSpending = 0;
    CurrencyAmount governmentDebt     = 0;
    CurrencyAmount taxRevenue         = 0;
    CurrencyAmount deficit            = 0;

    // -- Derived stats --
    CurrencyAmount gdp               = 0;
    Percentage     velocityOfMoney    = 1.0f;

    // -- Debasement (Commodity Money stage only) --
    DebasementState debasement;

    // -- Fiat currency specifics --
    /// Player-chosen currency name (e.g., "Dollar", "Yuan", "Mark").
    /// Default: civilization name + "Crown" (e.g., "Roman Crown").
    char currencyName[32] = "Crown";

    /// Fiat trust score [0.0, 1.0]. Determines trade acceptance and exchange rate.
    /// Trust depends on: GDP rank, inflation, debt-to-GDP, military, trade partners.
    /// Below 0.3: severe trade penalties, partners demand commodity payment.
    /// 0.3-0.6: fiat accepted at discount.
    /// 0.6-0.8: normal fiat acceptance.
    /// Above 0.8: candidate for reserve currency.
    Percentage fiatTrust = 0.5f;

    /// Whether this player holds reserve currency status (global acceptance).
    bool isReserveCurrency = false;

    /// Cumulative money printed (fiat only). Drives inflation via Fisher equation.
    CurrencyAmount totalMoneyPrinted = 0;

    /// Amount to print this turn (set by government policy).
    CurrencyAmount printAmountThisTurn = 0;

    // -- System duration tracking --
    int32_t turnsInCurrentSystem = 0;

    // -- Bankruptcy tracking --
    /// Number of consecutive turns the treasury has been negative.
    /// Resets to zero whenever treasury >= 0.
    int32_t consecutiveNegativeTurns = 0;

    // ========================================================================
    // Coin tier computation
    // ========================================================================

    /// Recompute the effective coin tier from actual coin reserves.
    void updateCoinTier() {
        if (this->goldCoinReserves >= COIN_TIER_THRESHOLD) {
            this->effectiveCoinTier = CoinTier::Gold;
        } else if (this->silverCoinReserves >= COIN_TIER_THRESHOLD) {
            this->effectiveCoinTier = CoinTier::Silver;
        } else if (this->copperCoinReserves >= COIN_TIER_THRESHOLD) {
            this->effectiveCoinTier = CoinTier::Copper;
        } else {
            this->effectiveCoinTier = CoinTier::None;
        }
    }

    /// Total coin reserves across all tiers (weighted by denomination value).
    /// Uses historical ratios: copper=1, silver=5, gold=25.
    [[nodiscard]] int32_t totalCoinValue() const {
        return this->copperCoinReserves * COPPER_COIN_VALUE
             + this->silverCoinReserves * SILVER_COIN_VALUE
             + this->goldCoinReserves   * GOLD_COIN_VALUE;
    }

    /// Currency strength: total metal-weighted value of all coin reserves.
    /// Determines trade efficiency independent of which specific metals are held.
    [[nodiscard]] int32_t currencyStrength() const {
        return this->totalCoinValue();
    }

    /// Total raw coin count across all tiers.
    [[nodiscard]] int32_t totalCoinCount() const {
        return this->copperCoinReserves + this->silverCoinReserves + this->goldCoinReserves;
    }

    // ========================================================================
    // Fiat money printing (only available in FiatMoney stage)
    // ========================================================================

    /**
     * @brief Print fiat money: add to money supply and treasury.
     *
     * This is theoretically worthless paper that only has value because
     * trade partners trust it. Printing more money:
     *   - Immediately adds gold to treasury (government spending power)
     *   - Increases money supply (M in Fisher equation: M*V = P*Y)
     *   - Causes inflation proportional to money printed / GDP
     *   - Erodes fiat trust if done excessively
     *
     * The temptation: print money to fund wars, buildings, research.
     * The risk: hyperinflation destroys the economy.
     *
     * @param amount  How much to print. Capped at 10% of GDP per turn.
     * @return Actual amount printed (may be capped).
     */
    CurrencyAmount printMoney(CurrencyAmount amount) {
        if (this->system != MonetarySystemType::FiatMoney) {
            return 0;  // Can only print in fiat stage
        }
        // Cap at 10% of GDP per turn to prevent instant hyperinflation
        const CurrencyAmount maxPrint = std::max(
            static_cast<CurrencyAmount>(1),
            static_cast<CurrencyAmount>(this->gdp / 10));
        const CurrencyAmount actualPrint = std::min(amount, maxPrint);

        this->treasury += actualPrint;
        this->moneySupply += actualPrint;
        this->totalMoneyPrinted += actualPrint;
        this->printAmountThisTurn = actualPrint;

        // Direct inflation impact: printed money / GDP
        if (this->gdp > 0) {
            this->inflationRate += static_cast<float>(actualPrint)
                                 / static_cast<float>(this->gdp);
        }
        return actualPrint;
    }

    /// Set the currency name (e.g. "Dollar", "Yuan", "Drachma").
    void setCurrencyName(const char* name) {
        std::size_t len = 0;
        while (name[len] != '\0' && len < 31) { ++len; }
        for (std::size_t i = 0; i < len; ++i) { this->currencyName[i] = name[i]; }
        this->currencyName[len] = '\0';
    }

    // ========================================================================
    // Transition logic
    // ========================================================================

    /**
     * @brief Check if the player can transition to a target monetary system.
     * @param cityCount  Number of cities the player owns.
     * @param tradePartnerCount  Number of active trade partners.
     * @param gdpRank  Player's GDP rank (1 = highest). Used for fiat check.
     * @param playerCount  Total active players. GDP rank must be top half for fiat.
     * @return Ok if transition is valid, InvalidMonetaryTransition if not.
     */
    [[nodiscard]] ErrorCode canTransition(MonetarySystemType target,
                                           int32_t cityCount,
                                           int32_t tradePartnerCount = 0,
                                           int32_t gdpRank = 1,
                                           int32_t playerCount = 1) const {
        // Must be the next stage in sequence
        uint8_t currentOrd = static_cast<uint8_t>(this->system);
        uint8_t targetOrd  = static_cast<uint8_t>(target);
        if (targetOrd != currentOrd + 1) {
            return ErrorCode::InvalidMonetaryTransition;
        }

        for (const MonetaryTransitionReq& req : MONETARY_TRANSITIONS) {
            if (req.target == target) {
                if (this->currencyStrength() < req.minCurrencyStrength) {
                    return ErrorCode::InvalidMonetaryTransition;
                }
                if (cityCount < req.minCityCount) {
                    return ErrorCode::InvalidMonetaryTransition;
                }
                if (this->turnsInCurrentSystem < req.minTurnsInCurrent) {
                    return ErrorCode::InvalidMonetaryTransition;
                }
                if (tradePartnerCount < req.minTradePartners) {
                    return ErrorCode::InvalidMonetaryTransition;
                }
                if (this->inflationRate > req.maxInflation) {
                    return ErrorCode::InvalidMonetaryTransition;
                }
                // Fiat requires GDP rank in top half of players
                if (target == MonetarySystemType::FiatMoney) {
                    int32_t topHalf = std::max(1, playerCount / 2);
                    if (gdpRank > topHalf) {
                        return ErrorCode::InvalidMonetaryTransition;
                    }
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
        this->turnsInCurrentSystem = 0;

        switch (target) {
            case MonetarySystemType::CommodityMoney:
                // Coins ARE money. Money supply = total coin value.
                this->moneySupply = static_cast<CurrencyAmount>(this->totalCoinValue());
                this->goldBackingRatio = 1.0f;
                this->debasement = {};
                break;

            case MonetarySystemType::GoldStandard:
                // Issue paper notes backed by total coin reserves at 2:1 ratio.
                this->moneySupply = static_cast<CurrencyAmount>(this->totalCoinValue()) * 2;
                this->goldBackingRatio = 0.5f;
                this->debasement = {};  // Paper money, debasement no longer applies
                break;

            case MonetarySystemType::FiatMoney:
                // Money is no longer backed by gold. Keep current supply.
                this->goldBackingRatio = 0.0f;
                break;

            default:
                break;
        }
    }

    // ========================================================================
    // Trade modifiers based on monetary system and coin tier
    // ========================================================================

    /// Trade efficiency multiplier. Based on currency strength and monetary system.
    /// A copper-rich civ trades just as well as a gold-rich one with equivalent total value.
    [[nodiscard]] float tradeEfficiency() const {
        float baseEfficiency = 0.50f;

        switch (this->system) {
            case MonetarySystemType::Barter:
                baseEfficiency = 0.50f;
                break;
            case MonetarySystemType::CommodityMoney:
                // Currency strength determines efficiency, not specific metal type
                baseEfficiency = currencyStrengthTradeEfficiency(this->currencyStrength());
                // Debasement penalty once discovered
                if (this->debasement.discoveredByPartners) {
                    baseEfficiency *= (1.0f - this->debasement.debasementRatio * 0.5f);
                }
                break;
            case MonetarySystemType::GoldStandard:
                baseEfficiency = 0.95f;
                break;
            case MonetarySystemType::FiatMoney:
                // Fiat efficiency depends on trust (applied externally via CurrencyTrust)
                baseEfficiency = 1.0f;
                break;
            default:
                break;
        }

        return baseEfficiency;
    }

    /// Maximum number of simultaneous trade routes allowed.
    /// Scales with currency strength in commodity money (more coins = more trade capacity).
    [[nodiscard]] int32_t maxTradeRoutes() const {
        switch (this->system) {
            case MonetarySystemType::Barter:         return 1;
            case MonetarySystemType::CommodityMoney: {
                // Currency strength determines trade capacity
                int32_t strength = this->currencyStrength();
                if (strength < STRENGTH_LOCAL_TRADE)    { return 1; }
                if (strength < STRENGTH_REGIONAL_TRADE) { return 2; }
                if (strength < STRENGTH_INTERNATIONAL)  { return 3; }
                return 4;  // Full commodity money trade capacity
            }
            case MonetarySystemType::GoldStandard:   return 6;
            case MonetarySystemType::FiatMoney:      return 10;
            default:                                 return 1;
        }
    }

    /// Get the total metal reserves value used for gold standard backing checks.
    /// In gold standard, all coin types back the paper currency (weighted by denomination).
    /// In fiat, metal is just a commodity (value comes from market).
    [[nodiscard]] CurrencyAmount metalReserves() const {
        return static_cast<CurrencyAmount>(this->totalCoinValue());
    }

    /// Legacy accessor (gold-specific reserves for gold buy/sell operations).
    [[nodiscard]] CurrencyAmount goldReserves() const {
        return static_cast<CurrencyAmount>(this->goldCoinReserves);
    }
};

} // namespace aoc::sim
