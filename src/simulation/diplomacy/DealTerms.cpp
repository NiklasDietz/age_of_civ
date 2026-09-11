/**
 * @file DealTerms.cpp
 * @brief Diplomatic deal enforcement and processing.
 *
 * The CedeCity term still carries an EntityId from the legacy struct definition.
 * City lookup for that term searches all players' city lists rather than going
 * through the ECS world. All other ECS access has been removed.
 */

#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/turn/TurnEventLog.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <map>

namespace aoc::sim {

namespace {

/// Reown a single tile to `receiver`, but only if `giver` currently owns it or
/// it is unowned -- a negotiated cession must never seize a third party's land.
void reownTileIfGiverOrUnowned(aoc::map::HexGrid& grid, hex::AxialCoord coord, PlayerId giver,
                               PlayerId receiver) {
    // isValid before toIndex: toIndex only asserts bounds (a no-op under
    // -DNDEBUG) and otherwise returns a wrapped/out-of-range index, so an
    // edge city's off-map ring neighbour would abort or corrupt a stray tile.
    if (!grid.isValid(coord)) {
        return;
    }
    const int32_t idx      = grid.toIndex(coord);
    const PlayerId current = grid.owner(idx);
    if (current == giver || current == INVALID_PLAYER) {
        grid.setOwner(idx, receiver);
    }
}

/// A ceded city brings its immediate territory (center + 6 ring tiles) with it.
/// Unlike a revolt (Secession.cpp flips the ring unconditionally), a sale only
/// moves tiles the giver actually held, so it cannot annex a neighbour's land.
void reownCityFootprint(aoc::map::HexGrid& grid, hex::AxialCoord center, PlayerId giver,
                        PlayerId receiver) {
    reownTileIfGiverOrUnowned(grid, center, giver, receiver);
    for (const hex::AxialCoord& n : hex::neighbors(center)) {
        reownTileIfGiverOrUnowned(grid, n, giver, receiver);
    }
}

/// Validate that every asset-transferring term of `deal` can execute against
/// Total units of `goodId` held across every city `player` owns.
[[nodiscard]] int32_t playerGoodsHeld(const aoc::game::Player& player, uint16_t goodId) {
    int32_t total = 0;
    for (const std::unique_ptr<aoc::game::City>& cityPtr : player.cities()) {
        if (cityPtr == nullptr || cityPtr->owner() != player.id()) { continue; }
        total += cityPtr->stockpile().getAmount(goodId);
    }
    return total;
}

/// Move `amount` of `goodId` from `giver`'s cities into `receiver`'s. Goods are
/// held per city, not per player, so a transfer has to be gathered from
/// wherever the giver keeps it and landed somewhere the receiver owns.
/// Returns what was actually moved.
int32_t transferGoods(aoc::game::Player& giver, aoc::game::Player& receiver, uint16_t goodId,
                      int32_t amount) {
    aoc::game::City* landing = nullptr;
    for (const std::unique_ptr<aoc::game::City>& cityPtr : receiver.cities()) {
        if (cityPtr == nullptr || cityPtr->owner() != receiver.id()) { continue; }
        landing = cityPtr.get();
        if (cityPtr->isOriginalCapital()) { break; } // prefer the capital
    }
    if (landing == nullptr) { return 0; }

    int32_t moved = 0;
    for (const std::unique_ptr<aoc::game::City>& cityPtr : giver.cities()) {
        if (moved >= amount) { break; }
        if (cityPtr == nullptr || cityPtr->owner() != giver.id()) { continue; }
        const int32_t take = std::min(amount - moved, cityPtr->stockpile().getAmount(goodId));
        if (take <= 0) { continue; }
        if (!cityPtr->stockpile().consumeGoods(goodId, take)) { continue; }
        moved += take;
    }
    if (moved > 0) { landing->stockpile().addGoods(goodId, moved); }
    return moved;
}

/// Lift the third-party embargoes an ExclusiveAccess term put in place.
///
/// Exclusivity must not outlive its contract. Caveat worth stating: this
/// removes the good from those relations without asking why it was there, so an
/// embargo declared independently on the same good by the same civ is lifted
/// too. Tracking provenance per embargo entry would need a save-format change;
/// an exclusivity deal quietly becoming permanent is the worse failure.
void liftExclusiveAccess(const aoc::game::GameState& gameState, DiplomacyManager& diplomacy,
                         const DiplomaticDeal& deal);

/// Terms that hold for `duration` turns, as opposed to ones that settle on
/// acceptance (gold, goods, cessions, guilt).
[[nodiscard]] bool isStandingTerm(DealTermType type) {
    switch (type) {
        case DealTermType::OpenBorders:
        case DealTermType::NonAggression:
        case DealTermType::MutualDefense:
        case DealTermType::ArmsLimitation:
        case DealTermType::DemilitarizedZone:
        case DealTermType::MostFavoredNation:
        case DealTermType::ExclusiveAccess:
        case DealTermType::SupplyContract:
        case DealTermType::WarReparations:
            return true;
        default:
            return false;
    }
}

/// Every major seat other than the two parties to the deal.
[[nodiscard]] std::vector<PlayerId> thirdParties(const aoc::game::GameState& gameState,
                                                 PlayerId a, PlayerId b) {
    std::vector<PlayerId> out;
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        if (p == nullptr) { continue; }
        if (p->id() == a || p->id() == b) { continue; }
        out.push_back(p->id());
    }
    return out;
}

void liftExclusiveAccess(const aoc::game::GameState& gameState, DiplomacyManager& diplomacy,
                         const DiplomaticDeal& deal) {
    for (const DealTerm& term : deal.terms) {
        if (term.type != DealTermType::ExclusiveAccess) { continue; }
        for (const PlayerId other : thirdParties(gameState, term.fromPlayer, term.toPlayer)) {
            PairwiseRelation& rel = diplomacy.relation(term.fromPlayer, other);
            std::vector<uint16_t>& list = rel.embargoedGoods;
            list.erase(std::remove(list.begin(), list.end(), term.goodId), list.end());
        }
    }
}

/// the current state, WITHOUT mutating anything. acceptDeal calls this first
/// so a deal that cannot execute in full never half-applies -- the bug this
/// guards is ceding a city (or tile) and then failing to collect the gold,
/// which handed the asset away for free. Deferred terms (reparations, DMZ,
/// pacts) are enforced later in processDeals and need no up-front check here.
[[nodiscard]] ErrorCode validateImmediateTerms(const aoc::game::GameState& gameState,
                                               const aoc::map::HexGrid& grid,
                                               const DiplomaticDeal& deal) {
    // Cumulative gold owed per payer: two lumps that each fit individually but
    // together overdraw the treasury must still be rejected as a group.
    std::map<PlayerId, CurrencyAmount> goldOwed;

    // Each term is checked against the pre-apply world, not a forward
    // simulation of the terms before it. That is exact for the deals the game
    // builds today (one asset transfer plus one payment). A future deal whose
    // later term only becomes valid after an earlier one applies (e.g. two
    // chained CedeTile terms) would need a simulate-forward model here.
    for (const DealTerm& term : deal.terms) {
        switch (term.type) {
        case DealTermType::CedeCity: {
            const aoc::game::Player* fromPlayer = gameState.player(term.fromPlayer);
            if (fromPlayer == nullptr) {
                return ErrorCode::InvalidState;
            }
            bool cityFound = false;
            for (const std::unique_ptr<aoc::game::City>& cityPtr : fromPlayer->cities()) {
                if (cityPtr != nullptr && cityPtr->location() == term.tileCoord) {
                    cityFound = true;
                    break;
                }
            }
            if (!cityFound) {
                return ErrorCode::InvalidState;
            }
            break;
        }
        case DealTermType::CedeTile: {
            if (!grid.isValid(term.tileCoord)) {
                return ErrorCode::InvalidState;
            }
            const int32_t idx = grid.toIndex(term.tileCoord);
            if (grid.owner(idx) != term.fromPlayer) {
                return ErrorCode::InvalidState;
            }
            bool touchesReceiver = false;
            for (const hex::AxialCoord& n : hex::neighbors(term.tileCoord)) {
                if (!grid.isValid(n)) {
                    continue;
                }
                if (grid.owner(grid.toIndex(n)) == term.toPlayer) {
                    touchesReceiver = true;
                    break;
                }
            }
            if (!touchesReceiver) {
                return ErrorCode::InvalidState;
            }
            break;
        }
        case DealTermType::GoodsExchange: {
            if (term.goodAmount <= 0) { break; } // no-op, not a failure
            const aoc::game::Player* giver    = gameState.player(term.fromPlayer);
            const aoc::game::Player* receiver = gameState.player(term.toPlayer);
            if (giver == nullptr || receiver == nullptr) {
                return ErrorCode::InvalidState;
            }
            // The receiver needs somewhere to put it.
            bool receiverHasCity = false;
            for (const std::unique_ptr<aoc::game::City>& c : receiver->cities()) {
                if (c != nullptr && c->owner() == receiver->id()) { receiverHasCity = true; break; }
            }
            if (!receiverHasCity) { return ErrorCode::InvalidState; }
            if (playerGoodsHeld(*giver, term.goodId) < term.goodAmount) {
                return ErrorCode::InsufficientResources;
            }
            break;
        }
        case DealTermType::SupplyContract: {
            if (term.goodAmount <= 0 || term.goldPerTurn < 0 || term.duration <= 0 ||
                term.duration > SUPPLY_CONTRACT_MAX_TURNS) {
                return ErrorCode::InvalidArgument;
            }
            const aoc::game::Player* seller = gameState.player(term.fromPlayer);
            const aoc::game::Player* buyer  = gameState.player(term.toPlayer);
            if (seller == nullptr || buyer == nullptr || buyer->ownedCityCount() == 0) {
                return ErrorCode::InvalidState;
            }
            // The seller can make the first shipment and the buyer the first
            // instalment; later turns are the contract's own risk.
            if (playerGoodsHeld(*seller, term.goodId) < term.goodAmount) {
                return ErrorCode::InsufficientResources;
            }
            goldOwed[term.toPlayer] += static_cast<CurrencyAmount>(term.goldPerTurn);
            break;
        }
        case DealTermType::GoldLump: {
            if (term.goldLump <= 0) {
                break;
            } // no-op, not a failure
            const aoc::game::Player* payer    = gameState.player(term.fromPlayer);
            const aoc::game::Player* receiver = gameState.player(term.toPlayer);
            if (payer == nullptr || receiver == nullptr) {
                return ErrorCode::InvalidState;
            }
            goldOwed[term.fromPlayer] += static_cast<CurrencyAmount>(term.goldLump);
            break;
        }
        default:
            break;
        }
    }

    for (const std::pair<const PlayerId, CurrencyAmount>& owed : goldOwed) {
        const aoc::game::Player* payer = gameState.player(owed.first);
        if (payer == nullptr || payer->treasury() < owed.second) {
            return ErrorCode::InsufficientResources;
        }
    }
    return ErrorCode::Ok;
}

} // namespace

ErrorCode proposeDeal(aoc::game::GameState& /*gameState*/, GlobalDealTracker& tracker,
                      DiplomaticDeal deal) {
    tracker.activeDeals.push_back(std::move(deal));
    LOG_INFO("Deal proposed between player %u and player %u with %zu terms",
             static_cast<unsigned>(tracker.activeDeals.back().playerA),
             static_cast<unsigned>(tracker.activeDeals.back().playerB),
             tracker.activeDeals.back().terms.size());
    return ErrorCode::Ok;
}

ErrorCode acceptDeal(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                     GlobalDealTracker& tracker, int32_t dealIndex,
                     DiplomacyManager* diplomacy) {
    if (dealIndex < 0 || dealIndex >= static_cast<int32_t>(tracker.activeDeals.size())) {
        return ErrorCode::InvalidArgument;
    }

    DiplomaticDeal& deal = tracker.activeDeals[static_cast<std::size_t>(dealIndex)];

    // Atomicity gate: reject before touching any portfolio if any asset
    // transfer cannot execute in full. Without this, terms applied one at a
    // time could leave a deal half-done (city ceded, gold never paid).
    const ErrorCode validation = validateImmediateTerms(gameState, grid, deal);
    if (validation != ErrorCode::Ok) {
        LOG_INFO("Deal between player %u and player %u rejected: %s",
                 static_cast<unsigned>(deal.playerA), static_cast<unsigned>(deal.playerB),
                 describeError(validation).data());
        // Roll the proposal back. processDeals only ticks and erases accepted
        // deals, so a rejected one left in place would linger forever; the
        // erase invalidates `deal`, so nothing below may touch it.
        tracker.activeDeals.erase(tracker.activeDeals.begin() +
                                  static_cast<std::ptrdiff_t>(dealIndex));
        return validation;
    }

    deal.isAccepted = true;
    // A deal made only of standing terms lives exactly as long as its longest
    // one; a mixed deal lives at least that long. A deal that settles on
    // acceptance keeps whatever the proposer set (normally nothing). Pacts
    // used to be left at zero, so a 30-turn pact left the tracker next turn.
    int32_t longestStanding = 0;
    bool onlyStanding       = true;
    for (const DealTerm& term : deal.terms) {
        if (isStandingTerm(term.type)) {
            longestStanding = std::max(longestStanding, term.duration);
        } else {
            onlyStanding = false;
        }
    }
    if (longestStanding > 0) {
        deal.turnsRemaining = onlyStanding ? longestStanding
                                           : std::max(deal.turnsRemaining, longestStanding);
    }
    if (diplomacy != nullptr && diplomacy->eventLog() != nullptr) {
        diplomacy->eventLog()->record(TurnEventType::DealAccepted, deal.playerA, deal.playerB,
                                      static_cast<int32_t>(deal.terms.size()), 0,
                                      "Deal accepted");
    }

    for (const DealTerm& term : deal.terms) {
        switch (term.type) {
        case DealTermType::GoodsExchange: {
            // Declared, described in the UI, and executed by nobody until now:
            // goods could only ever move by trade route, so a negotiated deal
            // had no way to hand over the thing being negotiated about.
            if (term.goodAmount <= 0) { break; }
            aoc::game::Player* giver    = gameState.player(term.fromPlayer);
            aoc::game::Player* receiver = gameState.player(term.toPlayer);
            if (giver == nullptr || receiver == nullptr) { break; }
            const int32_t moved =
                transferGoods(*giver, *receiver, term.goodId, term.goodAmount);
            LOG_INFO("Deal: player %u handed %d of good %u to player %u",
                     static_cast<unsigned>(term.fromPlayer), moved,
                     static_cast<unsigned>(term.goodId),
                     static_cast<unsigned>(term.toPlayer));
            break;
        }
        case DealTermType::ExclusiveAccess: {
            // Sole access means everyone ELSE is cut off, which the per-good
            // embargo list already expresses and TradeRouteSystem already
            // enforces by seizing embargoed cargo. Nothing had to be invented.
            if (diplomacy == nullptr) { break; }
            for (const PlayerId other : thirdParties(gameState, term.fromPlayer, term.toPlayer)) {
                PairwiseRelation& rel = diplomacy->relation(term.fromPlayer, other);
                if (!rel.isGoodEmbargoed(term.goodId)) {
                    rel.embargoedGoods.push_back(term.goodId);
                }
            }
            LOG_INFO("Deal: player %u granted player %u exclusive access to good %u",
                     static_cast<unsigned>(term.fromPlayer),
                     static_cast<unsigned>(term.toPlayer),
                     static_cast<unsigned>(term.goodId));
            break;
        }
        case DealTermType::CedeCity: {
            // Match by axial location. Prior code stored
            // q*10000 + r in EntityId.index and decoded via the same
            // formula, which collides for coordinates outside the
            // default map bounds. DealTerm.tileCoord is authoritative;
            // cityEntity is retained only for backwards compatibility
            // with existing UI/AI call sites.
            aoc::game::Player* fromPlayer = gameState.player(term.fromPlayer);
            if (fromPlayer == nullptr) {
                break;
            }
            for (const std::unique_ptr<aoc::game::City>& cityPtr : fromPlayer->cities()) {
                if (cityPtr == nullptr) {
                    continue;
                }
                if (cityPtr->location() == term.tileCoord) {
                    LOG_INFO("City %s ceded to player %u", cityPtr->name().c_str(),
                             static_cast<unsigned>(term.toPlayer));
                    // transferCity moves the object between the two
                    // players' vectors, which erases the element this loop
                    // is standing on: read nothing from cityPtr after it,
                    // and break out at once.
                    const aoc::hex::AxialCoord ceded = cityPtr->location();
                    gameState.transferCity(ceded, term.toPlayer);
                    // Move the city's grid footprint to the new owner too;
                    // otherwise the tiles stay with the giver and the buyer
                    // owns a city surrounded by hostile territory.
                    reownCityFootprint(grid, ceded, term.fromPlayer, term.toPlayer);
                    break;
                }
            }
            break;
        }
        case DealTermType::WarGuilt: {
            aoc::game::Player* fromPlayer = gameState.player(term.fromPlayer);
            if (fromPlayer == nullptr) {
                break;
            }
            Grievance g{};
            g.type    = GrievanceType::BrokePromise;
            g.against = term.toPlayer;
            // Severity convention (Grievance.cpp:49-97): all grievances
            // are stored as NEGATIVE values and summed into a negative
            // opinion modifier. Prior +50 actually improved the
            // fromPlayer's opinion of toPlayer — opposite of intent.
            g.severity       = -50;
            g.turnsRemaining = 0; // Permanent (tickGrievances only
                                  // decrements when > 0)
            fromPlayer->grievances().grievances.push_back(g);
            break;
        }
        case DealTermType::CedeTile: {
            // Transfer ownership of a single border-adjacent hex. Validates
            // that the tile currently belongs to fromPlayer AND that at
            // least one neighbour hex belongs to toPlayer (no teleporting
            // tiles across the map).
            if (!grid.isValid(term.tileCoord)) {
                break;
            }
            const int32_t idx = grid.toIndex(term.tileCoord);
            if (grid.owner(idx) != term.fromPlayer) {
                LOG_INFO("CedeTile aborted: tile (%d,%d) not owned by player %u", term.tileCoord.q,
                         term.tileCoord.r, static_cast<unsigned>(term.fromPlayer));
                break;
            }
            bool touchesReceiver = false;
            for (const hex::AxialCoord& n : hex::neighbors(term.tileCoord)) {
                if (!grid.isValid(n)) {
                    continue;
                }
                if (grid.owner(grid.toIndex(n)) == term.toPlayer) {
                    touchesReceiver = true;
                    break;
                }
            }
            if (!touchesReceiver) {
                LOG_INFO("CedeTile aborted: tile (%d,%d) does not border player %u",
                         term.tileCoord.q, term.tileCoord.r, static_cast<unsigned>(term.toPlayer));
                break;
            }
            grid.setOwner(idx, term.toPlayer);
            LOG_INFO("Tile (%d,%d) ceded from player %u to player %u", term.tileCoord.q,
                     term.tileCoord.r, static_cast<unsigned>(term.fromPlayer),
                     static_cast<unsigned>(term.toPlayer));
            break;
        }
        case DealTermType::GoldLump: {
            aoc::game::Player* payer    = gameState.player(term.fromPlayer);
            aoc::game::Player* receiver = gameState.player(term.toPlayer);
            if (payer == nullptr || receiver == nullptr) {
                break;
            }
            const CurrencyAmount amt = static_cast<CurrencyAmount>(term.goldLump);
            if (amt <= 0) {
                break;
            }
            // Affordability (including the cumulative debt of multiple
            // lumps from one payer) was checked in validateImmediateTerms;
            // applying unconditionally here keeps the whole deal atomic.
            payer->setTreasury(payer->treasury() - amt);
            receiver->setTreasury(receiver->treasury() + amt);
            LOG_INFO("GoldLump: player %u paid %lld to player %u",
                     static_cast<unsigned>(term.fromPlayer), static_cast<long long>(amt),
                     static_cast<unsigned>(term.toPlayer));
            break;
        }
        default:
            break;
        }
    }

    LOG_INFO("Deal accepted between player %u and player %u", static_cast<unsigned>(deal.playerA),
             static_cast<unsigned>(deal.playerB));
    return ErrorCode::Ok;
}

void breakDeal(aoc::game::GameState& gameState, GlobalDealTracker& tracker,
               DiplomacyManager& diplomacy, PlayerId breaker, int32_t dealIndex) {
    if (dealIndex < 0 || dealIndex >= static_cast<int32_t>(tracker.activeDeals.size())) {
        return;
    }

    DiplomaticDeal& deal = tracker.activeDeals[static_cast<std::size_t>(dealIndex)];
    deal.isBroken        = true;

    // A broken contract stops granting what it granted.
    liftExclusiveAccess(gameState, diplomacy, deal);

    PlayerId victim = (deal.playerA == breaker) ? deal.playerB : deal.playerA;

    // NonAggression broken: severe penalties
    if (deal.hasTerm(DealTermType::NonAggression)) {
        // -30 reputation with victim
        diplomacy.addReputationModifier(breaker, victim, -30, 0); // permanent

        // -15 reputation with ALL known players (broadcast dishonor)
        diplomacy.broadcastReputationPenalty(breaker, -15, 60);

        // Grievance on the victim
        aoc::game::Player* victimPlayer = gameState.player(victim);
        if (victimPlayer != nullptr) {
            victimPlayer->grievances().addGrievance(GrievanceType::BrokeNonAggression, breaker);
        }

        LOG_INFO("Player %u BROKE Non-Aggression Pact with Player %u! "
                 "Severe diplomatic penalties applied.",
                 static_cast<unsigned>(breaker), static_cast<unsigned>(victim));
    } else {
        // Generic deal break: moderate penalty
        diplomacy.addReputationModifier(breaker, victim, -10, 30);

        aoc::game::Player* victimPlayer = gameState.player(victim);
        if (victimPlayer != nullptr) {
            victimPlayer->grievances().addGrievance(GrievanceType::BrokePromise, breaker);
        }

        LOG_INFO("Player %u broke deal with Player %u. Diplomatic consequences apply.",
                 static_cast<unsigned>(breaker), static_cast<unsigned>(victim));
    }
}

namespace {

/// Check if a player has military units within `radius` tiles of any tile
/// owned by `borderOwner`. Used for DMZ enforcement.
bool hasMilitaryUnitsNearBorder(const aoc::game::GameState& gameState,
                                const aoc::map::HexGrid& grid, PlayerId violator,
                                PlayerId borderOwner, int32_t radius) {
    const aoc::game::Player* player = gameState.player(violator);
    if (player == nullptr) {
        return false;
    }

    for (const std::unique_ptr<aoc::game::Unit>& unit : player->units()) {
        UnitClass uc = unit->typeDef().unitClass;
        if (uc == UnitClass::Settler || uc == UnitClass::Civilian || uc == UnitClass::Trader ||
            uc == UnitClass::Scout || uc == UnitClass::Religious) {
            continue;
        }

        aoc::hex::AxialCoord pos = unit->position();
        if (!grid.isValid(pos)) {
            continue;
        }

        // Check if this unit is within `radius` of any tile owned by borderOwner
        // by scanning hex rings 0..radius around the unit position.
        for (int32_t r = 0; r <= radius; ++r) {
            for (int32_t q = -r; q <= r; ++q) {
                int32_t rMin = std::max(-r, -q - r);
                int32_t rMax = std::min(r, -q + r);
                for (int32_t s = rMin; s <= rMax; ++s) {
                    aoc::hex::AxialCoord neighbor{pos.q + q, pos.r + s};
                    if (!grid.isValid(neighbor)) {
                        continue;
                    }
                    int32_t idx = grid.toIndex(neighbor);
                    if (grid.owner(idx) == borderOwner) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

} // anonymous namespace

namespace {

enum class ContractFate : uint8_t { Runs, Ended, Breached };

struct ContractCheck {
    ContractFate fate = ContractFate::Runs;
    PlayerId breaker  = INVALID_PLAYER;
};

/// Whether a deal carrying contracts can run this turn: not with a party
/// gone (no city to ship from or to), not at war, not under embargo.
[[nodiscard]] ContractCheck checkContractParties(const aoc::game::GameState& gameState,
                                                 const DiplomacyManager& diplomacy,
                                                 const DiplomaticDeal& deal) {
    const aoc::game::Player* a = gameState.player(deal.playerA);
    const aoc::game::Player* b = gameState.player(deal.playerB);
    if (a == nullptr || b == nullptr || a->ownedCityCount() == 0 || b->ownedCityCount() == 0) {
        return {ContractFate::Ended, INVALID_PLAYER};
    }
    const PairwiseRelation& rel = diplomacy.relation(deal.playerA, deal.playerB);
    if (rel.isAtWar) {
        const PlayerId aggressor = rel.lastWarAggressor;
        return {ContractFate::Breached,
                (aggressor == deal.playerA || aggressor == deal.playerB) ? aggressor : deal.playerA};
    }
    if (diplomacy.hasEmbargo(deal.playerA, deal.playerB)) {
        return {ContractFate::Breached, deal.playerA};
    }
    if (diplomacy.hasEmbargo(deal.playerB, deal.playerA)) {
        return {ContractFate::Breached, deal.playerB};
    }
    return {};
}

/// One turn of a supply contract: the buyer must be able to pay something,
/// the goods move, the payment is prorated to what arrived, a shortfall costs
/// the seller reputation. Returns the party in breach, or INVALID_PLAYER.
[[nodiscard]] PlayerId runSupplyContract(aoc::game::GameState& gameState,
                                         DiplomacyManager& diplomacy, DealTerm& term) {
    aoc::game::Player* seller = gameState.player(term.fromPlayer);
    aoc::game::Player* buyer  = gameState.player(term.toPlayer);
    if (seller == nullptr || buyer == nullptr) {
        return INVALID_PLAYER;
    }
    if (term.goldPerTurn > 0 && buyer->treasury() <= 0) {
        return term.toPlayer;
    }
    const int32_t moved = transferGoods(*seller, *buyer, term.goodId, term.goodAmount);
    if (moved <= 0) {
        return term.fromPlayer;
    }
    const CurrencyAmount due =
        static_cast<CurrencyAmount>(term.goldPerTurn) * moved / std::max(1, term.goodAmount);
    if (due > 0) {
        const CurrencyAmount paid = std::min(due, buyer->treasury());
        buyer->setTreasury(buyer->treasury() - paid);
        seller->setTreasury(seller->treasury() + paid);
    }
    if (moved < term.goodAmount) {
        // Stored as breakDeal stores it: the seller's standing, in relation(seller, buyer).
        diplomacy.addReputationModifier(term.fromPlayer, term.toPlayer,
                                        CONTRACT_SHORTFALL_REPUTATION, 10);
        LOG_INFO("SupplyContract short: player %u sent %d of %d of good %u to player %u",
                 static_cast<unsigned>(term.fromPlayer), moved, term.goodAmount,
                 static_cast<unsigned>(term.goodId), static_cast<unsigned>(term.toPlayer));
    }
    --term.duration;
    return INVALID_PLAYER;
}

} // namespace

void processDeals(aoc::game::GameState& gameState, GlobalDealTracker& tracker,
                  DiplomacyManager& diplomacy, const aoc::map::HexGrid& grid) {
    // First pass: check for auto-break conditions (NonAggression violated by war)
    for (int32_t i = 0; i < static_cast<int32_t>(tracker.activeDeals.size()); ++i) {
        DiplomaticDeal& deal = tracker.activeDeals[static_cast<std::size_t>(i)];
        if (!deal.isAccepted || deal.isBroken) {
            continue;
        }

        if (deal.hasTerm(DealTermType::NonAggression)) {
            const PairwiseRelation& rel = diplomacy.relation(deal.playerA, deal.playerB);
            if (rel.isAtWar) {
                // Determine aggressor from lastWarAggressor
                PlayerId aggressor = rel.lastWarAggressor;
                if (aggressor == deal.playerA || aggressor == deal.playerB) {
                    breakDeal(gameState, tracker, diplomacy, aggressor, i);
                }
            }
        }
    }

    // Second pass: enforce terms and tick durations
    std::vector<DiplomaticDeal>::iterator it = tracker.activeDeals.begin();
    while (it != tracker.activeDeals.end()) {
        if (it->isBroken) {
            // Broken deals used to stay in the list for the rest of the game.
            it = tracker.activeDeals.erase(it);
            continue;
        }
        if (!it->isAccepted) {
            ++it;
            continue;
        }
        const int32_t index = static_cast<int32_t>(it - tracker.activeDeals.begin());
        if (it->hasTerm(DealTermType::SupplyContract)) {
            const ContractCheck check = checkContractParties(gameState, diplomacy, *it);
            if (check.fate == ContractFate::Ended) {
                LOG_INFO("Contract between player %u and %u ended: a party has no city",
                         static_cast<unsigned>(it->playerA), static_cast<unsigned>(it->playerB));
                liftExclusiveAccess(gameState, diplomacy, *it);
                it = tracker.activeDeals.erase(it);
                continue;
            }
            if (check.fate == ContractFate::Breached) {
                breakDeal(gameState, tracker, diplomacy, check.breaker, index);
                it = tracker.activeDeals.erase(it);
                continue;
            }
        }
        --it->turnsRemaining;
        PlayerId breachedBy = INVALID_PLAYER;
        for (DealTerm& term : it->terms) {
            switch (term.type) {
            case DealTermType::SupplyContract: {
                if (term.duration > 0 && breachedBy == INVALID_PLAYER) {
                    breachedBy = runSupplyContract(gameState, diplomacy, term);
                }
                break;
            }
            case DealTermType::WarReparations: {
                if (term.goldPerTurn > 0) {
                    aoc::game::Player* fromPlayer = gameState.player(term.fromPlayer);
                    aoc::game::Player* toPlayer   = gameState.player(term.toPlayer);
                    if (fromPlayer != nullptr) {
                        // Pay what can actually be paid. Previously the
                        // debit was unguarded and drove treasury
                        // arbitrarily negative, corrupting downstream
                        // loan and crisis calculations.
                        const CurrencyAmount available =
                            std::max<CurrencyAmount>(0, fromPlayer->monetary().treasury);
                        const CurrencyAmount paid =
                            std::min<CurrencyAmount>(term.goldPerTurn, available);
                        fromPlayer->monetary().treasury -= paid;
                        if (toPlayer != nullptr) {
                            toPlayer->monetary().treasury += paid;
                        }
                        if (paid < term.goldPerTurn) {
                            LOG_INFO("WarReparations partial: player %u owed %lld, paid %lld",
                                     static_cast<unsigned>(term.fromPlayer),
                                     static_cast<long long>(term.goldPerTurn),
                                     static_cast<long long>(paid));
                        }
                    }
                }
                break;
            }

            case DealTermType::DemilitarizedZone: {
                // Reputation modifier is directional: addReputationModifier(x, y)
                // stores in x's view of y (DiplomacyState.cpp:362). When A
                // violates, the *victim* (B) should lose trust in A, not
                // the other way around.
                if (hasMilitaryUnitsNearBorder(gameState, grid, it->playerA, it->playerB,
                                               term.zoneRadius)) {
                    diplomacy.addReputationModifier(it->playerB, it->playerA, -5, 10);

                    aoc::game::Player* victimPlayer = gameState.player(it->playerB);
                    if (victimPlayer != nullptr) {
                        victimPlayer->grievances().addGrievance(GrievanceType::DMZViolation,
                                                                it->playerA);
                    }
                }
                if (hasMilitaryUnitsNearBorder(gameState, grid, it->playerB, it->playerA,
                                               term.zoneRadius)) {
                    diplomacy.addReputationModifier(it->playerA, it->playerB, -5, 10);

                    aoc::game::Player* victimPlayer = gameState.player(it->playerA);
                    if (victimPlayer != nullptr) {
                        victimPlayer->grievances().addGrievance(GrievanceType::DMZViolation,
                                                                it->playerB);
                    }
                }
                break;
            }

            case DealTermType::ArmsLimitation: {
                if (term.maxMilitaryUnits <= 0) {
                    break;
                }

                // Check both parties
                for (PlayerId pid : {it->playerA, it->playerB}) {
                    const aoc::game::Player* player = gameState.player(pid);
                    if (player == nullptr) {
                        continue;
                    }

                    int32_t milCount = player->militaryUnitCount();
                    int32_t excess   = milCount - term.maxMilitaryUnits;
                    if (excess > 0) {
                        PlayerId other = (pid == it->playerA) ? it->playerB : it->playerA;
                        // -3 reputation per unit over the limit
                        diplomacy.addReputationModifier(pid, other, -3 * excess, 10);

                        LOG_INFO("Arms limitation violation: Player %u has %d military "
                                 "units (limit %d, excess %d)",
                                 static_cast<unsigned>(pid), milCount, term.maxMilitaryUnits,
                                 excess);
                    }
                }
                break;
            }

            default:
                break;
            }
        }
        if (breachedBy != INVALID_PLAYER) {
            breakDeal(gameState, tracker, diplomacy, breachedBy, index);
            it = tracker.activeDeals.erase(it);
            continue;
        }
        if (it->turnsRemaining <= 0) {
            LOG_INFO("Deal between player %u and %u expired", static_cast<unsigned>(it->playerA),
                     static_cast<unsigned>(it->playerB));
            liftExclusiveAccess(gameState, diplomacy, *it);
            it = tracker.activeDeals.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace aoc::sim
