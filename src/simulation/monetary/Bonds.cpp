/**
 * @file Bonds.cpp
 * @brief Government bond issuance, trading, payments, and weaponization.
 */

#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

float computeBondYield(const MonetaryStateComponent& state, bool hasRecentDefault) {
    float baseRate = state.interestRate;

    // Debt premium: higher debt-to-GDP = riskier bonds = higher yield demanded
    float debtPremium = 0.0f;
    if (state.gdp > 0) {
        float debtToGDP = static_cast<float>(state.governmentDebt)
                        / static_cast<float>(state.gdp);
        debtPremium = std::max(0.0f, (debtToGDP - 0.50f)) * 0.10f;
    }

    // Default penalty
    float defaultPenalty = hasRecentDefault ? 0.05f : 0.0f;

    return std::clamp(baseRate + debtPremium + defaultPenalty, 0.01f, 0.30f);
}

ErrorCode issueBond(aoc::game::GameState& gameState,
                    PlayerId issuer, PlayerId buyer,
                    CurrencyAmount principal) {
    if (principal <= 0) {
        return ErrorCode::InvalidArgument;
    }

    aoc::game::Player* issuerPlayer = gameState.player(issuer);
    aoc::game::Player* buyerPlayer  = gameState.player(buyer);
    if (issuerPlayer == nullptr || buyerPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    MonetaryStateComponent& issuerState = issuerPlayer->monetary();
    MonetaryStateComponent& buyerState  = buyerPlayer->monetary();

    // Buyer needs sufficient treasury
    if (buyerState.treasury < principal) {
        return ErrorCode::InsufficientResources;
    }

    // Check if issuer is in default (loans blocked during currency crisis)
    const CurrencyCrisisComponent& issuerCrisis = issuerPlayer->currencyCrisis();
    if (issuerCrisis.areLoansBlocked()) {
        return ErrorCode::InvalidArgument;
    }

    const bool hasDefaulted = issuerCrisis.hasDefaulted;
    float yield = computeBondYield(issuerState, hasDefaulted);

    // Create the bond
    BondIssue bond;
    bond.issuer          = issuer;
    bond.holder          = buyer;
    bond.principal       = principal;
    bond.yieldRate       = yield;
    bond.turnsToMaturity = 10;
    bond.accruedInterest = 0;

    // Transfer cash: buyer pays, issuer receives
    buyerState.treasury  -= principal;
    issuerState.treasury += principal;

    // Record in both portfolios
    issuerPlayer->bonds().issuedBonds.push_back(bond);
    buyerPlayer->bonds().heldBonds.push_back(bond);

    LOG_INFO("Bond issued: player %u -> player %u, principal %lld, yield %.1f%%",
             static_cast<unsigned>(issuer), static_cast<unsigned>(buyer),
             static_cast<long long>(principal), yield * 100.0f);

    return ErrorCode::Ok;
}

ErrorCode dumpBonds(aoc::game::GameState& gameState,
                    PlayerId dumper, PlayerId target) {
    aoc::game::Player* dumperPlayer = gameState.player(dumper);
    aoc::game::Player* targetPlayer = gameState.player(target);
    if (dumperPlayer == nullptr || targetPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    PlayerBondComponent& dumperBonds    = dumperPlayer->bonds();
    MonetaryStateComponent& targetState = targetPlayer->monetary();

    // Sell all bonds held from target at 50% of face value (fire sale)
    CurrencyAmount recoveredValue = 0;
    std::vector<BondIssue>::iterator it = dumperBonds.heldBonds.begin();
    while (it != dumperBonds.heldBonds.end()) {
        if (it->issuer == target) {
            CurrencyAmount saleValue = (it->principal + it->accruedInterest) / 2;
            recoveredValue += saleValue;
            it = dumperBonds.heldBonds.erase(it);
        } else {
            ++it;
        }
    }

    if (recoveredValue <= 0) {
        return ErrorCode::InvalidArgument;  // No bonds to dump
    }

    // Dumper gets 50% back immediately
    dumperPlayer->monetary().treasury += recoveredValue;

    // Effects on target:
    // 1. Bond yields spike (+5% across the board for the target)
    targetState.interestRate = std::min(0.25f, targetState.interestRate + 0.05f);

    // 2. Currency trust drops if on fiat
    CurrencyTrustComponent& targetTrust = targetPlayer->currencyTrust();
    targetTrust.trustScore = std::max(0.0f, targetTrust.trustScore - 0.15f);

    LOG_INFO("Player %u dumped bonds from player %u! Recovered %lld, "
             "target yields spiked, trust damaged",
             static_cast<unsigned>(dumper), static_cast<unsigned>(target),
             static_cast<long long>(recoveredValue));

    return ErrorCode::Ok;
}

void processBondPayments(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& issuerPtr : gameState.players()) {
        if (issuerPtr == nullptr) {
            continue;
        }

        PlayerBondComponent& portfolio   = issuerPtr->bonds();
        MonetaryStateComponent& issuerState = issuerPtr->monetary();

        std::vector<BondIssue>::iterator it = portfolio.issuedBonds.begin();
        while (it != portfolio.issuedBonds.end()) {
            // Accrue interest
            CurrencyAmount interest = static_cast<CurrencyAmount>(
                static_cast<float>(it->principal) * it->yieldRate);
            interest = std::max(static_cast<CurrencyAmount>(1), interest);
            it->accruedInterest += interest;

            --it->turnsToMaturity;

            if (it->turnsToMaturity <= 0) {
                // Bond matured: issuer must pay principal + interest to holder
                CurrencyAmount totalPayment = it->principal + it->accruedInterest;

                if (issuerState.treasury >= totalPayment) {
                    issuerState.treasury -= totalPayment;

                    // Pay the holder
                    aoc::game::Player* holderPlayer = gameState.player(it->holder);
                    if (holderPlayer != nullptr) {
                        holderPlayer->monetary().treasury += totalPayment;

                        // Remove from holder's portfolio
                        PlayerBondComponent& holderBonds = holderPlayer->bonds();
                        for (std::vector<BondIssue>::iterator hIt = holderBonds.heldBonds.begin();
                             hIt != holderBonds.heldBonds.end(); ++hIt) {
                            if (hIt->issuer == issuerPtr->id()
                                && hIt->principal == it->principal) {
                                holderBonds.heldBonds.erase(hIt);
                                break;
                            }
                        }
                    }

                    LOG_INFO("Bond matured: player %u paid %lld to player %u",
                             static_cast<unsigned>(issuerPtr->id()),
                             static_cast<long long>(totalPayment),
                             static_cast<unsigned>(it->holder));
                } else {
                    // Can't pay: sovereign default triggered via crisis system
                    LOG_INFO("Bond default: player %u cannot pay %lld to player %u",
                             static_cast<unsigned>(issuerPtr->id()),
                             static_cast<long long>(totalPayment),
                             static_cast<unsigned>(it->holder));
                }

                it = portfolio.issuedBonds.erase(it);
            } else {
                // Sync the matching bond in the holder's portfolio
                aoc::game::Player* holderPlayer = gameState.player(it->holder);
                if (holderPlayer != nullptr) {
                    for (BondIssue& held : holderPlayer->bonds().heldBonds) {
                        if (held.issuer == issuerPtr->id()
                            && held.principal == it->principal
                            && held.turnsToMaturity == it->turnsToMaturity + 1) {
                            held.accruedInterest = it->accruedInterest;
                            held.turnsToMaturity = it->turnsToMaturity;
                            break;
                        }
                    }
                }
                ++it;
            }
        }
    }
}

// ============================================================================
// Player-to-player IOUs (credit/loans)
// ============================================================================

ErrorCode createIOU(aoc::game::GameState& gameState,
                     PlayerId creditor, PlayerId debtor,
                     CurrencyAmount principal,
                     float interestRate,
                     int32_t termTurns) {
    if (principal <= 0 || creditor == debtor) {
        return ErrorCode::InvalidArgument;
    }

    aoc::game::Player* creditorPlayer = gameState.player(creditor);
    aoc::game::Player* debtorPlayer   = gameState.player(debtor);
    if (creditorPlayer == nullptr || debtorPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    MonetaryStateComponent& creditorState = creditorPlayer->monetary();
    MonetaryStateComponent& debtorState   = debtorPlayer->monetary();

    // Creditor must have the cash
    if (creditorState.treasury < principal) {
        return ErrorCode::InsufficientResources;
    }

    // Transfer cash
    creditorState.treasury -= principal;
    debtorState.treasury   += principal;

    // Create the contract
    IOUContract contract;
    contract.creditor       = creditor;
    contract.debtor         = debtor;
    contract.principal      = principal;
    contract.remaining      = principal;
    contract.interestRate   = interestRate;
    contract.turnsRemaining = termTurns;
    contract.turnsActive    = 0;
    contract.inDefault      = false;

    creditorPlayer->ious().loansGiven.push_back(contract);
    debtorPlayer->ious().loansReceived.push_back(contract);

    LOG_INFO("IOU created: player %u lent %lld to player %u at %.1f%% for %d turns",
             static_cast<unsigned>(creditor),
             static_cast<long long>(principal),
             static_cast<unsigned>(debtor),
             static_cast<double>(interestRate) * 100.0,
             termTurns);

    return ErrorCode::Ok;
}

ErrorCode callInIOU(aoc::game::GameState& gameState,
                     PlayerId creditor, PlayerId debtor) {
    aoc::game::Player* creditorPlayer = gameState.player(creditor);
    aoc::game::Player* debtorPlayer   = gameState.player(debtor);
    if (creditorPlayer == nullptr || debtorPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    MonetaryStateComponent& debtorState   = debtorPlayer->monetary();
    MonetaryStateComponent& creditorState = creditorPlayer->monetary();
    PlayerIOUComponent& creditorIOU = creditorPlayer->ious();
    PlayerIOUComponent& debtorIOU   = debtorPlayer->ious();

    // Call in all IOUs from this debtor
    CurrencyAmount totalDemanded = 0;
    CurrencyAmount totalPaid     = 0;
    bool anyDefault = false;

    std::vector<IOUContract>::iterator it = creditorIOU.loansGiven.begin();
    while (it != creditorIOU.loansGiven.end()) {
        if (it->debtor != debtor) {
            ++it;
            continue;
        }

        totalDemanded += it->remaining;
        CurrencyAmount payment = std::min(debtorState.treasury, it->remaining);
        debtorState.treasury   -= payment;
        creditorState.treasury += payment;
        totalPaid += payment;

        if (payment < it->remaining) {
            it->inDefault = true;
            anyDefault = true;
            it->remaining -= payment;
            ++it;
        } else {
            // Remove matching entry from debtor's loansReceived
            for (std::vector<IOUContract>::iterator dIt = debtorIOU.loansReceived.begin();
                 dIt != debtorIOU.loansReceived.end(); ++dIt) {
                if (dIt->creditor == creditor && dIt->principal == it->principal) {
                    debtorIOU.loansReceived.erase(dIt);
                    break;
                }
            }
            it = creditorIOU.loansGiven.erase(it);
        }
    }

    LOG_INFO("IOU called: player %u demanded %lld from player %u, received %lld%s",
             static_cast<unsigned>(creditor),
             static_cast<long long>(totalDemanded),
             static_cast<unsigned>(debtor),
             static_cast<long long>(totalPaid),
             anyDefault ? " (PARTIAL DEFAULT)" : "");

    // Default damages currency trust
    if (anyDefault) {
        CurrencyTrustComponent& trust = debtorPlayer->currencyTrust();
        trust.trustScore = std::max(0.0f, trust.trustScore - 0.10f);
        return ErrorCode::InsufficientResources;
    }

    return ErrorCode::Ok;
}

void processIOUPayments(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& creditorPtr : gameState.players()) {
        if (creditorPtr == nullptr) {
            continue;
        }

        PlayerIOUComponent& creditorIOU = creditorPtr->ious();

        std::vector<IOUContract>::iterator it = creditorIOU.loansGiven.begin();
        while (it != creditorIOU.loansGiven.end()) {
            ++it->turnsActive;
            --it->turnsRemaining;

            // Accrue interest
            CurrencyAmount interest = static_cast<CurrencyAmount>(
                static_cast<float>(it->remaining) * it->interestRate);
            interest = std::max(static_cast<CurrencyAmount>(1), interest);
            it->remaining += interest;

            // Scheduled payment: (principal / original_term) + interest per turn
            CurrencyAmount scheduledPayment = it->principal / 20 + interest;
            scheduledPayment = std::min(scheduledPayment, it->remaining);

            aoc::game::Player* debtorPlayer   = gameState.player(it->debtor);
            aoc::game::Player* creditorPlayer = gameState.player(creditorPtr->id());

            if (debtorPlayer != nullptr && creditorPlayer != nullptr) {
                MonetaryStateComponent& debtorState   = debtorPlayer->monetary();
                MonetaryStateComponent& creditorState = creditorPlayer->monetary();

                CurrencyAmount actualPayment = std::min(debtorState.treasury, scheduledPayment);
                debtorState.treasury   -= actualPayment;
                creditorState.treasury += actualPayment;
                it->remaining          -= actualPayment;

                if (actualPayment < scheduledPayment) {
                    it->inDefault = true;
                }
            }

            // Loan fully repaid or expired
            if (it->remaining <= 0) {
                // Remove from debtor's loansReceived
                if (debtorPlayer != nullptr) {
                    std::vector<IOUContract>& received = debtorPlayer->ious().loansReceived;
                    for (std::vector<IOUContract>::iterator rIt = received.begin();
                         rIt != received.end(); ++rIt) {
                        if (rIt->creditor == creditorPtr->id()
                            && rIt->principal == it->principal) {
                            received.erase(rIt);
                            break;
                        }
                    }
                }
                it = creditorIOU.loansGiven.erase(it);
            } else {
                // Sync to debtor's copy
                if (debtorPlayer != nullptr) {
                    for (IOUContract& received : debtorPlayer->ious().loansReceived) {
                        if (received.creditor == creditorPtr->id()
                            && received.principal == it->principal) {
                            received.remaining      = it->remaining;
                            received.turnsRemaining = it->turnsRemaining;
                            received.turnsActive    = it->turnsActive;
                            received.inDefault      = it->inDefault;
                            break;
                        }
                    }
                }
                ++it;
            }
        }
    }
}

} // namespace aoc::sim
