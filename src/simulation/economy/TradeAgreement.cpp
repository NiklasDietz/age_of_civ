/**
 * @file TradeAgreement.cpp
 * @brief Trade agreements, free trade zones, and customs unions.
 */

#include "aoc/simulation/economy/TradeAgreement.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

ErrorCode proposeBilateralDeal(aoc::ecs::World& world,
                                 PlayerId proposer, PlayerId partner) {
    if (proposer == partner) {
        return ErrorCode::InvalidArgument;
    }

    aoc::ecs::ComponentPool<PlayerTradeAgreementsComponent>* agreePool =
        world.getPool<PlayerTradeAgreementsComponent>();
    if (agreePool == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // Find both players' components
    PlayerTradeAgreementsComponent* proposerComp = nullptr;
    PlayerTradeAgreementsComponent* partnerComp = nullptr;
    for (uint32_t i = 0; i < agreePool->size(); ++i) {
        if (agreePool->data()[i].owner == proposer) { proposerComp = &agreePool->data()[i]; }
        if (agreePool->data()[i].owner == partner)  { partnerComp = &agreePool->data()[i]; }
    }
    if (proposerComp == nullptr || partnerComp == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // Check if deal already exists
    for (const TradeAgreementDef& existing : proposerComp->agreements) {
        if (existing.type == TradeAgreementType::BilateralDeal && existing.isActive) {
            for (PlayerId member : existing.members) {
                if (member == partner) {
                    return ErrorCode::InvalidArgument;  // Already have a deal
                }
            }
        }
    }

    TradeAgreementDef deal;
    deal.type = TradeAgreementType::BilateralDeal;
    deal.members = {proposer, partner};
    deal.turnsActive = 0;
    deal.isActive = true;

    proposerComp->agreements.push_back(deal);
    partnerComp->agreements.push_back(deal);

    LOG_INFO("Trade deal: bilateral agreement between player %u and %u (-20%% tariff)",
             static_cast<unsigned>(proposer), static_cast<unsigned>(partner));

    return ErrorCode::Ok;
}

ErrorCode createFreeTradeZone(aoc::ecs::World& world,
                                const std::vector<PlayerId>& members) {
    if (members.size() < 3) {
        return ErrorCode::InvalidArgument;
    }

    aoc::ecs::ComponentPool<PlayerTradeAgreementsComponent>* agreePool =
        world.getPool<PlayerTradeAgreementsComponent>();
    if (agreePool == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    TradeAgreementDef ftz;
    ftz.type = TradeAgreementType::FreeTradeZone;
    ftz.members = members;
    ftz.turnsActive = 0;
    ftz.isActive = true;

    for (PlayerId member : members) {
        for (uint32_t i = 0; i < agreePool->size(); ++i) {
            if (agreePool->data()[i].owner == member) {
                agreePool->data()[i].agreements.push_back(ftz);
                break;
            }
        }
    }

    LOG_INFO("Free Trade Zone formed with %zu members (-50%% tariff, +1 trade route)",
             members.size());

    return ErrorCode::Ok;
}

ErrorCode formCustomsUnion(aoc::ecs::World& world,
                             const std::vector<PlayerId>& members,
                             float externalTariff) {
    if (members.size() < 2) {
        return ErrorCode::InvalidArgument;
    }

    aoc::ecs::ComponentPool<PlayerTradeAgreementsComponent>* agreePool =
        world.getPool<PlayerTradeAgreementsComponent>();
    if (agreePool == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    TradeAgreementDef cu;
    cu.type = TradeAgreementType::CustomsUnion;
    cu.members = members;
    cu.turnsActive = 0;
    cu.externalTariff = std::clamp(externalTariff, 0.0f, 0.50f);
    cu.isActive = true;

    for (PlayerId member : members) {
        for (uint32_t i = 0; i < agreePool->size(); ++i) {
            if (agreePool->data()[i].owner == member) {
                agreePool->data()[i].agreements.push_back(cu);
                break;
            }
        }
    }

    LOG_INFO("Customs Union formed with %zu members (0%% internal tariff, %.0f%% external)",
             members.size(), static_cast<double>(externalTariff) * 100.0);

    return ErrorCode::Ok;
}

void processTradeAgreements(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<PlayerTradeAgreementsComponent>* agreePool =
        world.getPool<PlayerTradeAgreementsComponent>();
    if (agreePool == nullptr) {
        return;
    }

    for (uint32_t p = 0; p < agreePool->size(); ++p) {
        PlayerTradeAgreementsComponent& comp = agreePool->data()[p];
        for (TradeAgreementDef& agreement : comp.agreements) {
            if (agreement.isActive) {
                ++agreement.turnsActive;
            }
        }
    }
}

} // namespace aoc::sim
