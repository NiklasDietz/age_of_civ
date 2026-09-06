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
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
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

bool aiAcceptsPeace(const aoc::game::GameState& gameState, PlayerId ai, PlayerId other) {
    const aoc::game::Player* me   = gameState.player(ai);
    const aoc::game::Player* them = gameState.player(other);
    if (me == nullptr || them == nullptr) {
        return false;
    }
    const LeaderBehavior& beh = leaderPersonality(me->civId()).behavior;
    const int32_t ourMilitary   = me->militaryUnitCount();
    const int32_t theirMilitary = them->militaryUnitCount();
    const float ratio = ourMilitary > 0
                            ? static_cast<float>(theirMilitary) / static_cast<float>(ourMilitary)
                            : 10.0f;
    const float threshold = 1.5f + beh.grudgeHolding - beh.peaceAcceptanceThreshold;
    return ratio > std::max(threshold, 0.8f);
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
    if (target != gameState.humanPlayerId() && !aiAcceptsPeace(gameState, target, actor)) {
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
    me.addGold(-DELEGATION_GOLD);
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
    me.addGold(-EMBASSY_GOLD);
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
