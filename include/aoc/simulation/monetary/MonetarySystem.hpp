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

/// Trade efficiency multiplier for each coin tier.
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
inline constexpr int32_t COIN_TIER_THRESHOLD = 5;

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
    CoinTier           minCoinTier;        ///< Minimum coin tier held
    int32_t            minCoinReserves;    ///< Minimum total coin amount across all tiers
    int32_t            minCityCount;       ///< Minimum number of cities
    int32_t            minTurnsInCurrent;  ///< Minimum turns spent in current system
    int32_t            minTradePartners;   ///< Minimum active trade partners
    float              maxInflation;       ///< Maximum inflation rate allowed (for stability check)
};

/// Transition requirements. Fiat has strict prerequisites to prevent abuse.
inline constexpr std::array<MonetaryTransitionReq, 3> MONETARY_TRANSITIONS = {{
    // Barter -> Commodity Money: just need any coins + a mint
    {MonetarySystemType::CommodityMoney, TechId{},  CoinTier::Copper, 5,   1, 0, 0, 1.0f},
    // Commodity -> Gold Standard: need banking, gold coins, 2 cities
    {MonetarySystemType::GoldStandard,   TechId{9}, CoinTier::Gold,   20,  2, 0, 0, 1.0f},
    // Gold Standard -> Fiat: need economics, stability, trade network, GDP rank checked separately
    {MonetarySystemType::FiatMoney,      TechId{13},CoinTier::Gold,   10,  3, 5, 3, 0.10f},
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

    // -- System duration tracking --
    int32_t turnsInCurrentSystem = 0;

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

    /// Total coin reserves across all tiers (weighted by value).
    [[nodiscard]] int32_t totalCoinValue() const {
        return this->copperCoinReserves * 1
             + this->silverCoinReserves * 2
             + this->goldCoinReserves   * 4;
    }

    /// Total raw coin count across all tiers.
    [[nodiscard]] int32_t totalCoinCount() const {
        return this->copperCoinReserves + this->silverCoinReserves + this->goldCoinReserves;
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
                if (this->effectiveCoinTier < req.minCoinTier) {
                    return ErrorCode::InvalidMonetaryTransition;
                }
                if (this->totalCoinCount() < req.minCoinReserves) {
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
                // Issue paper notes backed by gold coins at 2:1 ratio.
                this->moneySupply = static_cast<CurrencyAmount>(this->goldCoinReserves) * 2;
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

    /// Trade efficiency multiplier. Accounts for coin tier and debasement.
    [[nodiscard]] float tradeEfficiency() const {
        float baseEfficiency = 0.50f;

        switch (this->system) {
            case MonetarySystemType::Barter:
                baseEfficiency = 0.50f;
                break;
            case MonetarySystemType::CommodityMoney:
                baseEfficiency = coinTierTradeEfficiency(this->effectiveCoinTier);
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
    [[nodiscard]] int32_t maxTradeRoutes() const {
        switch (this->system) {
            case MonetarySystemType::Barter:         return 1;
            case MonetarySystemType::CommodityMoney:
                // More trade routes with higher coin tiers
                switch (this->effectiveCoinTier) {
                    case CoinTier::None:   return 1;
                    case CoinTier::Copper: return 2;
                    case CoinTier::Silver: return 3;
                    case CoinTier::Gold:   return 4;
                    default:               return 1;
                }
            case MonetarySystemType::GoldStandard:   return 6;
            case MonetarySystemType::FiatMoney:      return 10;
            default:                                 return 1;
        }
    }

    /// Get the gold reserves value used for gold standard backing checks.
    /// In gold standard, this is the gold coin reserves.
    /// In fiat, gold is just a commodity (value comes from market).
    [[nodiscard]] CurrencyAmount goldReserves() const {
        return static_cast<CurrencyAmount>(this->goldCoinReserves);
    }
};

} // namespace aoc::sim
