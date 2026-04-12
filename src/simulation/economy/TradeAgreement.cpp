/**
 * @file TradeAgreement.cpp
 * @brief Trade agreements, free trade zones, and customs unions.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/economy/TradeAgreement.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

ErrorCode proposeBilateralDeal(aoc::game::GameState& gameState,
                                 PlayerId proposer, PlayerId partner) {
    if (proposer == partner) {
        return ErrorCode::InvalidArgument;
    }

    aoc::game::Player* proposerPlayer = gameState.player(proposer);
    aoc::game::Player* partnerPlayer  = gameState.player(partner);
    if (proposerPlayer == nullptr || partnerPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    PlayerTradeAgreementsComponent& proposerComp = proposerPlayer->tradeAgreements();
    PlayerTradeAgreementsComponent& partnerComp  = partnerPlayer->tradeAgreements();

    // Check if deal already exists
    for (const TradeAgreementDef& existing : proposerComp.agreements) {
        if (existing.type == TradeAgreementType::BilateralDeal && existing.isActive) {
            for (PlayerId member : existing.members) {
                if (member == partner) {
                    return ErrorCode::InvalidArgument;
                }
            }
        }
    }

    TradeAgreementDef deal;
    deal.type       = TradeAgreementType::BilateralDeal;
    deal.members    = {proposer, partner};
    deal.turnsActive = 0;
    deal.isActive   = true;

    proposerComp.agreements.push_back(deal);
    partnerComp.agreements.push_back(deal);

    LOG_INFO("Trade deal: bilateral agreement between player %u and %u (-20%% tariff)",
             static_cast<unsigned>(proposer), static_cast<unsigned>(partner));

    return ErrorCode::Ok;
}

ErrorCode createFreeTradeZone(aoc::game::GameState& gameState,
                                const std::vector<PlayerId>& members) {
    if (members.size() < 3) {
        return ErrorCode::InvalidArgument;
    }

    TradeAgreementDef ftz;
    ftz.type        = TradeAgreementType::FreeTradeZone;
    ftz.members     = members;
    ftz.turnsActive = 0;
    ftz.isActive    = true;

    for (PlayerId member : members) {
        aoc::game::Player* playerObj = gameState.player(member);
        if (playerObj != nullptr) {
            playerObj->tradeAgreements().agreements.push_back(ftz);
        }
    }

    LOG_INFO("Free Trade Zone formed with %zu members (-50%% tariff, +1 trade route)",
             members.size());

    return ErrorCode::Ok;
}

ErrorCode formCustomsUnion(aoc::game::GameState& gameState,
                             const std::vector<PlayerId>& members,
                             float externalTariff) {
    if (members.size() < 2) {
        return ErrorCode::InvalidArgument;
    }

    TradeAgreementDef cu;
    cu.type            = TradeAgreementType::CustomsUnion;
    cu.members         = members;
    cu.turnsActive     = 0;
    cu.externalTariff  = std::clamp(externalTariff, 0.0f, 0.50f);
    cu.isActive        = true;

    for (PlayerId member : members) {
        aoc::game::Player* playerObj = gameState.player(member);
        if (playerObj != nullptr) {
            playerObj->tradeAgreements().agreements.push_back(cu);
        }
    }

    LOG_INFO("Customs Union formed with %zu members (0%% internal tariff, %.0f%% external)",
             members.size(), static_cast<double>(externalTariff) * 100.0);

    return ErrorCode::Ok;
}

void processTradeAgreements(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        for (TradeAgreementDef& agreement : playerPtr->tradeAgreements().agreements) {
            if (agreement.isActive) {
                ++agreement.turnsActive;
            }
        }
    }
}

} // namespace aoc::sim
