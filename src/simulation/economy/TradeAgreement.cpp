/**
 * @file TradeAgreement.cpp
 * @brief Trade agreements, free trade zones, and customs unions.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/economy/TradeAgreement.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

namespace {
/// Trader unit type id (classic Trader).
constexpr UnitTypeId kStandingRouteUnitType = UnitTypeId{30};
/// Default spawn interval for bilateral standing routes.
constexpr int32_t   kDefaultStandingInterval = 5;
} // namespace

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
    // Bilateral deals automatically establish a standing route: a Trader
    // unit spawns every N turns on the proposer's capital -> partner's
    // capital leg. Destroyable/pillageable; player does not command it.
    deal.standingRouteInterval  = kDefaultStandingInterval;
    deal.standingRouteCountdown = kDefaultStandingInterval;

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

ErrorCode proposeTransitTreaty(aoc::game::GameState& gameState,
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

    // Reject duplicate active TransitTreaty between these two players.
    for (const TradeAgreementDef& existing : proposerComp.agreements) {
        if (existing.type == TradeAgreementType::TransitTreaty && existing.isActive) {
            for (PlayerId member : existing.members) {
                if (member == partner) {
                    return ErrorCode::InvalidArgument;
                }
            }
        }
    }

    TradeAgreementDef treaty;
    treaty.type        = TradeAgreementType::TransitTreaty;
    treaty.members     = {proposer, partner};
    treaty.turnsActive = 0;
    treaty.isActive    = true;
    // No standing route: TransitTreaty is purely right-of-passage.

    proposerPlayer->tradeAgreements().agreements.push_back(treaty);
    partnerPlayer->tradeAgreements().agreements.push_back(treaty);

    LOG_INFO("Transit treaty: zero-toll passage between player %u and %u",
             static_cast<unsigned>(proposer), static_cast<unsigned>(partner));

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

namespace {

/// Return the first city of `player`, or nullptr.
[[nodiscard]] aoc::game::City* firstCity(aoc::game::Player& player) {
    for (const std::unique_ptr<aoc::game::City>& cityPtr : player.cities()) {
        if (cityPtr != nullptr) { return cityPtr.get(); }
    }
    return nullptr;
}

/// Attempt to spawn a standing-route Trader from memberA to memberB.
/// Silently noops if either player has no cities, or unit creation fails.
void spawnStandingTrader(aoc::game::GameState& gameState,
                         aoc::map::HexGrid& grid,
                         const Market& market,
                         DiplomacyManager* diplomacy,
                         PlayerId memberA,
                         PlayerId memberB) {
    aoc::game::Player* origin = gameState.player(memberA);
    aoc::game::Player* dest   = gameState.player(memberB);
    if (origin == nullptr || dest == nullptr) { return; }

    aoc::game::City* originCity = firstCity(*origin);
    aoc::game::City* destCity   = firstCity(*dest);
    if (originCity == nullptr || destCity == nullptr) { return; }

    aoc::game::Unit& traderUnit = origin->addUnit(
        kStandingRouteUnitType, originCity->location());

    const ErrorCode rc = establishTradeRoute(
        gameState, grid, market, diplomacy, traderUnit, *destCity);
    if (rc != ErrorCode::Ok) {
        // Route creation failed (no path, at war, etc.) -- tear the unit down
        // so it does not clutter the map as a dead-end trader.
        origin->removeUnit(&traderUnit);
        return;
    }

    LOG_INFO("Standing route spawned: player %u -> player %u",
             static_cast<unsigned>(memberA),
             static_cast<unsigned>(memberB));
}

} // namespace

void processStandingRoutes(aoc::game::GameState& gameState,
                           aoc::map::HexGrid& grid,
                           const Market& market,
                           DiplomacyManager* diplomacy) {
    // Avoid double-processing: both members carry their own copy of each
    // agreement. Only act on the copy owned by the first listed member.
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        for (TradeAgreementDef& agreement : playerPtr->tradeAgreements().agreements) {
            if (!agreement.isActive) { continue; }
            if (agreement.standingRouteInterval <= 0) { continue; }
            if (agreement.members.empty()) { continue; }
            if (agreement.members.front() != playerPtr->id()) { continue; }

            if (agreement.standingRouteCountdown > 0) {
                --agreement.standingRouteCountdown;
                continue;
            }
            agreement.standingRouteCountdown = agreement.standingRouteInterval;

            // Bilateral: spawn one trader on the first->second leg per cycle.
            // Multilateral: round-robin across ordered member pairs.
            if (agreement.members.size() == 2) {
                spawnStandingTrader(gameState, grid, market, diplomacy,
                                    agreement.members[0], agreement.members[1]);
            } else {
                const std::size_t n = agreement.members.size();
                const std::size_t pairIdx =
                    static_cast<std::size_t>(
                        agreement.turnsActive < 0 ? 0 : agreement.turnsActive) % n;
                const std::size_t partnerIdx = (pairIdx + 1) % n;
                spawnStandingTrader(gameState, grid, market, diplomacy,
                                    agreement.members[pairIdx],
                                    agreement.members[partnerIdx]);
            }
        }
    }
}

} // namespace aoc::sim
