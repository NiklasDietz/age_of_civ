/**
 * @file DealProposals.cpp
 * @brief Deal proposals: inbox, valuation, routing (see DealProposals.hpp).
 */

#include "aoc/simulation/diplomacy/DealProposals.hpp"
#include "aoc/simulation/economy/Market.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/diplomacy/DiplomacyActions.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/event/GameNotifications.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>
#include <map>
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
        if (atWar && (term.type == DealTermType::OpenBorders || term.type == DealTermType::SupplyContract)) {
            return ErrorCode::InvalidState;
        }
        if (term.type == DealTermType::SupplyContract && diplomacy.hasAnyEmbargo(a, b)) {
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
    const ErrorCode accepted =
        acceptDeal(gameState, grid, tracker, static_cast<int32_t>(index), &diplomacy);
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

std::string civName(const aoc::game::GameState& gameState, PlayerId id) {
    const aoc::game::Player* p = gameState.player(id);
    if (p == nullptr) {
        return "P" + std::to_string(static_cast<unsigned>(id));
    }
    const CivilizationDef& def = civDef(p->civId());
    return def.name.empty() ? "P" + std::to_string(static_cast<unsigned>(id)) : std::string(def.name);
}

namespace {

/// Units of `goodId` across every city `player` owns.
[[nodiscard]] int32_t goodsHeld(const aoc::game::Player& player, uint16_t goodId) {
    int32_t held = 0;
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (city != nullptr && city->owner() == player.id()) {
            held += city->stockpile().getAmount(goodId);
        }
    }
    return held;
}

[[nodiscard]] int32_t needOf(const aoc::game::Player& player, uint16_t goodId) {
    const std::unordered_map<uint16_t, int32_t>& needs = player.economy().totalNeeds;
    const std::unordered_map<uint16_t, int32_t>::const_iterator it = needs.find(goodId);
    return it == needs.end() ? 0 : std::max(0, it->second);
}

/// Whether `giver` is the only civ on the map holding any of `goodId`.
[[nodiscard]] bool soleSource(const aoc::game::GameState& gameState, PlayerId giver, uint16_t goodId) {
    bool giverHolds = false;
    for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
        if (other == nullptr || goodsHeld(*other, goodId) <= 0) {
            continue;
        }
        if (other->id() == giver) {
            giverHolds = true;
        } else {
            return false;
        }
    }
    return giverHolds;
}

[[nodiscard]] bool atWarWithAnyone(const DiplomacyManager& diplomacy, PlayerId who) {
    for (uint8_t other = 0; other < diplomacy.playerCount(); ++other) {
        if (other != who && diplomacy.isAtWar(who, static_cast<PlayerId>(other))) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] int32_t receivePercent(int32_t have, int32_t need) {
    if (have == 0 && need > 0) {
        return GOODS_MISSING_PCT;
    }
    if (have < need) {
        return GOODS_SHORT_PCT;
    }
    if (have == 0) {
        return GOODS_NEUTRAL_PCT;
    }
    return GOODS_SURPLUS_PCT;
}

[[nodiscard]] int32_t givePercent(int32_t have, int32_t need, int32_t amount) {
    const int32_t left = have - amount;
    if (left < need) {
        return GIVE_SHORTFALL_PCT;
    }
    if (left < need + amount) {
        return GIVE_THIN_PCT;
    }
    return GIVE_SURPLUS_PCT;
}

} // namespace

int32_t goodsValueFor(const aoc::game::GameState& gameState, const DiplomacyManager& diplomacy,
                      const Market* market, PlayerId evaluator, PlayerId counterparty, uint16_t goodId,
                      int32_t amount, GoodsSide side) {
    const aoc::game::Player* me = gameState.player(evaluator);
    if (me == nullptr || amount <= 0 || goodId >= goods::GOOD_COUNT) {
        return 0;
    }
    const int32_t unit = std::max(1, market != nullptr ? market->price(goodId)
                                                       : static_cast<int32_t>(goodDef(goodId).basePrice));
    const int32_t need = needOf(*me, goodId);
    const int32_t have = goodsHeld(*me, goodId);
    int32_t pct = side == GoodsSide::Receive ? receivePercent(have, need) : givePercent(have, need, amount);
    const PlayerId giver = side == GoodsSide::Give ? evaluator : counterparty;
    if (soleSource(gameState, giver, goodId)) {
        pct = pct * SOLE_SOURCE_PCT / 100;
    }
    if (goodDef(goodId).isStrategic && atWarWithAnyone(diplomacy, evaluator)) {
        pct = pct * WAR_STRATEGIC_PCT / 100;
    }
    if (side == GoodsSide::Give && me->treasury() < CASH_POOR_TREASURY) {
        pct = pct * CASH_POOR_PCT / 100;
    }
    pct = std::clamp(pct, GOODS_VALUE_MIN_PCT, GOODS_VALUE_MAX_PCT);
    return unit * amount * pct / 100;
}

int32_t dealValueFor(const aoc::game::GameState& gameState, const DiplomacyManager& diplomacy,
                     PlayerId evaluator, const DiplomaticDeal& deal, const Market* market) {
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
            case DealTermType::GoodsExchange: {
                // Through the one valuation seam, from the evaluator's side.
                // Before 2026-09-06 these terms were scored at exactly zero, and
                // until 2026-09-11 at flat base price with no idea of need.
                const GoodsSide side = term.toPlayer == evaluator ? GoodsSide::Receive : GoodsSide::Give;
                const int32_t worth  = goodsValueFor(gameState, diplomacy, market, evaluator, other,
                                                     term.goodId, term.goodAmount, side);
                value += side == GoodsSide::Receive ? worth : -worth;
                break;
            }
            case DealTermType::ExclusiveAccess: {
                // A standing claim, priced as EXCLUSIVE_ACCESS_SHIPMENTS units:
                // granting it costs the seller its other customers.
                const GoodsSide side = term.toPlayer == evaluator ? GoodsSide::Receive : GoodsSide::Give;
                const int32_t worth  = goodsValueFor(gameState, diplomacy, market, evaluator, other,
                                                     term.goodId, EXCLUSIVE_ACCESS_SHIPMENTS, side);
                value += side == GoodsSide::Receive ? worth : -worth;
                break;
            }
            case DealTermType::SupplyContract: {
                // A stream: the goods leg through the seam, discounted for the
                // wait, against the gold leg, both over the term.
                const GoodsSide side = term.toPlayer == evaluator ? GoodsSide::Receive : GoodsSide::Give;
                const int32_t goodsLeg = goodsValueFor(gameState, diplomacy, market, evaluator, other,
                                                       term.goodId, term.goodAmount, side)
                                         * term.duration * CONTRACT_VALUE_PCT / 100;
                const int32_t goldLeg = term.goldPerTurn * term.duration;
                value += side == GoodsSide::Receive ? goodsLeg - goldLeg : goldLeg - goodsLeg;
                break;
            }
            case DealTermType::MostFavoredNation:
                // Deliberately still neutral: nothing enforces it yet, so
                // pricing it would have the AI pay for a promise that does
                // nothing. See the note in DealTerms.hpp.
                break;
            default:
                break;
        }
    }
    // Goodwill only sweetens: a hostile evaluator still takes a gift, but pacts
    // already carry their stance penalties above.
    return value + std::max(score, 0) / 4;
}

bool aiAcceptsDeal(const aoc::game::GameState& gameState, const DiplomacyManager& diplomacy, PlayerId ai,
                   const DiplomaticDeal& deal, const Market* market) {
    return dealValueFor(gameState, diplomacy, ai, deal, market) >= 0;
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
            return std::to_string(term.goodAmount) + " " + std::string(goodDef(term.goodId).name) + " (" + from
                   + " -> " + to + ")";
        case DealTermType::MostFavoredNation:
            return "Most Favoured Nation";
        case DealTermType::ExclusiveAccess:
            return "Exclusive access to " + std::string(goodDef(term.goodId).name) + " (" + from + " -> " + to
                   + ")";
        case DealTermType::SupplyContract:
            return std::to_string(term.goodAmount) + " " + std::string(goodDef(term.goodId).name)
                   + " per turn for " + std::to_string(term.duration) + " turns at "
                   + std::to_string(term.goldPerTurn) + " gold per turn (" + from + " -> " + to + ")";
        default:
            return "Unknown term";
    }
}

ErrorCode requestProposeDeal(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, GlobalDealTracker& tracker,
                             DiplomacyManager& diplomacy, const DiplomaticDeal& deal, int32_t currentTurn,
                             const Market* market) {
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
    if (!aiAcceptsDeal(gameState, diplomacy, deal.playerB, deal, market)) {
        LOG_INFO("Player %u declined a deal from player %u (value %d)", static_cast<unsigned>(deal.playerB),
                 static_cast<unsigned>(deal.playerA),
                 dealValueFor(gameState, diplomacy, deal.playerB, deal, market));
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
        // Reparations, not a lump sum. DealTermType::WarReparations had
        // enforcement, AI valuation, UI text and a save round-trip, and no
        // surface anywhere built the term -- its switch case could not be
        // entered. A beaten civ paying tribute over time is what it is for.
        term.type        = DealTermType::WarReparations;
        term.goldPerTurn = std::max<int32_t>(1, static_cast<int32_t>(me->treasury() / 20));
        term.duration    = REPARATIONS_DURATION_TURNS;
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

std::optional<PurchaseTarget> aiPurchaseTarget(const aoc::game::Player& buyer) {
    const std::unordered_map<uint16_t, int32_t>& needs = buyer.economy().totalNeeds;
    for (const uint16_t luxId : luxuryGoodIds()) {
        const std::unordered_map<uint16_t, int32_t>::const_iterator it = needs.find(luxId);
        if (it != needs.end() && it->second > 0) {
            return PurchaseTarget{luxId, LUXURY_DEAL_UNITS};
        }
    }
    std::optional<PurchaseTarget> best;
    for (const std::pair<const uint16_t, int32_t>& need : needs) {
        if (need.second <= 0) {
            continue;
        }
        const bool better = !best.has_value() || need.second > best->amount ||
                            (need.second == best->amount && need.first < best->goodId);
        if (better) {
            best = PurchaseTarget{need.first, need.second};
        }
    }
    if (best.has_value()) {
        best->amount = std::min(best->amount, GOODS_DEAL_MAX_UNITS);
    }
    return best;
}

std::vector<WorldMarketRow> worldMarketRows(const aoc::game::GameState& gameState,
                                            const DiplomacyManager* diplomacy, PlayerId viewer) {
    std::map<uint16_t, WorldMarketRow> rows;
    for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
        if (other == nullptr) {
            continue;
        }
        const PlayerId id = other->id();
        const bool known  = id == viewer || diplomacy == nullptr ||
                           (viewer < diplomacy->playerCount() && id < diplomacy->playerCount() &&
                            diplomacy->haveMet(viewer, id));
        if (!known) {
            continue;
        }
        std::map<uint16_t, int32_t> held;
        for (const std::unique_ptr<aoc::game::City>& city : other->cities()) {
            if (city == nullptr || city->owner() != id) {
                continue;
            }
            for (const std::pair<const uint16_t, int32_t>& entry : city->stockpile().goods) {
                if (entry.second > 0 && goodDef(entry.first).category != GoodCategory::Monetary) {
                    held[entry.first] += entry.second;
                }
            }
        }
        for (const std::pair<const uint16_t, int32_t>& entry : held) {
            WorldMarketRow& row = rows[entry.first];
            row.goodId          = entry.first;
            row.holders.emplace_back(id, entry.second);
        }
        for (const std::pair<const uint16_t, int32_t>& need : other->economy().totalNeeds) {
            if (need.second > 0 && goodDef(need.first).category != GoodCategory::Monetary) {
                WorldMarketRow& row = rows[need.first];
                row.goodId          = need.first;
                row.seekers.push_back(id);
            }
        }
    }
    std::vector<WorldMarketRow> out;
    out.reserve(rows.size());
    for (std::pair<const uint16_t, WorldMarketRow>& entry : rows) {
        std::sort(entry.second.holders.begin(), entry.second.holders.end());
        std::sort(entry.second.seekers.begin(), entry.second.seekers.end());
        out.push_back(std::move(entry.second));
    }
    return out;
}

bool aiOfferToBuy(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, GlobalDealTracker& tracker,
                  DiplomacyManager& diplomacy, PlayerId buyer, uint16_t goodId, int32_t qty,
                  int32_t currentTurn, const Market* market) {
    const aoc::game::Player* buyerPtr = gameState.player(buyer);
    if (buyerPtr == nullptr || qty <= 0 || goodId >= goods::GOOD_COUNT) {
        return false;
    }
    for (const std::unique_ptr<aoc::game::Player>& sellerPtr : gameState.players()) {
        if (sellerPtr == nullptr || sellerPtr->id() == buyer) {
            continue;
        }
        const PlayerId seller       = sellerPtr->id();
        const PairwiseRelation& rel = diplomacy.relation(buyer, seller);
        if (!rel.hasMet || rel.isAtWar || goodsHeld(*sellerPtr, goodId) < qty) {
            continue;
        }
        // What the shipment is worth to us from this seller: a sole source
        // commands more. The seller's own valuation answers the offer.
        const int32_t offer = goodsValueFor(gameState, diplomacy, market, buyer, seller, goodId, qty,
                                            GoodsSide::Receive);
        if (offer <= 0 || buyerPtr->treasury() < offer) {
            continue;
        }
        DiplomaticDeal deal;
        deal.playerA        = buyer;
        deal.playerB        = seller;
        deal.turnsRemaining = 0; // both terms settle on acceptance
        DealTerm shipment{};
        shipment.type       = DealTermType::GoodsExchange;
        shipment.fromPlayer = seller;
        shipment.toPlayer   = buyer;
        shipment.goodId     = goodId;
        shipment.goodAmount = qty;
        DealTerm payment{};
        payment.type       = DealTermType::GoldLump;
        payment.fromPlayer = buyer;
        payment.toPlayer   = seller;
        payment.goldLump   = offer;
        deal.terms         = {shipment, payment};
        if (requestProposeDeal(gameState, grid, tracker, diplomacy, deal, currentTurn, market) == ErrorCode::Ok) {
            LOG_INFO("AI %u offers %d gold for %d of good %u to player %u", static_cast<unsigned>(buyer),
                     offer, qty, static_cast<unsigned>(goodId), static_cast<unsigned>(seller));
            return true;
        }
    }
    return false;
}

} // namespace aoc::sim
