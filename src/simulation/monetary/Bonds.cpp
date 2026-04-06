/**
 * @file Bonds.cpp
 * @brief Government bond issuance, trading, payments, and weaponization.
 */

#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/ecs/World.hpp"
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

ErrorCode issueBond(aoc::ecs::World& world,
                    PlayerId issuer, PlayerId buyer,
                    CurrencyAmount principal) {
    if (principal <= 0) {
        return ErrorCode::InvalidArgument;
    }

    // Find both players' bond and monetary components
    PlayerBondComponent* issuerBonds = nullptr;
    PlayerBondComponent* buyerBonds = nullptr;
    MonetaryStateComponent* issuerState = nullptr;
    MonetaryStateComponent* buyerState = nullptr;

    aoc::ecs::ComponentPool<PlayerBondComponent>* bondPool =
        world.getPool<PlayerBondComponent>();
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();

    if (bondPool == nullptr || monetaryPool == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    for (uint32_t i = 0; i < bondPool->size(); ++i) {
        if (bondPool->data()[i].owner == issuer) { issuerBonds = &bondPool->data()[i]; }
        if (bondPool->data()[i].owner == buyer)  { buyerBonds = &bondPool->data()[i]; }
    }
    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        if (monetaryPool->data()[i].owner == issuer) { issuerState = &monetaryPool->data()[i]; }
        if (monetaryPool->data()[i].owner == buyer)  { buyerState = &monetaryPool->data()[i]; }
    }

    if (issuerBonds == nullptr || buyerBonds == nullptr
        || issuerState == nullptr || buyerState == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // Buyer needs sufficient treasury
    if (buyerState->treasury < principal) {
        return ErrorCode::InsufficientResources;
    }

    // Check if issuer is in default (can't issue during cooldown)
    const aoc::ecs::ComponentPool<CurrencyCrisisComponent>* crisisPool =
        world.getPool<CurrencyCrisisComponent>();
    if (crisisPool != nullptr) {
        for (uint32_t i = 0; i < crisisPool->size(); ++i) {
            if (crisisPool->data()[i].owner == issuer
                && crisisPool->data()[i].areLoansBlocked()) {
                return ErrorCode::InvalidArgument;
            }
        }
    }

    // Compute yield
    bool hasDefaulted = false;
    if (crisisPool != nullptr) {
        for (uint32_t i = 0; i < crisisPool->size(); ++i) {
            if (crisisPool->data()[i].owner == issuer && crisisPool->data()[i].hasDefaulted) {
                hasDefaulted = true;
                break;
            }
        }
    }
    float yield = computeBondYield(*issuerState, hasDefaulted);

    // Create the bond
    BondIssue bond;
    bond.issuer = issuer;
    bond.holder = buyer;
    bond.principal = principal;
    bond.yieldRate = yield;
    bond.turnsToMaturity = 10;
    bond.accruedInterest = 0;

    // Transfer cash: buyer pays, issuer receives
    buyerState->treasury -= principal;
    issuerState->treasury += principal;

    // Record in both portfolios
    issuerBonds->issuedBonds.push_back(bond);
    buyerBonds->heldBonds.push_back(bond);

    LOG_INFO("Bond issued: player %u -> player %u, principal %lld, yield %.1f%%",
             static_cast<unsigned>(issuer), static_cast<unsigned>(buyer),
             static_cast<long long>(principal), yield * 100.0f);

    return ErrorCode::Ok;
}

ErrorCode dumpBonds(aoc::ecs::World& world,
                    PlayerId dumper, PlayerId target) {
    aoc::ecs::ComponentPool<PlayerBondComponent>* bondPool =
        world.getPool<PlayerBondComponent>();
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();

    if (bondPool == nullptr || monetaryPool == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    PlayerBondComponent* dumperBonds = nullptr;
    MonetaryStateComponent* targetState = nullptr;

    for (uint32_t i = 0; i < bondPool->size(); ++i) {
        if (bondPool->data()[i].owner == dumper) { dumperBonds = &bondPool->data()[i]; }
    }
    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        if (monetaryPool->data()[i].owner == target) { targetState = &monetaryPool->data()[i]; }
    }

    if (dumperBonds == nullptr || targetState == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // Sell all bonds held from target at 50% of face value (fire sale)
    CurrencyAmount recoveredValue = 0;
    auto it = dumperBonds->heldBonds.begin();
    while (it != dumperBonds->heldBonds.end()) {
        if (it->issuer == target) {
            CurrencyAmount saleValue = (it->principal + it->accruedInterest) / 2;
            recoveredValue += saleValue;
            it = dumperBonds->heldBonds.erase(it);
        } else {
            ++it;
        }
    }

    if (recoveredValue <= 0) {
        return ErrorCode::InvalidArgument;  // No bonds to dump
    }

    // Dumper gets 50% back immediately
    MonetaryStateComponent* dumperState = nullptr;
    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        if (monetaryPool->data()[i].owner == dumper) {
            dumperState = &monetaryPool->data()[i];
            break;
        }
    }
    if (dumperState != nullptr) {
        dumperState->treasury += recoveredValue;
    }

    // Effects on target:
    // 1. Bond yields spike (+5% across the board for the target)
    targetState->interestRate = std::min(0.25f, targetState->interestRate + 0.05f);

    // 2. Currency trust drops if on fiat
    aoc::ecs::ComponentPool<CurrencyTrustComponent>* trustPool =
        world.getPool<CurrencyTrustComponent>();
    if (trustPool != nullptr) {
        for (uint32_t i = 0; i < trustPool->size(); ++i) {
            if (trustPool->data()[i].owner == target) {
                trustPool->data()[i].trustScore =
                    std::max(0.0f, trustPool->data()[i].trustScore - 0.15f);
                break;
            }
        }
    }

    LOG_INFO("Player %u dumped bonds from player %u! Recovered %lld, "
             "target yields spiked, trust damaged",
             static_cast<unsigned>(dumper), static_cast<unsigned>(target),
             static_cast<long long>(recoveredValue));

    return ErrorCode::Ok;
}

void processBondPayments(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<PlayerBondComponent>* bondPool =
        world.getPool<PlayerBondComponent>();
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();

    if (bondPool == nullptr || monetaryPool == nullptr) {
        return;
    }

    // Process each player's issued bonds
    for (uint32_t p = 0; p < bondPool->size(); ++p) {
        PlayerBondComponent& portfolio = bondPool->data()[p];

        // Find issuer's monetary state
        MonetaryStateComponent* issuerState = nullptr;
        for (uint32_t m = 0; m < monetaryPool->size(); ++m) {
            if (monetaryPool->data()[m].owner == portfolio.owner) {
                issuerState = &monetaryPool->data()[m];
                break;
            }
        }
        if (issuerState == nullptr) {
            continue;
        }

        auto it = portfolio.issuedBonds.begin();
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

                if (issuerState->treasury >= totalPayment) {
                    // Pay the holder
                    issuerState->treasury -= totalPayment;

                    // Find holder and pay them
                    for (uint32_t h = 0; h < monetaryPool->size(); ++h) {
                        if (monetaryPool->data()[h].owner == it->holder) {
                            monetaryPool->data()[h].treasury += totalPayment;
                            break;
                        }
                    }

                    // Remove from holder's portfolio too
                    for (uint32_t h = 0; h < bondPool->size(); ++h) {
                        if (bondPool->data()[h].owner == it->holder) {
                            auto& held = bondPool->data()[h].heldBonds;
                            for (auto hIt = held.begin(); hIt != held.end(); ++hIt) {
                                if (hIt->issuer == portfolio.owner
                                    && hIt->principal == it->principal) {
                                    held.erase(hIt);
                                    break;
                                }
                            }
                            break;
                        }
                    }

                    LOG_INFO("Bond matured: player %u paid %lld to player %u",
                             static_cast<unsigned>(portfolio.owner),
                             static_cast<long long>(totalPayment),
                             static_cast<unsigned>(it->holder));
                } else {
                    // Can't pay: this will trigger sovereign default via crisis system
                    LOG_INFO("Bond default: player %u cannot pay %lld to player %u",
                             static_cast<unsigned>(portfolio.owner),
                             static_cast<long long>(totalPayment),
                             static_cast<unsigned>(it->holder));
                }

                it = portfolio.issuedBonds.erase(it);
            } else {
                // Also update the matching bond in the holder's portfolio
                for (uint32_t h = 0; h < bondPool->size(); ++h) {
                    if (bondPool->data()[h].owner == it->holder) {
                        for (BondIssue& held : bondPool->data()[h].heldBonds) {
                            if (held.issuer == portfolio.owner
                                && held.principal == it->principal
                                && held.turnsToMaturity == it->turnsToMaturity + 1) {
                                held.accruedInterest = it->accruedInterest;
                                held.turnsToMaturity = it->turnsToMaturity;
                                break;
                            }
                        }
                        break;
                    }
                }
                ++it;
            }
        }
    }
}

} // namespace aoc::sim
