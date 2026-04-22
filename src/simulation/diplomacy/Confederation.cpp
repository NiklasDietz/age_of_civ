/**
 * @file Confederation.cpp
 * @brief Persistent multi-player alliance blocs.
 */

#include "aoc/simulation/diplomacy/Confederation.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/AllianceObligations.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

namespace {

uint32_t nextConfederationId(const aoc::game::GameState& gameState) {
    uint32_t maxId = 0;
    for (const ConfederationComponent& c : gameState.confederations()) {
        if (c.id > maxId) { maxId = c.id; }
    }
    return maxId + 1;
}

bool pairHasTradeRoute(const aoc::game::GameState& gameState,
                       PlayerId a, PlayerId b) {
    for (const TradeRouteComponent& route : gameState.tradeRoutes()) {
        const bool ab = (route.sourcePlayer == a && route.destPlayer   == b);
        const bool ba = (route.sourcePlayer == b && route.destPlayer   == a);
        if (ab || ba) { return true; }
    }
    return false;
}

bool anyMemberInAnyConfederation(const aoc::game::GameState& gameState,
                                  const std::vector<PlayerId>& members) {
    for (const ConfederationComponent& c : gameState.confederations()) {
        if (!c.isActive) { continue; }
        for (PlayerId mine : members) {
            for (PlayerId existing : c.members) {
                if (mine == existing) { return true; }
            }
        }
    }
    return false;
}

/// Industrial era gate. Every member must have researched at least one
/// tech whose era is Industrial (era.value >= 4). Uses `techDef` iteration
/// since PlayerTechComponent stores a bitset by id.
bool memberIsIndustrial(const aoc::game::Player& p) {
    for (uint16_t t = 0; t < techCount(); ++t) {
        TechId id{t};
        if (!p.tech().hasResearched(id)) { continue; }
        if (techDef(id).era.value >= 4) { return true; }
    }
    return false;
}

} // namespace

ErrorCode formConfederation(aoc::game::GameState& gameState,
                             const DiplomacyManager& diplomacy,
                             const std::vector<PlayerId>& members,
                             int32_t currentTurn) {
    if (members.size() < 2 || members.size() > CONFEDERATION_MAX_MEMBERS) {
        return ErrorCode::InvalidArgument;
    }

    // Duplicate-id check and city-state exclusion.
    for (std::size_t i = 0; i < members.size(); ++i) {
        const PlayerId a = members[i];
        const aoc::game::Player* ap = gameState.player(a);
        if (ap == nullptr) { return ErrorCode::InvalidArgument; }
        if (ap->victoryTracker().isEliminated) {
            return ErrorCode::InvalidArgument;
        }
        if (!memberIsIndustrial(*ap)) {
            return ErrorCode::TechPrerequisiteNotMet;
        }
        for (std::size_t j = i + 1; j < members.size(); ++j) {
            if (members[i] == members[j]) {
                return ErrorCode::InvalidArgument;
            }
        }
    }

    if (anyMemberInAnyConfederation(gameState, members)) {
        return ErrorCode::AllianceExists;
    }

    // Pairwise stance + met + trade-route gate.
    for (std::size_t i = 0; i < members.size(); ++i) {
        for (std::size_t j = i + 1; j < members.size(); ++j) {
            const PlayerId a = members[i];
            const PlayerId b = members[j];
            if (!diplomacy.haveMet(a, b)) {
                return ErrorCode::TechPrerequisiteNotMet;
            }
            const PairwiseRelation& rel = diplomacy.relation(a, b);
            if (rel.isAtWar) { return ErrorCode::TechPrerequisiteNotMet; }
            if (rel.stance() < DiplomaticStance::Friendly) {
                return ErrorCode::TechPrerequisiteNotMet;
            }
            if (!pairHasTradeRoute(gameState, a, b)) {
                return ErrorCode::TechPrerequisiteNotMet;
            }
        }
    }

    ConfederationComponent conf;
    conf.id         = nextConfederationId(gameState);
    conf.members    = members;
    conf.formedTurn = currentTurn;
    conf.isActive   = true;
    gameState.confederations().push_back(std::move(conf));

    LOG_INFO("Confederation %u formed with %zu members on turn %d",
             static_cast<unsigned>(gameState.confederations().back().id),
             members.size(), currentTurn);
    return ErrorCode::Ok;
}

void dissolveConfederationFor(aoc::game::GameState& gameState, PlayerId member) {
    for (ConfederationComponent& c : gameState.confederations()) {
        if (!c.isActive) { continue; }
        for (PlayerId m : c.members) {
            if (m == member) {
                c.isActive = false;
                LOG_INFO("Confederation %u dissolved (member %u trigger)",
                         static_cast<unsigned>(c.id),
                         static_cast<unsigned>(member));
                return;
            }
        }
    }
}

const ConfederationComponent* confederationForPlayer(
    const aoc::game::GameState& gameState, PlayerId player) {
    for (const ConfederationComponent& c : gameState.confederations()) {
        if (!c.isActive) { continue; }
        for (PlayerId m : c.members) {
            if (m == player) { return &c; }
        }
    }
    return nullptr;
}

void tickConfederations(aoc::game::GameState& gameState) {
    for (ConfederationComponent& c : gameState.confederations()) {
        if (!c.isActive) { continue; }

        // Purge eliminated / invalid members.
        std::vector<PlayerId> alive;
        alive.reserve(c.members.size());
        for (PlayerId m : c.members) {
            const aoc::game::Player* p = gameState.player(m);
            if (p != nullptr && !p->victoryTracker().isEliminated) {
                alive.push_back(m);
            }
        }
        c.members = std::move(alive);

        if (c.members.size() < 2) {
            c.isActive = false;
            LOG_INFO("Confederation %u dissolved (below minimum size)",
                     static_cast<unsigned>(c.id));
        }
    }
}

void onConfederationMemberAttacked(aoc::game::GameState& gameState,
                                    AllianceObligationTracker* tracker,
                                    PlayerId aggressor,
                                    PlayerId target,
                                    int32_t /*currentTurn*/) {
    if (tracker == nullptr) { return; }
    const ConfederationComponent* conf = confederationForPlayer(gameState, target);
    if (conf == nullptr) { return; }

    for (PlayerId m : conf->members) {
        if (m == target || m == aggressor) { continue; }
        AllianceObligation ob{};
        ob.defender         = target;
        ob.attacker         = aggressor;
        ob.obligatedPlayer  = m;
        ob.turnsToRespond   = 5;
        ob.fulfilled        = false;
        tracker->pendingObligations.push_back(ob);
        LOG_INFO("Confederation obligation: p%u must respond to attack on p%u by p%u",
                 static_cast<unsigned>(m),
                 static_cast<unsigned>(target),
                 static_cast<unsigned>(aggressor));
    }
}

} // namespace aoc::sim
