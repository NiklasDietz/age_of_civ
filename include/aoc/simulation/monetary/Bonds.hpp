#pragma once

/**
 * @file Bonds.hpp
 * @brief Government bond issuance, trading, and weaponization.
 *
 * Players can issue government bonds to raise cash. Other players (human
 * or AI) can purchase these bonds, creating financial interdependence.
 *
 * Bond mechanics:
 * - Yield = base rate + debt-to-GDP premium. High debt = expensive borrowing.
 * - Maturity: 10 turns. At maturity, issuer pays principal + accumulated interest.
 * - Default: if issuer can't pay at maturity, triggers sovereign default crisis.
 *
 * Strategic use:
 * - Holding 30%+ of another civ's bonds = financial leverage.
 * - Can "dump bonds" to crash their economy (diplomatic action).
 * - Bond dump: floods the market, drives up yields, makes borrowing
 *   unaffordable, can trigger sovereign default cascade.
 * - "Too connected to fail": if everyone holds everyone's bonds,
 *   war becomes economically destructive for the aggressor too.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace aoc::game { class GameState; }

namespace aoc::sim {

struct MonetaryStateComponent;

// ============================================================================
// Bond definition
// ============================================================================

struct BondIssue {
    PlayerId  issuer = INVALID_PLAYER;        ///< Who issued the bond
    PlayerId  holder = INVALID_PLAYER;        ///< Who currently holds it
    CurrencyAmount principal = 0;             ///< Face value
    float    yieldRate = 0.05f;               ///< Annual yield (per turn simplified)
    int32_t  turnsToMaturity = 10;            ///< Turns until principal repayment
    CurrencyAmount accruedInterest = 0;       ///< Interest accumulated so far
};

// ============================================================================
// Per-player bond portfolio (ECS component)
// ============================================================================

struct PlayerBondComponent {
    PlayerId owner = INVALID_PLAYER;

    /// Bonds this player has ISSUED (owes money to holders).
    std::vector<BondIssue> issuedBonds;

    /// Total outstanding bond debt (sum of all issued bond principals).
    [[nodiscard]] CurrencyAmount totalBondDebt() const {
        CurrencyAmount total = 0;
        for (const BondIssue& bond : this->issuedBonds) {
            total += bond.principal + bond.accruedInterest;
        }
        return total;
    }

    /// Bonds this player HOLDS from other issuers (assets).
    std::vector<BondIssue> heldBonds;

    /// Total value of bonds held from a specific issuer.
    [[nodiscard]] CurrencyAmount bondsHeldFrom(PlayerId issuer) const {
        CurrencyAmount total = 0;
        for (const BondIssue& bond : this->heldBonds) {
            if (bond.issuer == issuer) {
                total += bond.principal + bond.accruedInterest;
            }
        }
        return total;
    }

    /// Fraction of an issuer's total bonds that this player holds.
    /// Above 0.3 (30%) grants financial leverage.
    [[nodiscard]] float bondShareOfIssuer(PlayerId issuer,
                                          CurrencyAmount issuerTotalDebt) const {
        if (issuerTotalDebt <= 0) {
            return 0.0f;
        }
        return static_cast<float>(this->bondsHeldFrom(issuer))
             / static_cast<float>(issuerTotalDebt);
    }

    /// Financial leverage threshold: holding 30%+ of someone's bonds.
    static constexpr float LEVERAGE_THRESHOLD = 0.30f;
};

// ============================================================================
// Bond operations
// ============================================================================

/**
 * @brief Compute the current bond yield for an issuer.
 *
 * Yield = base interest rate + debt premium.
 * debt premium = max(0, (debtToGDP - 0.5)) * 0.10
 * Recent default adds +5% penalty.
 *
 * @param state  Issuer's monetary state.
 * @param hasRecentDefault  Whether issuer has defaulted recently.
 * @return Per-turn yield rate.
 */
[[nodiscard]] float computeBondYield(const MonetaryStateComponent& state,
                                     bool hasRecentDefault);

/**
 * @brief Issue a new bond. Creates debt for the issuer, cash for the buyer.
 *
 * @param world    ECS world.
 * @param issuer   Player issuing the bond.
 * @param buyer    Player buying the bond.
 * @param principal  Face value of the bond.
 * @return Ok if successful, InsufficientResources if buyer can't afford it.
 */
[[nodiscard]] ErrorCode issueBond(aoc::game::GameState& gameState,
                                  PlayerId issuer, PlayerId buyer,
                                  CurrencyAmount principal);

/**
 * @brief Dump bonds on the market (sell all bonds held from a target issuer).
 *
 * This is a hostile economic action. Effects:
 * - Target's bond yields spike (makes future borrowing expensive).
 * - Target's currency trust drops (if on fiat).
 * - Can trigger sovereign default if target can't absorb the sell-off.
 *
 * @param world    ECS world.
 * @param dumper   Player selling the bonds.
 * @param target   Issuer whose bonds are being dumped.
 * @return Ok if successful.
 */
[[nodiscard]] ErrorCode dumpBonds(aoc::game::GameState& gameState,
                                  PlayerId dumper, PlayerId target);

/**
 * @brief Process bond maturity and interest accrual for all bonds.
 *
 * Called once per turn. Accrues interest on all outstanding bonds.
 * When a bond matures, the issuer must pay principal + interest to the holder.
 * If issuer can't pay, triggers sovereign default.
 *
 * @param world  ECS world.
 */
void processBondPayments(aoc::game::GameState& gameState);

// ============================================================================
// Player-to-player IOUs (credit/loans)
//
// Unlike bonds (which are government debt instruments tradeable on markets),
// IOUs are direct bilateral loans between two players. They create tight
// economic interdependence:
//
// - Creditor earns interest each turn (passive income)
// - Debtor gets immediate cash to fund wars/expansion
// - Defaulting on IOUs damages diplomatic relations and currency trust
// - Creditor can "call in" IOUs early (forces partial repayment)
// - IOUs can be used as diplomatic leverage ("pay me or I call the loan")
// ============================================================================

struct IOUContract {
    PlayerId  creditor = INVALID_PLAYER;    ///< Who lent the money
    PlayerId  debtor   = INVALID_PLAYER;    ///< Who owes the money
    CurrencyAmount principal = 0;           ///< Original loan amount
    CurrencyAmount remaining = 0;           ///< Outstanding balance
    float    interestRate = 0.05f;          ///< Per-turn interest rate
    int32_t  turnsRemaining = 20;           ///< Turns until full repayment due
    int32_t  turnsActive = 0;               ///< How long the loan has been active
    bool     inDefault = false;             ///< Debtor failed to make a payment
};

/// Per-player IOU tracking (attached to each player entity).
struct PlayerIOUComponent {
    PlayerId owner = INVALID_PLAYER;

    /// IOUs where this player is the CREDITOR (money owed TO us).
    std::vector<IOUContract> loansGiven;

    /// IOUs where this player is the DEBTOR (money WE owe).
    std::vector<IOUContract> loansReceived;

    /// Total amount owed to this player across all IOUs.
    [[nodiscard]] CurrencyAmount totalCreditsOutstanding() const {
        CurrencyAmount total = 0;
        for (const IOUContract& iou : this->loansGiven) {
            total += iou.remaining;
        }
        return total;
    }

    /// Total amount this player owes across all IOUs.
    [[nodiscard]] CurrencyAmount totalDebtsOutstanding() const {
        CurrencyAmount total = 0;
        for (const IOUContract& iou : this->loansReceived) {
            total += iou.remaining;
        }
        return total;
    }
};

/**
 * @brief Create a new IOU (loan) between two players.
 *
 * Creditor pays the principal from their treasury to debtor's treasury.
 * Debtor must repay over time with interest.
 *
 * @param world      ECS world.
 * @param creditor   Player lending money.
 * @param debtor     Player borrowing money.
 * @param principal  Loan amount.
 * @param interestRate  Per-turn interest rate (default 5%).
 * @param termTurns  Loan term in turns (default 20).
 * @return Ok if successful, InsufficientResources if creditor can't afford it.
 */
[[nodiscard]] ErrorCode createIOU(aoc::game::GameState& gameState,
                                   PlayerId creditor, PlayerId debtor,
                                   CurrencyAmount principal,
                                   float interestRate = 0.05f,
                                   int32_t termTurns = 20);

/**
 * @brief Call in an IOU early (demand partial repayment).
 *
 * Creditor demands immediate repayment of remaining balance. Debtor pays
 * what they can from treasury. Any unpaid remainder is marked as default.
 * Defaulting damages diplomatic relations and currency trust.
 *
 * @param world      ECS world.
 * @param creditor   Player calling the loan.
 * @param debtor     Player whose loan is called.
 * @return Ok if debtor fully repaid, InsufficientResources if partial/default.
 */
[[nodiscard]] ErrorCode callInIOU(aoc::game::GameState& gameState,
                                   PlayerId creditor, PlayerId debtor);

/**
 * @brief Process IOU interest accrual and scheduled repayments.
 *
 * Called once per turn. For each active IOU:
 * - Accrues interest on remaining balance.
 * - Debtor makes scheduled payment (principal / termTurns + interest).
 * - If debtor can't pay, marks IOU as in default.
 * - Expired IOUs (turnsRemaining <= 0) demand full repayment.
 *
 * @param world  ECS world.
 */
void processIOUPayments(aoc::game::GameState& gameState);

} // namespace aoc::sim
