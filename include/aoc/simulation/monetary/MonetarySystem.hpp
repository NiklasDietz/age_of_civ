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
 * In the CommodityMoney stage, the people hold private specie (coin).
 * Currency strength and trade efficiency are based on the pool of
 * private specie the civ holds. Gold Standard issues paper notes backed
 * by specie reserves. Fiat Money removes the gold backing but requires
 * trust from trade partners to be accepted (see CurrencyTrust.hpp).
 *
 * Which good serves as money is tracked by `moneyGood` (a good ID), set
 * by requestSetMoneyGood in Phase C. In Phase B, moneyGood = NO_MONEY_GOOD.
 *
 * Transitions are one-way (no going back to barter from fiat).
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
    Barter,         ///< No currency. Trade is direct good-for-good.
    CommodityMoney, ///< Metal coins ARE money. Value = metal's intrinsic value.
    GoldStandard,   ///< Paper currency backed by gold bar reserves at fixed rate.
    FiatMoney,      ///< Unbacked government currency. Full monetary control.
    Digital,        ///< Electronic settlement. Requires compute + power. No physical carry.

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
    case MonetarySystemType::Barter:
        return 0; // no money; swap goods only
    case MonetarySystemType::CommodityMoney:
        return 2; // metal coin chests
    case MonetarySystemType::GoldStandard:
        return 0; // paper notes; bullion stays in vault
    case MonetarySystemType::FiatMoney:
        return 0; // paper, negligible
    case MonetarySystemType::Digital:
        return 0; // nothing carried
    default:
        return 0;
    }
}

/// Whether gold physically rides with traders (vulnerable to pillage) on this tier.

[[nodiscard]] constexpr std::string_view monetarySystemName(MonetarySystemType type) {
    switch (type) {
    case MonetarySystemType::Barter:
        return "Barter";
    case MonetarySystemType::CommodityMoney:
        return "Commodity Money";
    case MonetarySystemType::GoldStandard:
        return "Gold Standard";
    case MonetarySystemType::FiatMoney:
        return "Fiat Money";
    case MonetarySystemType::Digital:
        return "Digital";
    default:
        return "Unknown";
    }
}

/// Good ID meaning "no good is money yet". Fits in uint8_t since GOOD_COUNT=167 < 256.
inline constexpr uint8_t NO_MONEY_GOOD = 0xFF;

/// Turns a civ must keep a money good before it may elect another. Money is
/// the good everyone else expects to be paid in, so switching is costly and
/// rare; without a dwell the AI could thrash between two near-equal goods
/// every turn and no good would ever accumulate the acceptance that makes it
/// money in the first place.
inline constexpr int32_t MONEY_GOOD_DWELL_TURNS = 20;

/// Regimes whose people hold paper: the state pays in notes and taxes them
/// back, and a trusted pair settles trade in them (plan 2.6).
[[nodiscard]] constexpr bool notesInUse(MonetarySystemType type) {
    return type == MonetarySystemType::GoldStandard || type == MonetarySystemType::FiatMoney ||
           type == MonetarySystemType::Digital;
}

/// Regimes whose money is the state's note alone, with no commodity behind it.
[[nodiscard]] constexpr bool isFiatClass(MonetarySystemType type) {
    return type == MonetarySystemType::FiatMoney || type == MonetarySystemType::Digital;
}

/// Currency strength thresholds for trade efficiency tiers.
/// A civ with 100 units of private specie trades as well as one with 4x the reserves.
inline constexpr int32_t STRENGTH_LOCAL_TRADE    = 3;   ///< Minimal coinage
inline constexpr int32_t STRENGTH_REGIONAL_TRADE = 25;  ///< Regional commerce
inline constexpr int32_t STRENGTH_INTERNATIONAL  = 100; ///< Full international trade

/// Currency strength to trade efficiency mapping.
/// Based on total metal-weighted coin value, not specific metal type.
[[nodiscard]] inline float currencyStrengthTradeEfficiency(int32_t currencyStrength) {
    if (currencyStrength < STRENGTH_LOCAL_TRADE) {
        return 0.50f; // Barter-equivalent
    }
    if (currencyStrength < STRENGTH_REGIONAL_TRADE) {
        // Linear interpolation 0.65 to 0.80
        float t = static_cast<float>(currencyStrength - STRENGTH_LOCAL_TRADE) /
                  static_cast<float>(STRENGTH_REGIONAL_TRADE - STRENGTH_LOCAL_TRADE);
        return 0.65f + t * 0.15f;
    }
    if (currencyStrength < STRENGTH_INTERNATIONAL) {
        // Linear interpolation 0.80 to 0.95
        float t = static_cast<float>(currencyStrength - STRENGTH_REGIONAL_TRADE) /
                  static_cast<float>(STRENGTH_INTERNATIONAL - STRENGTH_REGIONAL_TRADE);
        return 0.80f + t * 0.15f;
    }
    return 0.95f; // Full international trade efficiency
}

// ============================================================================
// Transition requirements
// ============================================================================

/// What a player needs to transition to the next monetary stage.
struct MonetaryTransitionReq {
    MonetarySystemType target;
    TechId requiredTech;         ///< Tech prerequisite (INVALID = none)
    int32_t minCurrencyStrength; ///< Minimum currency strength (metal-weighted coin value)
    int32_t minCityCount;        ///< Minimum number of cities
    int32_t minTurnsInCurrent;   ///< Minimum turns spent in current system
    int32_t minTradePartners;    ///< Minimum active trade partners
    float maxInflation;          ///< Maximum inflation rate allowed (for stability check)
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
    {MonetarySystemType::CommodityMoney, TechId{}, 100, 1, 0, 0, 1.0f},
    // Commodity -> Gold Standard: need banking tech, moderate reserves, 2 cities.
    // 20 copper coins (strength 20) or 4 silver coins is achievable before
    // copper ore depletes (~80 turns of mining at 1 ore/turn).
    {MonetarySystemType::GoldStandard, TechId{9}, 20, 2, 0, 0, 1.0f},
    // Gold Standard -> Fiat: Banking (TechId{9}). Printing is TechId{55} and
    // Economics TechId{13}; neither is checked by this row.
    // Lowered currency strength requirement. Needs 2 live trade partners (the
    // trade volume that makes metal coins impractical, like Song Dynasty
    // Sichuan; the threshold was 3, which seed-42 maps fence most land off
    // from). Inflation under 5%: must demonstrate monetary discipline first.
    // requestSetMonetaryRegime adds Printing or Economics on top of Banking.
    {MonetarySystemType::FiatMoney, TechId{9}, 75, 2, 5, 2, 0.05f},
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
    {MonetarySystemType::Digital, TechId{16}, 200, 3, 10, 4, 0.10f},
}};

// ============================================================================
// Gate diagnostics (AOC_DUMP_MONETARY_GATES)
// ============================================================================

/// Which clause refused a monetary transition.
///
/// Every gate in canTransition returns the same ErrorCode, so a refusal
/// carries no reason: nothing could say whether fiat was out of reach for want
/// of tech, partners, calm prices or a top-half economy, and a motive for fiat
/// is worthless if the gate is shut for some other cause. The enumerator is
/// recorded beside the refusal; what canTransition returns is unchanged.
///
/// The last four are the request's own pre-gates (requestSetMonetaryRegime),
/// which return BEFORE canTransition runs. Without them a stage blocked there
/// would read as "never refused" rather than "never asked".
enum class GateRefusal : uint8_t {
    NotNextStage = 0, ///< target is not the stage after the current one
    Tech,             ///< the row's requiredTech is not researched
    CurrencyStrength, ///< debasement-discounted strength under the row minimum
    CityCount,        ///< fewer cities than the row wants
    TurnsInCurrent,   ///< not long enough in the current system
    TradePartners,    ///< too few live trade partners
    Inflation,        ///< prices above the row's ceiling
    NoTableRow,       ///< no MONETARY_TRANSITIONS row for the target at all
    PaperTech,        ///< pre-gate: fiat without Printing or Economics

    Count
};

inline constexpr std::array<std::string_view, static_cast<std::size_t>(GateRefusal::Count)>
    GATE_REFUSAL_NAMES = {"notNextStage", "tech",      "strength",   "cities",   "turnsIn",
                          "partners",     "inflation", "noTableRow", "paperTech"};

/// One run's gate outcomes, by target stage. Cumulative, never reset, and
/// written only while the dump is enabled, so an unset environment costs one
/// predictable branch per call and nothing else. Same opt-in-by-env-var shape
/// as AOC_DUMP_ECONOMY. Written from the turn loop, which is single-threaded.
struct MonetaryGateCounters {
    static constexpr std::size_t STAGES  = static_cast<std::size_t>(MonetarySystemType::Count);
    static constexpr std::size_t REASONS = static_cast<std::size_t>(GateRefusal::Count);

    std::array<std::array<int64_t, REASONS>, STAGES> refused{};
    std::array<int64_t, STAGES> asked{};  ///< canTransition evaluations
    std::array<int64_t, STAGES> passed{}; ///< of those, the ones it allowed
};

[[nodiscard]] inline MonetaryGateCounters& monetaryGateCounters() {
    static MonetaryGateCounters counters;
    return counters;
}

/// True when AOC_DUMP_MONETARY_GATES is set in the environment. Read once.
[[nodiscard]] inline bool monetaryGateDumpEnabled() {
    static const bool enabled = std::getenv("AOC_DUMP_MONETARY_GATES") != nullptr;
    return enabled;
}

/// Stage index, or STAGES for anything outside the ladder (the request accepts
/// a target from outside it, and refuses it).
[[nodiscard]] inline std::size_t gateStageIndex(MonetarySystemType target) {
    const std::size_t idx = static_cast<std::size_t>(target);
    return idx < MonetaryGateCounters::STAGES ? idx : MonetaryGateCounters::STAGES;
}

inline void recordGateAsked(MonetarySystemType target) {
    if (!monetaryGateDumpEnabled()) {
        return;
    }
    const std::size_t idx = gateStageIndex(target);
    if (idx >= MonetaryGateCounters::STAGES) {
        return;
    }
    monetaryGateCounters().asked[idx] += 1;
}

inline void recordGatePassed(MonetarySystemType target) {
    if (!monetaryGateDumpEnabled()) {
        return;
    }
    const std::size_t idx = gateStageIndex(target);
    if (idx >= MonetaryGateCounters::STAGES) {
        return;
    }
    monetaryGateCounters().passed[idx] += 1;
}

inline void recordGateRefusal(MonetarySystemType target, GateRefusal why) {
    if (!monetaryGateDumpEnabled()) {
        return;
    }
    const std::size_t idx = gateStageIndex(target);
    if (idx >= MonetaryGateCounters::STAGES) {
        return;
    }
    monetaryGateCounters().refused[idx][static_cast<std::size_t>(why)] += 1;
}

/// Print the run's gate outcomes to stderr. A no-op unless the dump is on.
///
/// `asked` counts canTransition evaluations, so the four pre-gate reasons can
/// exceed it: those refusals never reach the table.
inline void dumpMonetaryGates() {
    if (!monetaryGateDumpEnabled()) {
        return;
    }
    const MonetaryGateCounters& counters = monetaryGateCounters();
    for (std::size_t stage = 0; stage < MonetaryGateCounters::STAGES; ++stage) {
        const MonetarySystemType target = static_cast<MonetarySystemType>(stage);
        int64_t refusedTotal            = 0;
        for (const int64_t count : counters.refused[stage]) {
            refusedTotal += count;
        }
        if (counters.asked[stage] == 0 && refusedTotal == 0) {
            continue;
        }
        const std::string_view name = monetarySystemName(target);
        std::fprintf(stderr, "[monetarygate] -> %.*s asked=%lld passed=%lld refused=%lld",
                     static_cast<int>(name.size()), name.data(),
                     static_cast<long long>(counters.asked[stage]),
                     static_cast<long long>(counters.passed[stage]),
                     static_cast<long long>(refusedTotal));
        if (refusedTotal > counters.asked[stage]) {
            // The pre-gate refusals recorded before canTransition is reached
            // (no Mint, no bullion, a bad tier, no press) have no matching ask,
            // so this line is not an accounting error.
            std::fprintf(stderr, " (incl. pre-gate)");
        }
        for (std::size_t reason = 0; reason < MonetaryGateCounters::REASONS; ++reason) {
            if (counters.refused[stage][reason] == 0) {
                continue;
            }
            std::fprintf(stderr, " %.*s=%lld", static_cast<int>(GATE_REFUSAL_NAMES[reason].size()),
                         GATE_REFUSAL_NAMES[reason].data(),
                         static_cast<long long>(counters.refused[stage][reason]));
        }
        std::fprintf(stderr, "\n");
    }
}

// ============================================================================
// Debasement state
// ============================================================================

struct DebasementState {
    float debasementRatio     = 0.0f;  ///< 0.0 = pure coins, up to 0.5 = 50% base metal mixed in
    int32_t turnsDebased      = 0;     ///< How many turns since last debasement (for discovery)
    bool discoveredByPartners = false; ///< Once discovered, trade penalty applies
};

// ============================================================================
// Per-player monetary state (ECS component)
// ============================================================================

/// Minimum money supply in any monetized system. Fisher / velocity / backing
/// math divide by `moneySupply`; a zero or negative value produces undefined
/// state that never recovers. Barter keeps M == 0 (no currency exists).
inline constexpr CurrencyAmount MONEY_SUPPLY_FLOOR = 1;

struct MonetaryStateComponent {
    PlayerId owner            = INVALID_PLAYER;
    MonetarySystemType system = MonetarySystemType::Barter;

    // -- Money supply (paper/fiat currency in GoldStandard/Fiat) --
    /// Total currency in circulation: DERIVED, treasury + private specie +
    /// private notes, set every turn by the coin sweep (plan 2.6). The
    /// Fisher path reads its growth; nothing else should write it.
    CurrencyAmount moneySupply = 0;
    TreasuryAccount treasury; ///< Government cash; see TreasuryAccount

    // -- Private money (v34) --
    CurrencyAmount privateSpecie = 0; ///< Coin in private hands, civ-wide
    CurrencyAmount privateNotes  = 0; ///< Paper in private hands once notes are issued
    CurrencyAmount bullion       = 0; ///< Specie held before coinage is adopted
    /// Good ID elected as money by this civ (Phase C: requestSetMoneyGood).
    /// NO_MONEY_GOOD (0xFF) while no good has been chosen. Fits in uint8_t
    /// since GOOD_COUNT=167 < 256.
    uint8_t moneyGood = NO_MONEY_GOOD;
    /// Metal backing per unit of circulating money, gold standard only. An
    /// OUTPUT: measured each turn as reserves over money supply. It must not be
    /// fed back into the money supply that defines it -- see
    /// GOLD_STANDARD_NOTE_ISSUE.
    Percentage goldBackingRatio = 1.0f;

    // -- Inflation --
    Percentage inflationRate = 0.0f; ///< Current per-turn CPI change
    Percentage priceLevel    = 1.0f; ///< Cumulative price level (1.0 = base)

    // -- Central bank controls (GoldStandard / Fiat only) --
    Percentage interestRate       = 0.05f;
    Percentage reserveRequirement = 0.10f;

    // -- Output allocation slider (gold/science/luxury split) --
    // Fraction of city output allocated to each purpose. Should sum to ~1.0.
    // Gold: goes to treasury. Science: boosts research. Luxury: boosts happiness.
    Percentage goldAllocation    = 0.70f; ///< 70% to treasury
    Percentage scienceAllocation = 0.20f; ///< 20% bonus to science
    Percentage luxuryAllocation  = 0.10f; ///< 10% converted to happiness amenities

    // -- Fiscal policy --
    Percentage taxRate                = 0.15f;
    CurrencyAmount governmentSpending = 0;
    CurrencyAmount governmentDebt     = 0;
    CurrencyAmount taxRevenue         = 0;

    CurrencyAmount deficit = 0;

    // -- Derived stats --
    CurrencyAmount gdp         = 0;
    Percentage velocityOfMoney = 1.0f;

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

    /// Turns since this civ last changed its money good. A people does not
    /// re-price everything it owns on a whim, so requestSetMoneyGood enforces
    /// MONEY_GOOD_DWELL_TURNS between changes; the first adoption is free
    /// because the counter starts above the dwell.
    int32_t turnsWithCurrentMoneyGood = MONEY_GOOD_DWELL_TURNS;

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
    bool redemptionRunActive = false;

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

    /// Currency strength: the pool of private money this regime runs on.
    [[nodiscard]] int32_t currencyStrength() const { return this->strengthUnder(this->system); }

    /// Currency strength as it would be measured under an arbitrary regime.
    /// A civ is gated on the measure the TARGET regime runs on, not the source.
    [[nodiscard]] int32_t strengthUnder(MonetarySystemType regime) const {
        switch (regime) {
        case MonetarySystemType::Barter:
        case MonetarySystemType::CommodityMoney:
            // Barter civs hold metal in `bullion` (pre-coinage pool); on
            // adopting CommodityMoney it all moves to privateSpecie. Include
            // both so the gate reads the same pool regardless of which side of
            // the transition the civ is on.
            return static_cast<int32_t>(std::max<CurrencyAmount>(0, this->privateSpecie) +
                                        std::max<CurrencyAmount>(0, this->bullion));

        case MonetarySystemType::GoldStandard:
            return static_cast<int32_t>(std::max<CurrencyAmount>(0, this->privateSpecie) +
                                        std::max<CurrencyAmount>(0, this->privateNotes));

        case MonetarySystemType::FiatMoney:
        case MonetarySystemType::Digital:
            return static_cast<int32_t>(this->moneySupply);

        default:
            return 0;
        }
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
     * @param amount  How much to print. The turn's issues together are capped
     *                at 10% of GDP, so a second call in the same turn gets only
     *                what the first left.
     * @return Actual amount printed (may be capped). The caller credits the
     *         treasury with it as MoneyFlow::printed(); this only issues. The
     *         inflation it causes is read from printAmountThisTurn by
     *         computeInflation, the one writer of inflationRate.
     */
    CurrencyAmount printMoney(CurrencyAmount amount) {
        if (this->system != MonetarySystemType::FiatMoney &&
            this->system != MonetarySystemType::Digital) {
            return 0; // Only fiat-class systems can issue money
        }
        const CurrencyAmount maxPrint =
            std::max(static_cast<CurrencyAmount>(1), static_cast<CurrencyAmount>(this->gdp / 10));
        const CurrencyAmount room = std::max<CurrencyAmount>(0, maxPrint - this->printAmountThisTurn);
        const CurrencyAmount actualPrint = std::clamp<CurrencyAmount>(amount, 0, room);

        this->printAmountThisTurn += actualPrint; // the supply follows the pools
        return actualPrint;
    }

    // ========================================================================
    // Transition logic
    // ========================================================================

    /**
     * @brief Check if the player can transition to a target monetary system.
     * @param cityCount  Number of cities the player owns.
     * @param tradePartnerCount  Number of active trade partners.
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
    [[nodiscard]] ErrorCode canTransition(MonetarySystemType target, int32_t cityCount,
                                          HasTechFn hasTech, int32_t tradePartnerCount = 0) const {
        recordGateAsked(target);

        // Must be the next stage in sequence
        uint8_t currentOrd = static_cast<uint8_t>(this->system);
        uint8_t targetOrd  = static_cast<uint8_t>(target);
        if (targetOrd != currentOrd + 1) {
            recordGateRefusal(target, GateRefusal::NotNextStage);
            return ErrorCode::InvalidMonetaryTransition;
        }

        for (const MonetaryTransitionReq& req : MONETARY_TRANSITIONS) {
            if (req.target == target) {
                if (req.requiredTech.isValid() && !hasTech(req.requiredTech)) {
                    recordGateRefusal(target, GateRefusal::Tech);
                    return ErrorCode::InvalidMonetaryTransition;
                }
                // G8: read raw (pre-debasement) strength. Debasement inflates
                // coin counts without adding real silver/gold, so allowing the
                // gate to read post-debasement totals lets a civ clear the
                // threshold by mixing base metal into its coinage rather than
                // actually accumulating reserves.
                const int32_t rawStrength =
                    static_cast<int32_t>(static_cast<float>(this->strengthUnder(target)) *
                                         (1.0f - this->debasement.debasementRatio));
                if (rawStrength < req.minCurrencyStrength) {
                    recordGateRefusal(target, GateRefusal::CurrencyStrength);
                    return ErrorCode::InvalidMonetaryTransition;
                }
                if (cityCount < req.minCityCount) {
                    recordGateRefusal(target, GateRefusal::CityCount);
                    return ErrorCode::InvalidMonetaryTransition;
                }
                if (this->turnsInCurrentSystem < req.minTurnsInCurrent) {
                    recordGateRefusal(target, GateRefusal::TurnsInCurrent);
                    return ErrorCode::InvalidMonetaryTransition;
                }
                if (tradePartnerCount < req.minTradePartners) {
                    recordGateRefusal(target, GateRefusal::TradePartners);
                    return ErrorCode::InvalidMonetaryTransition;
                }
                if (this->inflationRate > req.maxInflation) {
                    recordGateRefusal(target, GateRefusal::Inflation);
                    return ErrorCode::InvalidMonetaryTransition;
                }
                recordGatePassed(target);
                return ErrorCode::Ok;
            }
        }
        recordGateRefusal(target, GateRefusal::NoTableRow);
        return ErrorCode::InvalidMonetaryTransition;
    }

    /**
     * @brief Execute the transition to a new monetary system.
     *
     * Sets initial values for the new system. Assumes canTransition() returned Ok.
     */
    void transitionTo(MonetarySystemType target) {
        this->system               = target;
        this->turnsInCurrentSystem = 0;

        switch (target) {
        // The money supply is derived from the pools every turn (2.6), so
        // no stage seeds it here.
        case MonetarySystemType::CommodityMoney:
            this->goldBackingRatio = 1.0f;
            this->debasement       = {};
            break;

        case MonetarySystemType::GoldStandard:
            // Notes issued one for one against the people's coin
            // (requestSetMonetaryRegime does the issue): full backing on
            // entry, eroded by whatever specie leaves the country.
            this->goldBackingRatio = 1.0f;
            this->debasement       = {}; // Paper money, debasement no longer applies
            break;

        case MonetarySystemType::FiatMoney:
        case MonetarySystemType::Digital:
            // Money is no longer backed by gold, and the note is the money:
            // the commodity this civ priced in is no longer anyone's money.
            // Both the request and the crisis suspension come through here.
            this->goldBackingRatio          = 0.0f;
            this->moneyGood                 = NO_MONEY_GOOD;
            this->turnsWithCurrentMoneyGood = 0;
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
        case MonetarySystemType::Barter:
            return 2;
        case MonetarySystemType::CommodityMoney: {
            int32_t strength = this->currencyStrength();
            if (strength < STRENGTH_LOCAL_TRADE) {
                return 3;
            }
            if (strength < STRENGTH_REGIONAL_TRADE) {
                return 4;
            }
            if (strength < STRENGTH_INTERNATIONAL) {
                return 5;
            }
            return 6;
        }
        case MonetarySystemType::GoldStandard:
            if (this->moneySupply < MONEY_SUPPLY_FLOOR) {
                return 2;
            }
            return 8;
        case MonetarySystemType::FiatMoney:
            if (this->moneySupply < MONEY_SUPPLY_FLOOR) {
                return 2;
            }
            return 16;
        case MonetarySystemType::Digital:
            if (this->moneySupply < MONEY_SUPPLY_FLOOR) {
                return 2;
            }
            return 22;
        default:
            return 1;
        }
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
inline void adjustMoneySupply(MonetaryStateComponent& state, CurrencyAmount delta,
                              std::string_view /*reason*/) {
    state.moneySupply += delta;
    const CurrencyAmount floor =
        (state.system == MonetarySystemType::Barter) ? CurrencyAmount{0} : MONEY_SUPPLY_FLOOR;
    if (state.moneySupply < floor) {
        state.moneySupply = floor;
    }
}

} // namespace aoc::sim
