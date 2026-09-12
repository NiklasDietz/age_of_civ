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
 * copper or silver ore. Only one coin type is legal tender at a time
 * (Gresham's Law). When a civ transitions to silver money, copper coins
 * are demonetized and become a meltable commodity (copper ore for
 * electronics chains). Gold is NEVER coined -- it is smelted into bars
 * that serve as treasury reserves backing paper money (Gold Standard).
 *
 *   CoinTier::None    -- no coins (Barter only)
 *   CoinTier::Copper  -- local trade, low efficiency
 *   CoinTier::Silver  -- regional trade, medium efficiency
 *   CoinTier::Gold    -- gold bars held (treasury reserve tier)
 *
 * Coins flow between civilizations through trade: the net importer pays
 * the net exporter in their active legal tender coin (price-specie flow).
 *
 * Gold Standard issues paper notes backed by gold bar reserves.
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
#include <string>
#include <string_view>

namespace aoc::game {
class Player;
}
namespace aoc::save {
struct TreasuryRestore;
}

namespace aoc::sim {

/// The state's cash. Read it anywhere; written only by Player's tagged
/// mutators (MoneyFlow.hpp) and by the save loader, so every change names
/// its counterparty and the turn's money book stays complete. The compiler
/// lists any site that tries to write it another way.
class TreasuryAccount {
public:
    constexpr TreasuryAccount() = default;
    [[nodiscard]] constexpr operator CurrencyAmount() const { return this->m_value; }

private:
    friend class aoc::game::Player;
    friend struct aoc::save::TreasuryRestore;
    CurrencyAmount m_value = 0;
};

// ============================================================================
// Monetary system type (state machine states)
// ============================================================================

enum class MonetarySystemType : uint8_t {
    Barter,          ///< No currency. Trade is direct good-for-good.
    CommodityMoney,  ///< Metal coins ARE money. Value = metal's intrinsic value.
    GoldStandard,    ///< Paper currency backed by gold bar reserves at fixed rate.
    FiatMoney,       ///< Unbacked government currency. Full monetary control.
    Digital,         ///< Electronic settlement. Requires compute + power. No physical carry.

    Count
};

/// Paper notes a gold-standard civ issues per unit of coin value.
///
/// The money supply used to be `coinWealth * (1 + goldBackingRatio)` while the
/// backing ratio was measured as `metalBacking / moneySupply`, both every turn.
/// That is circular: solving it gives r(1+r) = 1, so the ratio converges on
/// 0.618 regardless of anything a player does, permanently clear of the 0.40
/// stress threshold -- until copper passes about 45 % of coin value, at which
/// point it sits permanently below and forces a suspension. Whether a civ kept
/// its gold standard was decided by its copper/silver MIX, not by its conduct,
/// and the design comment describing stress as a response to over-issue could
/// not have been true.
///
/// A statutory note issue breaks the loop. At 1.0 -- notes equal to coin value
/// -- a civ whose coins are all silver and gold measures exactly 0.5 backing,
/// which is the entry value transitionTo sets and the figure that comment
/// assumed. Backing then FALLS as base metal enters the coinage, so debasing or
/// minting copper genuinely erodes the peg and can force a suspension. That is
/// the mechanic the threshold was written for.
inline constexpr float GOLD_STANDARD_NOTE_ISSUE = 1.0f;

/// Cargo slots consumed by the currency medium when a trader carries money on route.
/// Only CommodityMoney is heavy (metal coins). All paper/electronic tiers are 0.
/// Used by `TraderComponent::effectiveCargoSlots()` to shrink goods capacity.
[[nodiscard]] constexpr int32_t moneyWeightSlots(MonetarySystemType type) {
    switch (type) {
        case MonetarySystemType::Barter:         return 0;  // no money; swap goods only
        case MonetarySystemType::CommodityMoney: return 2;  // metal coin chests
        case MonetarySystemType::GoldStandard:   return 0;  // paper notes; bullion stays in vault
        case MonetarySystemType::FiatMoney:      return 0;  // paper, negligible
        case MonetarySystemType::Digital:        return 0;  // nothing carried
        default:                                 return 0;
    }
}

/// Whether gold physically rides with traders (vulnerable to pillage) on this tier.

[[nodiscard]] constexpr std::string_view monetarySystemName(MonetarySystemType type) {
    switch (type) {
        case MonetarySystemType::Barter:         return "Barter";
        case MonetarySystemType::CommodityMoney: return "Commodity Money";
        case MonetarySystemType::GoldStandard:   return "Gold Standard";
        case MonetarySystemType::FiatMoney:      return "Fiat Money";
        case MonetarySystemType::Digital:        return "Digital";
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
    Gold   = 3,   ///< Gold bars held (treasury reserves)

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
/// Value of one gold bar in base currency units (treasury reserve, not circulating).
inline constexpr int32_t GOLD_BAR_VALUE = 25;

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
        case CoinTier::Gold:   return goods::GOLD_BARS;
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
///
/// Historical note: China invented paper money (fiat) in the Song Dynasty (1024 CE),
/// centuries before Europe. The conditions were: high trade volume (metal coins too
/// heavy), Paper Making + Banking tech, and government credibility. We allow early
/// fiat transition when trade volume is high enough, even without modern Economics.
///
/// The Gold Standard -> Fiat transition has two paths:
///   Path A (modern): Economics tech + high currency strength + stability
///   Path B (early/Song): Printing tech + 3 trade partners + low inflation
/// Both require 5+ turns in Gold Standard for stability track record.
inline constexpr std::array<MonetaryTransitionReq, 4> MONETARY_TRANSITIONS = {{
    // Barter -> Commodity Money: need any coins worth >= 3 currency units
    // A state that adopts coinage starts paying its upkeep in coin, so it
    // needs a stock that can carry it: three coppers (the old gate) left a
    // civ in arrears for the whole game once money was conserved. 100 face
    // is a few turns of a fed Mint.
    {MonetarySystemType::CommodityMoney, TechId{},  100,  1, 0, 0, 1.0f},
    // Commodity -> Gold Standard: need banking tech, moderate reserves, 2 cities.
    // 20 copper coins (strength 20) or 4 silver coins is achievable before
    // copper ore depletes (~80 turns of mining at 1 ore/turn).
    {MonetarySystemType::GoldStandard,   TechId{9}, 20,   2, 0, 0, 1.0f},
    // Gold Standard -> Fiat: Banking (TechId{9}). Printing is TechId{55} and
    // Economics TechId{13}; neither is checked by this row.
    // Lowered currency strength requirement. Needs 2 live trade partners (the
    // trade volume that makes metal coins impractical, like Song Dynasty
    // Sichuan; the threshold was 3, which seed-42 maps fence most land off
    // from). Inflation under 5%: must demonstrate monetary discipline first.
    // requestSetMonetaryRegime adds Printing or Economics on top of Banking.
    {MonetarySystemType::FiatMoney,      TechId{9}, 75,   2, 5, 2, 0.05f},
    // Fiat -> Digital: late-game electronic settlement. Needs sustained
    // stability. "Computers" (TechId{16}) gates access; low inflation and a
    // mature economy are required.
    //
    // This comment used to promise "an additional powered-grid check enforced
    // externally by playerMeetsDigitalPowerRequirement()". No such function
    // exists anywhere in the tree, so there is no power requirement: the row
    // below is the whole gate. PlayerEnergyComponent (EnergyDependency.hpp)
    // holds the renewableCapacity and oil-shock state such a check would read,
    // if one is ever wanted.
    //
    // Adding it would change nothing measurable today. Digital is reached in
    // ZERO player-turns of either blessed seed over 500 turns -- seed 42 splits
    // 1060 Barter / 67 Commodity / 33 Gold / 240 Fiat, and seed 43 splits
    // 512 / 128 / 104 / 1256 -- so the row above is already the binding
    // constraint and a power gate would sit behind an unreachable one.
    {MonetarySystemType::Digital,        TechId{16}, 200,  3, 10, 4, 0.10f},
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

/// Minimum money supply in any monetized system. Fisher / velocity / backing
/// math divide by `moneySupply`; a zero or negative value produces undefined
/// state that never recovers. Barter keeps M == 0 (no currency exists).
inline constexpr CurrencyAmount MONEY_SUPPLY_FLOOR = 1;

struct MonetaryStateComponent {
    PlayerId           owner = INVALID_PLAYER;
    MonetarySystemType system = MonetarySystemType::Barter;

    // -- Coin reserves (actual physical coin stockpiles, aggregated from cities) --
    int32_t copperCoinReserves = 0;
    int32_t silverCoinReserves = 0;
    int32_t goldBarReserves   = 0;

    // -- Current effective coin tier (recomputed each turn) --
    CoinTier effectiveCoinTier = CoinTier::None;

    // -- Money supply (paper/fiat currency in GoldStandard/Fiat) --
    CurrencyAmount moneySupply    = 0;    ///< Total currency in circulation
    TreasuryAccount treasury;             ///< Government cash; see TreasuryAccount

    // -- Private money (v34) --
    // Coin the Mint strikes is swept out of the city stockpiles at the end of
    // the turn into one of these pools, so that what the state spends returns
    // to the people it pays instead of vanishing. Nothing reads them until the
    // conserved-money ledger lands; they are carried in the save now so the
    // format is bumped once.
    CurrencyAmount privateSpecie   = 0;   ///< Coin in private hands, civ-wide
    CurrencyAmount privateNotes    = 0;   ///< Paper in private hands once notes are issued
    CurrencyAmount bullion         = 0;   ///< Minted metal held before coinage is adopted
    CoinTier       coinageStandard = CoinTier::None; ///< Metal chosen at adoption; None until then
    /// Metal backing per unit of circulating money, gold standard only. An
    /// OUTPUT: measured each turn as reserves over money supply. It must not be
    /// fed back into the money supply that defines it -- see
    /// GOLD_STANDARD_NOTE_ISSUE.
    Percentage     goldBackingRatio = 1.0f;

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
    //
    // Trust and reserve-currency status live in CurrencyTrustComponent
    // (CurrencyTrust.hpp), which is the model that computes them, the one the
    // save file carries, and the one forex, victory scoring and the UI read.
    // This struct used to carry `fiatTrust` and `isReserveCurrency` as well:
    // shadow copies that nothing read, so every trust penalty the crisis
    // system levied landed on them and did nothing. See CurrencyCrisis.cpp.
    //
    // `totalMoneyPrinted` went with them -- its comment claimed it drove
    // inflation "via Fisher equation", but that equation uses money GROWTH,
    // which is computed from moneySupply directly.

    /// Amount to print this turn (set by government policy).
    CurrencyAmount printAmountThisTurn = 0;

    // -- System duration tracking --
    int32_t turnsInCurrentSystem = 0;

    // -- Bankruptcy tracking --
    /// Consecutive turns with a bill the treasury could not pay (arrears);
    /// resets to zero on the first turn everything is paid. The name is the
    /// save layout's; the treasury itself no longer goes negative.
    int32_t consecutiveNegativeTurns = 0;

    // -- Reserve-ratio stress tracking (GoldStandard -> Fiat cascade) --
    /// Consecutive turns actual backing ratio < 0.7 while on GoldStandard.
    /// Drives the organic Gold->Fiat suspension: once the ratio stays low for
    /// long enough or collapses below 0.2, convertibility is suspended and
    /// the civ is forced onto Fiat (rep + trust penalty applied on transition).
    int32_t reserveStressTurns = 0;
    /// True once a redemption-run drain started (< 0.5 ratio). Lets UI notify.
    bool    redemptionRunActive = false;

    // ========================================================================
    // Taxable turnover
    // ========================================================================

    /// Share of the money supply that changes hands this turn, and so can be
    /// taxed.
    ///
    /// 0.35 is the calibrated share at a neutral velocity of 1.0. Both tax-base
    /// sites in Maintenance.cpp used to hardcode that constant and ignore
    /// `velocityOfMoney` entirely -- which tickInflation recomputes every turn
    /// from the interest rate and the monetary system, and which the save file
    /// carries. The comment beside one of them described velocity as the thing
    /// that "limits how fast the economy can use" its coins, so the model always
    /// meant to consult it.
    ///
    /// Multiplying rather than substituting keeps that calibration: velocity
    /// starts at 1.0, so a fresh game collects exactly what it did before, and
    /// monetary policy moves it from there. Cheap money quickens turnover and
    /// widens the tax base; dear money slows both.
    static constexpr float TAXABLE_SHARE_AT_NEUTRAL_VELOCITY = 0.35f;

    [[nodiscard]] float taxableMoneyShare() const {
        return TAXABLE_SHARE_AT_NEUTRAL_VELOCITY * this->velocityOfMoney;
    }

    // ========================================================================
    // Coin tier computation
    // ========================================================================

    /// Recompute the effective coin tier from actual coin reserves.
    void updateCoinTier() {
        // A standard chosen at adoption (plan 2.5) is the coinage, whatever
        // else has been minted since.
        if (this->coinageStandard != CoinTier::None) {
            this->effectiveCoinTier = this->coinageStandard;
            return;
        }
        if (this->goldBarReserves >= COIN_TIER_THRESHOLD) {
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
    /// Uses historical ratios: copper=1, silver=5, gold bar=25.
    /// Includes ALL reserves regardless of legal tender status (used for
    /// money supply calculation on system transitions).
    [[nodiscard]] int32_t totalCoinValue() const {
        return this->copperCoinReserves * COPPER_COIN_VALUE
             + this->silverCoinReserves * SILVER_COIN_VALUE
             + this->goldBarReserves   * GOLD_BAR_VALUE;
    }

    /// Currency strength: only counts the ACTIVE legal tender (Gresham's Law).
    /// Demonetized coins still exist in reserves but do not contribute.
    ///
    ///   Barter/early CommodityMoney: copper coins only
    ///   CommodityMoney (silver tier): silver coins only (copper is demonetized)
    ///   GoldStandard: silver coins + gold bars (silver circulates, gold backs paper)
    ///   Fiat: based on money supply, metals are just commodities
    [[nodiscard]] int32_t currencyStrength() const {
        switch (this->system) {
            case MonetarySystemType::Barter:
                // Any coinage counts toward exiting barter (copper, silver, or gold bars)
                return this->copperCoinReserves * COPPER_COIN_VALUE
                     + this->silverCoinReserves * SILVER_COIN_VALUE
                     + this->goldBarReserves   * GOLD_BAR_VALUE;

            case MonetarySystemType::CommodityMoney:
                // Only the active legal tender counts (Gresham's Law)
                if (this->effectiveCoinTier == CoinTier::Silver) {
                    return this->silverCoinReserves * SILVER_COIN_VALUE;
                }
                return this->copperCoinReserves * COPPER_COIN_VALUE;

            case MonetarySystemType::GoldStandard:
                // Silver remains everyday currency; gold bars back paper notes
                return this->silverCoinReserves * SILVER_COIN_VALUE
                     + this->goldBarReserves   * GOLD_BAR_VALUE;

            case MonetarySystemType::FiatMoney:
                // Fiat strength is based on money supply, not metals
                return static_cast<int32_t>(this->moneySupply);

            case MonetarySystemType::Digital:
                // Digital carries over the fiat money supply concept; the
                // distinguishing mechanics (electronic settlement, no physical
                // carry) live on the trader side.
                return static_cast<int32_t>(this->moneySupply);

            default:
                return 0;
        }
    }

    /// Total raw coin/bar count across all tiers (unweighted).
    [[nodiscard]] int32_t totalCoinCount() const {
        return this->copperCoinReserves + this->silverCoinReserves + this->goldBarReserves;
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
     * @return Actual amount printed (may be capped). The caller credits the
     *         treasury with it as MoneyFlow::printed(); this only issues.
     */
    CurrencyAmount printMoney(CurrencyAmount amount) {
        if (this->system != MonetarySystemType::FiatMoney
            && this->system != MonetarySystemType::Digital) {
            return 0;  // Only fiat-class systems can issue money
        }
        // Cap at 10% of GDP per turn to prevent instant hyperinflation
        const CurrencyAmount maxPrint = std::max(
            static_cast<CurrencyAmount>(1),
            static_cast<CurrencyAmount>(this->gdp / 10));
        const CurrencyAmount actualPrint = std::min(amount, maxPrint);

        this->moneySupply += actualPrint;
        this->printAmountThisTurn = actualPrint;

        // Direct inflation impact: printed money / GDP
        if (this->gdp > 0) {
            this->inflationRate += static_cast<float>(actualPrint)
                                 / static_cast<float>(this->gdp);
        }
        return actualPrint;
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
    /// `hasTech` answers whether the player has researched a given TechId. It
    /// is a callback rather than a Player& because this header is included by
    /// the component layer and must not depend on the game object.
    ///
    /// Before it existed, `MonetaryTransitionReq::requiredTech` was declared,
    /// populated with Banking and Computers, and read by NOBODY -- a grep for
    /// the field returned only its own declaration. The gate would have let a
    /// civ reach the Gold Standard with no Banking at all; the only thing
    /// keeping the ladder tech-ordered was a hardcoded override elsewhere that
    /// bypassed this whole function.
    template <typename HasTechFn>
    [[nodiscard]] ErrorCode canTransition(MonetarySystemType target,
                                           int32_t cityCount,
                                           HasTechFn hasTech,
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
                if (req.requiredTech.isValid() && !hasTech(req.requiredTech)) {
                    return ErrorCode::InvalidMonetaryTransition;
                }
                // G8: read raw (pre-debasement) strength. Debasement inflates
                // coin counts without adding real silver/gold, so allowing the
                // gate to read post-debasement totals lets a civ clear the
                // threshold by mixing base metal into its coinage rather than
                // actually accumulating reserves.
                const int32_t rawStrength = static_cast<int32_t>(
                    static_cast<float>(this->currencyStrength())
                    * (1.0f - this->debasement.debasementRatio));
                if (rawStrength < req.minCurrencyStrength) {
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
                // Fiat/Digital require GDP rank in top half of players.
                if (target == MonetarySystemType::FiatMoney
                    || target == MonetarySystemType::Digital) {
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
                // Active legal tender coins ARE money. Demonetized coins are commodities.
                this->moneySupply = static_cast<CurrencyAmount>(this->currencyStrength());
                this->goldBackingRatio = 1.0f;
                this->debasement = {};
                break;

            case MonetarySystemType::GoldStandard:
                // Paper notes backed by silver coins + gold bars at 2:1 ratio.
                // Copper coins (if any remain) are demonetized commodities.
                // Reserves are re-synced from city stockpiles every turn, so
                // seeding goldBarReserves here is pointless -- the reserve
                // stress check treats silver + gold as backing, and the
                // designed 0.5 entry ratio is below the stress threshold.
                this->moneySupply = static_cast<CurrencyAmount>(this->currencyStrength()) * 2;
                this->goldBackingRatio = 0.5f;
                this->debasement = {};  // Paper money, debasement no longer applies
                break;

            case MonetarySystemType::FiatMoney:
                // Money is no longer backed by gold. Keep current supply.
                // Gold bars become a commodity (raw material for gold contacts).
                this->goldBackingRatio = 0.0f;
                // Seed money supply if the civ arrives with M == 0 (e.g. after
                // sovereign default, crisis reform, or aggressive gold-buy).
                // Fisher / trust / maxTradeRoutes all assume M > 0.
                if (this->moneySupply < MONEY_SUPPLY_FLOOR) {
                    this->moneySupply = std::max<CurrencyAmount>(
                        static_cast<CurrencyAmount>(this->currencyStrength()) * 2,
                        MONEY_SUPPLY_FLOOR);
                }
                break;

            case MonetarySystemType::Digital:
                // Same ledger semantics as fiat; all settlement is electronic.
                this->goldBackingRatio = 0.0f;
                if (this->moneySupply < MONEY_SUPPLY_FLOOR) {
                    this->moneySupply = std::max<CurrencyAmount>(
                        static_cast<CurrencyAmount>(this->currencyStrength()) * 2,
                        MONEY_SUPPLY_FLOOR);
                }
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
            case MonetarySystemType::Digital:
                // Instant electronic settlement clears small frictions that
                // remained under fiat paper handling.
                baseEfficiency = 1.05f;
                break;
            default:
                break;
        }

        return baseEfficiency;
    }

    /// Maximum number of simultaneous trade routes allowed.
    /// 2026-05-02: bumped tier baselines so early-game civs aren't
    /// permanently locked at 1-2 routes. Audit showed 7191 "at cap"
    /// rejections per 36-sim run when most civs sat in Barter/Commodity
    /// for the first quartile of the game.
    [[nodiscard]] int32_t maxTradeRoutes() const {
        switch (this->system) {
            case MonetarySystemType::Barter:         return 2;
            case MonetarySystemType::CommodityMoney: {
                int32_t strength = this->currencyStrength();
                if (strength < STRENGTH_LOCAL_TRADE)    { return 3; }
                if (strength < STRENGTH_REGIONAL_TRADE) { return 4; }
                if (strength < STRENGTH_INTERNATIONAL)  { return 5; }
                return 6;
            }
            case MonetarySystemType::GoldStandard:
                if (this->moneySupply < MONEY_SUPPLY_FLOOR) { return 2; }
                return 8;
            case MonetarySystemType::FiatMoney:
                if (this->moneySupply < MONEY_SUPPLY_FLOOR) { return 2; }
                return 16;
            case MonetarySystemType::Digital:
                if (this->moneySupply < MONEY_SUPPLY_FLOOR) { return 2; }
                return 22;
            default:                                 return 1;
        }
    }

    /// Metal reserves backing the currency (phase-aware).
    /// In GoldStandard: silver coins + gold bars back the paper currency.
    /// In Fiat: metals are just commodities, no backing.
    [[nodiscard]] CurrencyAmount metalReserves() const {
        return static_cast<CurrencyAmount>(this->currencyStrength());
    }

    /// Gold bar reserves accessor (for gold buy/sell operations).
    [[nodiscard]] CurrencyAmount goldReserves() const {
        return static_cast<CurrencyAmount>(this->goldBarReserves);
    }
};

/**
 * @brief Single chokepoint for all money supply changes. Applies the delta
 *        and enforces the MONEY_SUPPLY_FLOOR for monetized systems.
 *
 * Every path that creates or destroys money (printMoney, remintCurrency,
 * monetizeDebt, counterfeit, currency war, crisis devaluation) must route
 * through this function. Direct `state.moneySupply +=` writes bypass the
 * floor and can leave the supply at 0 or negative, breaking inflation and
 * backing-ratio math irrecoverably.
 *
 * @param state   Player's monetary state.
 * @param delta   Signed change to apply (can be negative).
 * @param reason  Short label for tracing; currently unused at runtime.
 */
inline void adjustMoneySupply(MonetaryStateComponent& state,
                              CurrencyAmount delta,
                              std::string_view /*reason*/) {
    state.moneySupply += delta;
    const CurrencyAmount floor =
        (state.system == MonetarySystemType::Barter)
            ? CurrencyAmount{0}
            : MONEY_SUPPLY_FLOOR;
    if (state.moneySupply < floor) {
        state.moneySupply = floor;
    }
}

} // namespace aoc::sim
