/**
 * @file DealProposals.cpp
 * @brief Deal proposals: inbox, valuation, routing (see DealProposals.hpp).
 */

#include "aoc/simulation/diplomacy/DealProposals.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/diplomacy/DiplomacyActions.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/event/GameNotifications.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace aoc::sim {

namespace {

constexpr int32_t CITY_BASE_VALUE     = 200;
constexpr int32_t CITY_VALUE_PER_POP  = 50;
constexpr int32_t TILE_VALUE          = 40;
constexpr int32_t OPEN_BORDERS_COOL   = -20;  ///< Neutral stance
constexpr int32_t OPEN_BORDERS_COLD   = -60;  ///< Unfriendly or Hostile
constexpr int32_t NON_AGGRESSION_GAIN = 10;
constexpr int32_t NON_AGGRESSION_COLD = -40;  ///< Hostile: no trust
constexpr int32_t MUTUAL_DEFENSE_ALLY = 20;
constexpr int32_t MUTUAL_DEFENSE_WARM = -20;  ///< Friendly but not allied
constexpr int32_t MUTUAL_DEFENSE_COLD = -100;
constexpr int32_t ARMS_LIMIT_COST     = -50;
constexpr int32_t ZONE_COST           = -30;
constexpr int32_t WAR_GUILT_BLAME     = -150;
constexpr int32_t WAR_GUILT_GAIN      = 50;

std::string civName(const aoc::game::GameState& gameState, PlayerId id) {
    const aoc::game::Player* p = gameState.player(id);
    if (p == nullptr) {
        return "P" + std::to_string(static_cast<unsigned>(id));
    }
    const CivilizationDef& def = civDef(p->civId());
    return def.name.empty() ? "P" + std::to_string(static_cast<unsigned>(id)) : std::string(def.name);
}

int32_t cityValueAt(const aoc::game::GameState& gameState, PlayerId owner, hex::AxialCoord at) {
    const aoc::game::Player* p = gameState.player(owner);
    if (p == nullptr) {
        return CITY_BASE_VALUE;
    }
    for (const std::unique_ptr<aoc::game::City>& city : p->cities()) {
        if (city != nullptr && city->location() == at) {
            return CITY_BASE_VALUE + CITY_VALUE_PER_POP * city->population();
        }
    }
    return CITY_BASE_VALUE;
}

PlayerId otherParty(const DiplomaticDeal& deal, PlayerId me) {
    return deal.playerA == me ? deal.playerB : deal.playerA;
}

ErrorCode proposalShapeValid(const aoc::game::GameState& gameState, const DiplomacyManager& diplomacy,
                             const DiplomaticDeal& deal) {
    const PlayerId a = deal.playerA;
    const PlayerId b = deal.playerB;
    if (a == b || a >= diplomacy.playerCount() || b >= diplomacy.playerCount()
        || gameState.player(a) == nullptr || gameState.player(b) == nullptr) {
        return ErrorCode::EntityNotFound;
    }
    if (!diplomacy.haveMet(a, b)) {
        return ErrorCode::InvalidState;
    }
    if (deal.terms.empty()) {
        return ErrorCode::InvalidArgument;
    }
    const bool atWar = diplomacy.isAtWar(a, b);
    for (const DealTerm& term : deal.terms) {
        const bool partiesOk = (term.fromPlayer == a && term.toPlayer == b)
                               || (term.fromPlayer == b && term.toPlayer == a);
        if (!partiesOk) {
            return ErrorCode::InvalidArgument;
        }
        if (atWar && term.type == DealTermType::OpenBorders) {
            return ErrorCode::InvalidState;
        }
    }
    return ErrorCode::Ok;
}

std::string termsText(const aoc::game::GameState& gameState, const DiplomaticDeal& deal) {
    std::string text;
    for (std::size_t t = 0; t < deal.terms.size(); ++t) {
        if (t > 0) { text += "; "; }
        text += describeDealTerm(gameState, deal.terms[t]);
    }
    return text;
}

void notify(PlayerId to, PlayerId other, std::string title, std::string body, int32_t priority) {
    aoc::sim::event::GameNotification n;
    n.category       = aoc::sim::event::NotificationCategory::Diplomacy;
    n.title          = std::move(title);
    n.body           = std::move(body);
    n.relevantPlayer = to;
    n.otherPlayer    = other;
    n.priority       = priority;
    aoc::sim::event::pushNotification(n);
}

/// proposeDeal + acceptDeal, then the parts acceptDeal leaves to diplomacy: open
/// borders for the term's duration, and peace when the parties were at war.
ErrorCode applyDeal(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, GlobalDealTracker& tracker,
                    DiplomacyManager& diplomacy, const DiplomaticDeal& deal, int32_t currentTurn) {
    const std::size_t index = tracker.activeDeals.size();
    const ErrorCode proposed = proposeDeal(gameState, tracker, deal);
    if (proposed != ErrorCode::Ok) {
        return proposed;
    }
    const ErrorCode accepted = acceptDeal(gameState, grid, tracker, static_cast<int32_t>(index));
    if (accepted != ErrorCode::Ok) {
        return accepted;
    }
    const PlayerId a = deal.playerA;
    const PlayerId b = deal.playerB;
    if (a < diplomacy.playerCount() && b < diplomacy.playerCount()) {
        for (const DealTerm& term : deal.terms) {
            if (term.type == DealTermType::OpenBorders) {
                if (!diplomacy.relation(a, b).hasOpenBorders) {
                    diplomacy.grantOpenBorders(a, b);
                }
                diplomacy.relation(a, b).openBordersUntilTurn = currentTurn + term.duration;
                diplomacy.relation(b, a).openBordersUntilTurn = currentTurn + term.duration;
            }
        }
        if (diplomacy.isAtWar(a, b)) {
            diplomacy.makePeace(a, b); // a deal concluded at war is the peace treaty
        }
    }
    return ErrorCode::Ok;
}

} // namespace

int32_t dealValueFor(const aoc::game::GameState& gameState, const DiplomacyManager& diplomacy,
                     PlayerId evaluator, const DiplomaticDeal& deal) {
    const PlayerId other = otherParty(deal, evaluator);
    const bool inMatrix  = evaluator < diplomacy.playerCount() && other < diplomacy.playerCount();
    const DiplomaticStance stance = inMatrix ? diplomacy.relation(evaluator, other).stance()
                                             : DiplomaticStance::Neutral;
    const int32_t score = inMatrix ? diplomacy.relation(evaluator, other).totalScore() : 0;
    int32_t value = 0;
    for (const DealTerm& term : deal.terms) {
        const int32_t dir = term.toPlayer == evaluator ? 1 : (term.fromPlayer == evaluator ? -1 : 0);
        switch (term.type) {
            case DealTermType::GoldLump:
                value += dir * term.goldLump;
                break;
            case DealTermType::WarReparations:
                value += dir * term.goldPerTurn * term.duration;
                break;
            case DealTermType::CedeCity:
                value += dir * cityValueAt(gameState, term.fromPlayer, term.tileCoord);
                break;
            case DealTermType::CedeTile:
                value += dir * TILE_VALUE;
                break;
            case DealTermType::OpenBorders:
                if (stance == DiplomaticStance::Neutral) {
                    value += OPEN_BORDERS_COOL;
                } else if (stance == DiplomaticStance::Unfriendly || stance == DiplomaticStance::Hostile) {
                    value += OPEN_BORDERS_COLD;
                }
                break;
            case DealTermType::NonAggression:
                value += stance == DiplomaticStance::Hostile ? NON_AGGRESSION_COLD : NON_AGGRESSION_GAIN;
                break;
            case DealTermType::MutualDefense:
                if (stance == DiplomaticStance::Allied) {
                    value += MUTUAL_DEFENSE_ALLY;
                } else if (stance == DiplomaticStance::Friendly) {
                    value += MUTUAL_DEFENSE_WARM;
                } else {
                    value += MUTUAL_DEFENSE_COLD;
                }
                break;
            case DealTermType::ArmsLimitation:
                value += ARMS_LIMIT_COST;
                break;
            case DealTermType::DemilitarizedZone:
                value += ZONE_COST;
                break;
            case DealTermType::WarGuilt:
                value += term.fromPlayer == evaluator ? WAR_GUILT_BLAME : WAR_GUILT_GAIN;
                break;
            case DealTermType::GoodsExchange:
            case DealTermType::MostFavoredNation:
            case DealTermType::ExclusiveAccess:
            default:
                break; // economy terms: neutral until the goods valuation lands
        }
    }
    // Goodwill only sweetens: a hostile evaluator still takes a gift, but pacts
    // already carry their stance penalties above.
    return value + std::max(score, 0) / 4;
}

bool aiAcceptsDeal(const aoc::game::GameState& gameState, const DiplomacyManager& diplomacy, PlayerId ai,
                   const DiplomaticDeal& deal) {
    return dealValueFor(gameState, diplomacy, ai, deal) >= 0;
}

std::string describeDealTerm(const aoc::game::GameState& gameState, const DealTerm& term) {
    const std::string from = civName(gameState, term.fromPlayer);
    const std::string to   = civName(gameState, term.toPlayer);
    switch (term.type) {
        case DealTermType::GoldLump:
            return std::to_string(term.goldLump) + " gold (" + from + " -> " + to + ")";
        case DealTermType::WarReparations:
            return std::to_string(term.goldPerTurn) + " gold per turn for " + std::to_string(term.duration)
                   + " turns (" + from + " -> " + to + ")";
        case DealTermType::CedeCity:
            return "city at (" + std::to_string(term.tileCoord.q) + "," + std::to_string(term.tileCoord.r)
                   + ") ceded by " + from;
        case DealTermType::CedeTile:
            return "tile (" + std::to_string(term.tileCoord.q) + "," + std::to_string(term.tileCoord.r)
                   + ") ceded by " + from;
        case DealTermType::OpenBorders:
            return "Open Borders (" + std::to_string(term.duration) + " turns)";
        case DealTermType::NonAggression:
            return "Non-Aggression Pact (" + std::to_string(term.duration) + " turns)";
        case DealTermType::MutualDefense:
            return "Mutual Defence (" + std::to_string(term.duration) + " turns)";
        case DealTermType::ArmsLimitation:
            return "Arms Limitation to " + std::to_string(term.maxMilitaryUnits) + " units";
        case DealTermType::DemilitarizedZone:
            return "Demilitarized Zone (" + std::to_string(term.zoneRadius) + " tiles)";
        case DealTermType::WarGuilt:
            return from + " accepts war guilt";
        case DealTermType::GoodsExchange:
            return std::to_string(term.goodAmount) + " of good " + std::to_string(term.goodId) + " (" + from
                   + " -> " + to + ")";
        case DealTermType::MostFavoredNation:
            return "Most Favoured Nation";
        case DealTermType::ExclusiveAccess:
            return "Exclusive access to good " + std::to_string(term.goodId);
        default:
            return "Unknown term";
    }
}

ErrorCode requestProposeDeal(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, GlobalDealTracker& tracker,
                             DiplomacyManager& diplomacy, const DiplomaticDeal& deal, int32_t currentTurn) {
    const ErrorCode shape = proposalShapeValid(gameState, diplomacy, deal);
    if (shape != ErrorCode::Ok) {
        return shape;
    }
    // A human recipient answers from the inbox; headless runs mark nobody human,
    // so every proposal there resolves by valuation.
    const aoc::game::Player* recipient = gameState.player(deal.playerB);
    if (recipient != nullptr && recipient->isHuman()) {
        for (const PendingProposal& pending : gameState.pendingProposals()) {
            if (pending.from == deal.playerA && pending.to == deal.playerB) {
                return ErrorCode::InvalidState; // one open proposal per pair
            }
        }
        PendingProposal pending;
        pending.from         = deal.playerA;
        pending.to           = deal.playerB;
        pending.deal         = deal;
        pending.proposedTurn = currentTurn;
        pending.expiresTurn  = currentTurn + PROPOSAL_TTL_TURNS;
        gameState.pendingProposals().push_back(std::move(pending));
        notify(deal.playerB, deal.playerA, "Deal proposed",
               civName(gameState, deal.playerA) + " offers: " + termsText(gameState, deal), 5);
        LOG_INFO("Player %u proposed a deal with %zu terms to player %u", static_cast<unsigned>(deal.playerA),
                 deal.terms.size(), static_cast<unsigned>(deal.playerB));
        return ErrorCode::Ok;
    }
    const aoc::game::Player* proposer = gameState.player(deal.playerA);
    const bool humanProposer          = proposer != nullptr && proposer->isHuman();
    if (!aiAcceptsDeal(gameState, diplomacy, deal.playerB, deal)) {
        LOG_INFO("Player %u declined a deal from player %u (value %d)", static_cast<unsigned>(deal.playerB),
                 static_cast<unsigned>(deal.playerA), dealValueFor(gameState, diplomacy, deal.playerB, deal));
        if (humanProposer) {
            notify(deal.playerA, deal.playerB, "Deal declined",
                   civName(gameState, deal.playerB) + " declined: " + termsText(gameState, deal), 4);
        }
        return ErrorCode::InvalidState;
    }
    const ErrorCode applied = applyDeal(gameState, grid, tracker, diplomacy, deal, currentTurn);
    if (humanProposer && applied == ErrorCode::Ok) {
        notify(deal.playerA, deal.playerB, "Deal accepted",
               civName(gameState, deal.playerB) + " accepted: " + termsText(gameState, deal), 4);
    }
    return applied;
}

ErrorCode requestRespondToProposal(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                                   GlobalDealTracker& tracker, DiplomacyManager& diplomacy, PlayerId responder,
                                   std::size_t index, bool accept, int32_t currentTurn) {
    std::vector<PendingProposal>& inbox = gameState.pendingProposals();
    if (index >= inbox.size()) {
        return ErrorCode::EntityNotFound;
    }
    if (inbox[index].to != responder) {
        return ErrorCode::InvalidState;
    }
    const DiplomaticDeal deal = inbox[index].deal;
    inbox.erase(inbox.begin() + static_cast<std::ptrdiff_t>(index));
    if (!accept) {
        LOG_INFO("Player %u rejected a deal from player %u", static_cast<unsigned>(responder),
                 static_cast<unsigned>(deal.playerA));
        return ErrorCode::Ok;
    }
    return applyDeal(gameState, grid, tracker, diplomacy, deal, currentTurn);
}

void expireProposals(aoc::game::GameState& gameState, int32_t currentTurn) {
    std::vector<PendingProposal>& inbox = gameState.pendingProposals();
    std::vector<PendingProposal> kept;
    kept.reserve(inbox.size());
    for (PendingProposal& p : inbox) {
        if (p.expiresTurn > currentTurn) {
            kept.push_back(std::move(p));
            continue;
        }
        notify(p.to, p.from, "Proposal expired",
               "The offer from " + civName(gameState, p.from) + " lapsed: " + termsText(gameState, p.deal), 2);
        LOG_INFO("Deal proposal from player %u to %u expired on turn %d", static_cast<unsigned>(p.from),
                 static_cast<unsigned>(p.to), currentTurn);
    }
    inbox = std::move(kept);
}

bool aiOfferPeace(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, GlobalDealTracker& tracker,
                  DiplomacyManager& diplomacy, PlayerId loser, PlayerId winner, int32_t currentTurn) {
    const aoc::game::Player* me = gameState.player(loser);
    if (me == nullptr || loser >= diplomacy.playerCount() || winner >= diplomacy.playerCount()
        || !diplomacy.isAtWar(loser, winner)) {
        return false;
    }
    DiplomaticDeal deal;
    deal.playerA = loser;
    deal.playerB = winner;
    DealTerm term{};
    term.fromPlayer = loser;
    term.toPlayer   = winner;
    if (me->treasury() > 0) {
        term.type     = DealTermType::GoldLump;
        term.goldLump = std::max<int32_t>(1, static_cast<int32_t>(me->treasury() / 10));
    } else {
        term.type     = DealTermType::NonAggression;
        term.duration = 30;
    }
    deal.terms.push_back(term);
    return requestProposeDeal(gameState, grid, tracker, diplomacy, deal, currentTurn) == ErrorCode::Ok;
}

bool aiOfferOpenBorders(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, GlobalDealTracker& tracker,
                        DiplomacyManager& diplomacy, PlayerId ai, PlayerId other, int32_t currentTurn) {
    if (ai >= diplomacy.playerCount() || other >= diplomacy.playerCount() || ai == other) {
        return false;
    }
    const PairwiseRelation& rel = diplomacy.relation(ai, other);
    if (!rel.hasMet || rel.isAtWar || rel.hasOpenBorders || rel.totalScore() < OPEN_BORDERS_MIN_SCORE) {
        return false;
    }
    DiplomaticDeal deal;
    deal.playerA = ai;
    deal.playerB = other;
    DealTerm term{};
    term.type       = DealTermType::OpenBorders;
    term.fromPlayer = ai;
    term.toPlayer   = other;
    term.duration   = OPEN_BORDERS_TURNS;
    deal.terms.push_back(term);
    return requestProposeDeal(gameState, grid, tracker, diplomacy, deal, currentTurn) == ErrorCode::Ok;
}

} // namespace aoc::sim
