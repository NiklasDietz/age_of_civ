/**
 * @file DiplomacyActions.cpp
 * @brief Player-facing diplomacy requests (see DiplomacyActions.hpp).
 */

#include "aoc/simulation/diplomacy/DiplomacyActions.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/diplomacy/WarWeariness.hpp"
#include "aoc/simulation/religion/Religion.hpp"

#include <algorithm>
#include <memory>

namespace aoc::sim {

namespace {

/// Both ids name living majors inside the relation matrix and differ.
ErrorCode pairValid(const aoc::game::GameState& gameState, const DiplomacyManager& diplomacy,
                    PlayerId actor, PlayerId target) {
    if (actor == target || actor >= diplomacy.playerCount() || target >= diplomacy.playerCount()) {
        return ErrorCode::EntityNotFound;
    }
    const aoc::game::Player* a = gameState.player(actor);
    const aoc::game::Player* b = gameState.player(target);
    if (a == nullptr || b == nullptr || a->victoryTracker().isEliminated
        || b->victoryTracker().isEliminated) {
        return ErrorCode::EntityNotFound;
    }
    return ErrorCode::Ok;
}

bool denouncementActive(const PairwiseRelation& rel, int32_t currentTurn) {
    return rel.denouncedOnTurn >= 0 && currentTurn - rel.denouncedOnTurn <= DENOUNCE_TURNS;
}

bool friendshipActive(const PairwiseRelation& rel, int32_t currentTurn) {
    return rel.friendshipUntilTurn > currentTurn;
}

/// Does `holder` own a city founded by `founder`?
bool holdsCityFoundedBy(const aoc::game::Player& holder, PlayerId founder) {
    for (const std::unique_ptr<aoc::game::City>& city : holder.cities()) {
        if (city != nullptr && city->owner() == holder.id() && city->originalOwner() == founder) {
            return true;
        }
    }
    return false;
}

} // namespace

ErrorCode casusBelliUsable(const aoc::game::GameState& gameState, const DiplomacyManager& diplomacy,
                           PlayerId actor, PlayerId target, CasusBelliType cb, int32_t currentTurn) {
    const ErrorCode pair = pairValid(gameState, diplomacy, actor, target);
    if (pair != ErrorCode::Ok) {
        return pair;
    }
    const aoc::game::Player& me    = *gameState.player(actor);
    const aoc::game::Player& them  = *gameState.player(target);
    const PairwiseRelation& rel    = diplomacy.relation(actor, target);
    switch (cb) {
        case CasusBelliType::SurpriseWar:
            return ErrorCode::Ok;
        case CasusBelliType::FormalWar:
            return (denouncementActive(rel, currentTurn) || diplomacy.holdsCasusBelli(actor, target))
                       ? ErrorCode::Ok
                       : ErrorCode::InvalidArgument;
        case CasusBelliType::HolyWar: {
            const ReligionId theirs = them.faith().foundedReligion;
            if (theirs == NO_RELIGION) {
                return ErrorCode::InvalidArgument;
            }
            for (const std::unique_ptr<aoc::game::City>& city : me.cities()) {
                if (city != nullptr && city->owner() == actor
                    && city->religion().dominantReligion() == theirs) {
                    return ErrorCode::Ok;
                }
            }
            return ErrorCode::InvalidArgument;
        }
        case CasusBelliType::LiberationWar: {
            for (const std::unique_ptr<aoc::game::City>& city : them.cities()) {
                if (city == nullptr || city->owner() != target) {
                    continue;
                }
                const PlayerId founder = city->originalOwner();
                if (founder == INVALID_PLAYER || founder == actor || founder == target
                    || founder >= diplomacy.playerCount()) {
                    continue;
                }
                const PairwiseRelation& withFounder = diplomacy.relation(actor, founder);
                if (withFounder.hasDefensiveAlliance || withFounder.hasMilitaryAlliance) {
                    return ErrorCode::Ok;
                }
            }
            return ErrorCode::InvalidArgument;
        }
        case CasusBelliType::ReconquestWar:
            return holdsCityFoundedBy(them, actor) ? ErrorCode::Ok : ErrorCode::InvalidArgument;
        case CasusBelliType::ColonialWar:
            return (static_cast<int32_t>(me.era().currentEra.value)
                    - static_cast<int32_t>(them.era().currentEra.value) >= COLONIAL_ERA_GAP)
                       ? ErrorCode::Ok
                       : ErrorCode::InvalidArgument;
        case CasusBelliType::EconomicWar:
            // Directional on purpose: the grounds are that THEY embargoed YOU.
            // Your own embargo on them is not a grievance against them.
            return diplomacy.hasEmbargo(target, actor) ? ErrorCode::Ok : ErrorCode::InvalidArgument;
        case CasusBelliType::ProtectorateWar:
        default:
            return ErrorCode::InvalidArgument;
    }
}

std::vector<CasusBelliType> availableCasusBelli(const aoc::game::GameState& gameState,
                                                const DiplomacyManager& diplomacy, PlayerId actor,
                                                PlayerId target, int32_t currentTurn) {
    std::vector<CasusBelliType> out;
    for (int32_t i = 0; i < CASUS_BELLI_COUNT; ++i) {
        const CasusBelliType cb = static_cast<CasusBelliType>(i);
        if (casusBelliUsable(gameState, diplomacy, actor, target, cb, currentTurn) == ErrorCode::Ok) {
            out.push_back(cb);
        }
    }
    return out;
}

int32_t economicCostOfWar(const aoc::game::GameState& gameState, const DiplomacyManager& diplomacy,
                          PlayerId me, PlayerId target) {
    if (me == target || me == INVALID_PLAYER || target == INVALID_PLAYER
        || gameState.player(me) == nullptr || gameState.player(target) == nullptr) {
        return 0;
    }
    // The tie a delivery refreshes already counts the value shipped and the
    // routes running, and it decays once the shipments stop, so a war costs
    // what the trade was recently worth rather than what it once was.
    int32_t value = diplomacy.modifierAmount(me, target, TRADE_PARTNER_REASON);
    for (const DiplomaticDeal& deal : gameState.deals().activeDeals) {
        if (!deal.isAccepted || deal.isBroken) {
            continue;
        }
        const bool thisPair = (deal.playerA == me && deal.playerB == target)
                              || (deal.playerA == target && deal.playerB == me);
        if (!thisPair) {
            continue;
        }
        for (const DealTerm& term : deal.terms) {
            if (term.toPlayer != me) {
                continue; // only what comes to us is ours to lose
            }
            value += term.goldPerTurn;
            if (term.type == DealTermType::SupplyContract
                || term.type == DealTermType::GoodsExchange) {
                value += term.goodAmount; // the goods we lean on them to ship
            }
        }
    }
    return value;
}

int32_t warCostRelationPoints(const aoc::game::GameState& gameState,
                              const DiplomacyManager& diplomacy, PlayerId me, PlayerId target) {
    const aoc::game::Player* p = gameState.player(me);
    if (p == nullptr) {
        return 0;
    }
    const int32_t value = economicCostOfWar(gameState, diplomacy, me, target);
    if (value <= 0) {
        return 0;
    }
    const LeaderBehavior& beh = leaderPersonality(p->civId()).behavior;
    const int32_t points =
        static_cast<int32_t>(static_cast<float>(value) * beh.economicFocus) / WAR_COST_VALUE_DIVISOR;
    return std::min(WAR_COST_MAX_POINTS, points);
}

bool aiAcceptsPeace(const aoc::game::GameState& gameState, const DiplomacyManager& diplomacy,
                    PlayerId ai, PlayerId other) {
    const aoc::game::Player* me   = gameState.player(ai);
    const aoc::game::Player* them = gameState.player(other);
    if (me == nullptr || them == nullptr) {
        return false;
    }
    const LeaderBehavior& beh = leaderPersonality(me->civId()).behavior;
    const int32_t ourMilitary   = me->militaryUnitCount();
    const int32_t theirMilitary = them->militaryUnitCount();
    // Exhaustion, not just weakness. A victor with thirty turns of weariness
    // behind it takes the peace it is offered; without this the winning side
    // always refused and the loser had no way out but destruction.
    if (me->warWeariness().weariness >= PEACE_WEARINESS_TURNS) {
        return true;
    }

    // A partner worth keeping buys peace (plan 4.2). The tie decays across the
    // war, so what this reads is the trade that was still running when it
    // started, not a memory of one long finished.
    if (economicCostOfWar(gameState, diplomacy, ai, other) >= PEACE_TRADE_VALUE) {
        return true;
    }

    // Refuse only while holding a DECISIVE advantage.
    //
    // This used to accept peace only when the other side was much stronger,
    // which inverted the interesting case: a near-parity war -- the one both
    // sides most want to end -- was refused by both of them. Measured on seed
    // 42, players 2 and 3 sued for peace at each other on alternating turns,
    // each refusing the other at weariness 10-17, and the war ran until one
    // was conquered. Cycling is not the risk it once was: PEACE_LOCK_TURNS and
    // WAR_MIN_TURNS now both bind, capping a pair at one war per twenty turns.
    // An empty enemy army only means dominance if we have one. Two civs with no
    // military between them are at parity, not infinitely mismatched -- and
    // that is the ordinary early-game case, so getting it wrong refused every
    // peace before either side had raised troops.
    const float advantage =
        (theirMilitary > 0)
            ? static_cast<float>(ourMilitary) / static_cast<float>(theirMilitary)
            : ((ourMilitary > 0) ? 10.0f : 1.0f);
    const float decisive =
        std::max(1.2f, 1.5f + beh.grudgeHolding - beh.peaceAcceptanceThreshold);
    return advantage <= decisive;
}

ErrorCode requestDeclareWar(aoc::game::GameState& gameState, DiplomacyManager& diplomacy, PlayerId actor,
                            PlayerId target, CasusBelliType cb, int32_t currentTurn,
                            AllianceObligationTracker* obligations) {
    const ErrorCode pair = pairValid(gameState, diplomacy, actor, target);
    if (pair != ErrorCode::Ok) {
        return pair;
    }
    const PairwiseRelation& rel = diplomacy.relation(actor, target);
    if (!rel.hasMet || rel.isAtWar || friendshipActive(rel, currentTurn)
        || rel.turnsSincePeace < PEACE_LOCK_TURNS) {
        return ErrorCode::InvalidState;
    }
    const ErrorCode justified = casusBelliUsable(gameState, diplomacy, actor, target, cb, currentTurn);
    if (justified != ErrorCode::Ok) {
        return justified;
    }
    // Trade is a reason not to fight (plan 4.2). Every AI war path reaches the
    // field through this request, the relation-gated roll, the opportunistic
    // one and the Domination campaign alike, so one check covers all three. A
    // human is told the cost and decides for itself.
    if (actor != gameState.humanPlayerId()
        && warCostRelationPoints(gameState, diplomacy, actor, target) > WAR_COST_VETO_POINTS) {
        return ErrorCode::InvalidState;
    }
    diplomacy.declareWar(actor, target, cb, obligations, &gameState, currentTurn);
    return ErrorCode::Ok;
}

ErrorCode requestMakePeace(aoc::game::GameState& gameState, DiplomacyManager& diplomacy, PlayerId actor,
                           PlayerId target, int32_t currentTurn) {
    const ErrorCode pair = pairValid(gameState, diplomacy, actor, target);
    if (pair != ErrorCode::Ok) {
        return pair;
    }
    const PairwiseRelation& rel = diplomacy.relation(actor, target);
    if (!rel.isAtWar) {
        return ErrorCode::InvalidState;
    }
    if (rel.warDeclaredOnTurn >= 0 && currentTurn - rel.warDeclaredOnTurn < WAR_MIN_TURNS) {
        return ErrorCode::InvalidState;
    }
    if (target != gameState.humanPlayerId() && !aiAcceptsPeace(gameState, diplomacy, target, actor)) {
        return ErrorCode::InvalidState; // they are winning and refuse
    }
    diplomacy.makePeace(actor, target);
    return ErrorCode::Ok;
}

ErrorCode requestDenounce(const aoc::game::GameState& gameState, DiplomacyManager& diplomacy,
                          PlayerId actor, PlayerId target, int32_t currentTurn) {
    const ErrorCode pair = pairValid(gameState, diplomacy, actor, target);
    if (pair != ErrorCode::Ok) {
        return pair;
    }
    PairwiseRelation& rel = diplomacy.relation(actor, target);
    if (!rel.hasMet || rel.isAtWar || friendshipActive(rel, currentTurn)
        || denouncementActive(rel, currentTurn)) {
        return ErrorCode::InvalidState;
    }
    rel.denouncedOnTurn = currentTurn;
    diplomacy.addModifier(actor, target, RelationModifier{"Denounced", DENOUNCE_PENALTY, DENOUNCE_TURNS});
    LOG_INFO("Player %u denounced player %u", static_cast<unsigned>(actor), static_cast<unsigned>(target));
    return ErrorCode::Ok;
}

ErrorCode requestDeclareFriendship(const aoc::game::GameState& gameState, DiplomacyManager& diplomacy,
                                   PlayerId actor, PlayerId target, int32_t currentTurn) {
    const ErrorCode pair = pairValid(gameState, diplomacy, actor, target);
    if (pair != ErrorCode::Ok) {
        return pair;
    }
    PairwiseRelation& rel  = diplomacy.relation(actor, target);
    PairwiseRelation& back = diplomacy.relation(target, actor);
    if (!rel.hasMet || rel.isAtWar || friendshipActive(rel, currentTurn)
        || denouncementActive(rel, currentTurn) || denouncementActive(back, currentTurn)) {
        return ErrorCode::InvalidState;
    }
    if (rel.totalScore() < FRIENDSHIP_MIN_SCORE) {
        return ErrorCode::InvalidState; // they decline
    }
    rel.friendshipUntilTurn  = currentTurn + FRIENDSHIP_TURNS;
    back.friendshipUntilTurn = currentTurn + FRIENDSHIP_TURNS;
    diplomacy.addModifier(actor, target,
                          RelationModifier{"Declaration of Friendship", FRIENDSHIP_BONUS, FRIENDSHIP_TURNS});
    LOG_INFO("Players %u and %u declared friendship until turn %d", static_cast<unsigned>(actor),
             static_cast<unsigned>(target), currentTurn + FRIENDSHIP_TURNS);
    return ErrorCode::Ok;
}

ErrorCode requestSendDelegation(aoc::game::GameState& gameState, DiplomacyManager& diplomacy,
                                PlayerId actor, PlayerId target) {
    const ErrorCode pair = pairValid(gameState, diplomacy, actor, target);
    if (pair != ErrorCode::Ok) {
        return pair;
    }
    PairwiseRelation& rel = diplomacy.relation(actor, target);
    if (!rel.hasMet || rel.isAtWar || rel.hasDelegation || rel.hasEmbassy) {
        return ErrorCode::InvalidState;
    }
    aoc::game::Player& me = *gameState.player(actor);
    if (me.treasury() < DELEGATION_GOLD) {
        return ErrorCode::InsufficientResources;
    }
    me.addGold(-DELEGATION_GOLD, aoc::sim::MoneyFlow::transfer(target)); // the gifts go with it
    gameState.player(target)->addGold(DELEGATION_GOLD, aoc::sim::MoneyFlow::transfer(actor));
    rel.hasDelegation = true;
    rel.intelLevel    = std::max<uint8_t>(rel.intelLevel, 1);
    diplomacy.addModifier(actor, target, RelationModifier{"Delegation", DELEGATION_BONUS, 0});
    return ErrorCode::Ok;
}

ErrorCode requestEstablishEmbassy(aoc::game::GameState& gameState, DiplomacyManager& diplomacy,
                                  PlayerId actor, PlayerId target) {
    const ErrorCode pair = pairValid(gameState, diplomacy, actor, target);
    if (pair != ErrorCode::Ok) {
        return pair;
    }
    PairwiseRelation& rel = diplomacy.relation(actor, target);
    if (!rel.hasMet || rel.isAtWar || rel.hasEmbassy) {
        return ErrorCode::InvalidState;
    }
    if (rel.stance() == DiplomaticStance::Hostile) {
        return ErrorCode::InvalidState; // they refuse
    }
    aoc::game::Player& me = *gameState.player(actor);
    if (me.treasury() < EMBASSY_GOLD) {
        return ErrorCode::InsufficientResources;
    }
    me.addGold(-EMBASSY_GOLD, aoc::sim::MoneyFlow::transfer(target)); // the building is bought there
    gameState.player(target)->addGold(EMBASSY_GOLD, aoc::sim::MoneyFlow::transfer(actor));
    rel.hasEmbassy = true;
    rel.intelLevel = std::max<uint8_t>(rel.intelLevel, 2);
    diplomacy.addModifier(actor, target, RelationModifier{"Embassy", EMBASSY_BONUS, 0});
    return ErrorCode::Ok;
}

ErrorCode requestOpenBorders(const aoc::game::GameState& gameState, DiplomacyManager& diplomacy,
                             PlayerId actor, PlayerId target, int32_t currentTurn) {
    const ErrorCode pair = pairValid(gameState, diplomacy, actor, target);
    if (pair != ErrorCode::Ok) {
        return pair;
    }
    PairwiseRelation& rel = diplomacy.relation(actor, target);
    if (!rel.hasMet || rel.isAtWar || rel.hasOpenBorders) {
        return ErrorCode::InvalidState;
    }
    if (rel.totalScore() < OPEN_BORDERS_MIN_SCORE) {
        return ErrorCode::InvalidState; // they decline
    }
    diplomacy.grantOpenBorders(actor, target);
    diplomacy.relation(actor, target).openBordersUntilTurn = currentTurn + OPEN_BORDERS_TURNS;
    diplomacy.relation(target, actor).openBordersUntilTurn = currentTurn + OPEN_BORDERS_TURNS;
    return ErrorCode::Ok;
}

} // namespace aoc::sim
