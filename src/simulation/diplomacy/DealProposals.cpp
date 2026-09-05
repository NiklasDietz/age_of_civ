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
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"

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

ErrorCode applyDeal(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, GlobalDealTracker& tracker,
                    const DiplomaticDeal& deal) {
    const std::size_t index = tracker.activeDeals.size();
    const ErrorCode proposed = proposeDeal(gameState, tracker, deal);
    if (proposed != ErrorCode::Ok) {
        return proposed;
    }
    return acceptDeal(gameState, grid, tracker, static_cast<int32_t>(index));
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
                             const DiplomacyManager& diplomacy, const DiplomaticDeal& deal, int32_t currentTurn) {
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
        LOG_INFO("Player %u proposed a deal with %zu terms to player %u", static_cast<unsigned>(deal.playerA),
                 deal.terms.size(), static_cast<unsigned>(deal.playerB));
        return ErrorCode::Ok;
    }
    if (!aiAcceptsDeal(gameState, diplomacy, deal.playerB, deal)) {
        LOG_INFO("Player %u declined a deal from player %u (value %d)", static_cast<unsigned>(deal.playerB),
                 static_cast<unsigned>(deal.playerA), dealValueFor(gameState, diplomacy, deal.playerB, deal));
        return ErrorCode::InvalidState;
    }
    return applyDeal(gameState, grid, tracker, deal);
}

ErrorCode requestRespondToProposal(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                                   GlobalDealTracker& tracker, PlayerId responder, std::size_t index, bool accept) {
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
    return applyDeal(gameState, grid, tracker, deal);
}

void expireProposals(aoc::game::GameState& gameState, int32_t currentTurn) {
    std::vector<PendingProposal>& inbox = gameState.pendingProposals();
    const std::size_t before = inbox.size();
    inbox.erase(std::remove_if(inbox.begin(), inbox.end(),
                               [currentTurn](const PendingProposal& p) { return p.expiresTurn <= currentTurn; }),
                inbox.end());
    if (inbox.size() != before) {
        LOG_INFO("%zu deal proposal(s) expired on turn %d", before - inbox.size(), currentTurn);
    }
}

} // namespace aoc::sim
