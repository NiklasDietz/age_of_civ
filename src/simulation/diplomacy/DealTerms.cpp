/**
 * @file DealTerms.cpp
 * @brief Diplomatic deal enforcement and processing.
 *
 * The CedeCity term still carries an EntityId from the legacy struct definition.
 * City lookup for that term searches all players' city lists rather than going
 * through the ECS world. All other ECS access has been removed.
 */

#include "aoc/simulation/diplomacy/DealTerms.hpp"

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

namespace aoc::sim {

ErrorCode proposeDeal(aoc::game::GameState& /*gameState*/,
                      GlobalDealTracker& tracker,
                      DiplomaticDeal deal) {
    tracker.activeDeals.push_back(std::move(deal));
    LOG_INFO("Deal proposed between player %u and player %u with %zu terms",
             static_cast<unsigned>(tracker.activeDeals.back().playerA),
             static_cast<unsigned>(tracker.activeDeals.back().playerB),
             tracker.activeDeals.back().terms.size());
    return ErrorCode::Ok;
}

ErrorCode acceptDeal(aoc::game::GameState& gameState,
                     aoc::map::HexGrid& grid,
                     GlobalDealTracker& tracker,
                     int32_t dealIndex) {
    if (dealIndex < 0 || dealIndex >= static_cast<int32_t>(tracker.activeDeals.size())) {
        return ErrorCode::InvalidArgument;
    }

    DiplomaticDeal& deal = tracker.activeDeals[static_cast<std::size_t>(dealIndex)];
    deal.isAccepted = true;

    for (const DealTerm& term : deal.terms) {
        switch (term.type) {
            case DealTermType::CedeCity: {
                // Match by axial location. Prior code stored
                // q*10000 + r in EntityId.index and decoded via the same
                // formula, which collides for coordinates outside the
                // default map bounds. DealTerm.tileCoord is authoritative;
                // cityEntity is retained only for backwards compatibility
                // with existing UI/AI call sites.
                aoc::game::Player* fromPlayer = gameState.player(term.fromPlayer);
                if (fromPlayer == nullptr) { break; }
                for (const std::unique_ptr<aoc::game::City>& cityPtr : fromPlayer->cities()) {
                    if (cityPtr->location() == term.tileCoord) {
                        LOG_INFO("City %s ceded to player %u",
                                 cityPtr->name().c_str(),
                                 static_cast<unsigned>(term.toPlayer));
                        cityPtr->setOwner(term.toPlayer);
                        break;
                    }
                }
                break;
            }
            case DealTermType::WarGuilt: {
                aoc::game::Player* fromPlayer = gameState.player(term.fromPlayer);
                if (fromPlayer == nullptr) { break; }
                Grievance g{};
                g.type           = GrievanceType::BrokePromise;
                g.against        = term.toPlayer;
                // Severity convention (Grievance.cpp:49-97): all grievances
                // are stored as NEGATIVE values and summed into a negative
                // opinion modifier. Prior +50 actually improved the
                // fromPlayer's opinion of toPlayer — opposite of intent.
                g.severity       = -50;
                g.turnsRemaining = 0;  // Permanent (tickGrievances only
                                       // decrements when > 0)
                fromPlayer->grievances().grievances.push_back(g);
                break;
            }
            case DealTermType::CedeTile: {
                // Transfer ownership of a single border-adjacent hex. Validates
                // that the tile currently belongs to fromPlayer AND that at
                // least one neighbour hex belongs to toPlayer (no teleporting
                // tiles across the map).
                const int32_t idx = grid.toIndex(term.tileCoord);
                if (idx < 0) { break; }
                if (grid.owner(idx) != term.fromPlayer) {
                    LOG_INFO("CedeTile aborted: tile (%d,%d) not owned by player %u",
                             term.tileCoord.q, term.tileCoord.r,
                             static_cast<unsigned>(term.fromPlayer));
                    break;
                }
                bool touchesReceiver = false;
                for (const hex::AxialCoord& n : hex::neighbors(term.tileCoord)) {
                    const int32_t nIdx = grid.toIndex(n);
                    if (nIdx < 0) { continue; }
                    if (grid.owner(nIdx) == term.toPlayer) {
                        touchesReceiver = true;
                        break;
                    }
                }
                if (!touchesReceiver) {
                    LOG_INFO("CedeTile aborted: tile (%d,%d) does not border player %u",
                             term.tileCoord.q, term.tileCoord.r,
                             static_cast<unsigned>(term.toPlayer));
                    break;
                }
                grid.setOwner(idx, term.toPlayer);
                LOG_INFO("Tile (%d,%d) ceded from player %u to player %u",
                         term.tileCoord.q, term.tileCoord.r,
                         static_cast<unsigned>(term.fromPlayer),
                         static_cast<unsigned>(term.toPlayer));
                break;
            }
            case DealTermType::GoldLump: {
                aoc::game::Player* payer = gameState.player(term.fromPlayer);
                aoc::game::Player* receiver = gameState.player(term.toPlayer);
                if (payer == nullptr || receiver == nullptr) { break; }
                const CurrencyAmount amt = static_cast<CurrencyAmount>(term.goldLump);
                if (amt <= 0) { break; }
                if (payer->treasury() < amt) {
                    LOG_INFO("GoldLump aborted: player %u has insufficient gold (%lld < %lld)",
                             static_cast<unsigned>(term.fromPlayer),
                             static_cast<long long>(payer->treasury()),
                             static_cast<long long>(amt));
                    break;
                }
                payer->setTreasury(payer->treasury() - amt);
                receiver->setTreasury(receiver->treasury() + amt);
                LOG_INFO("GoldLump: player %u paid %lld to player %u",
                         static_cast<unsigned>(term.fromPlayer),
                         static_cast<long long>(amt),
                         static_cast<unsigned>(term.toPlayer));
                break;
            }
            default:
                break;
        }
    }

    LOG_INFO("Deal accepted between player %u and player %u",
             static_cast<unsigned>(deal.playerA), static_cast<unsigned>(deal.playerB));
    return ErrorCode::Ok;
}

void breakDeal(aoc::game::GameState& gameState, GlobalDealTracker& tracker,
               DiplomacyManager& diplomacy, PlayerId breaker, int32_t dealIndex) {
    if (dealIndex < 0 || dealIndex >= static_cast<int32_t>(tracker.activeDeals.size())) {
        return;
    }

    DiplomaticDeal& deal = tracker.activeDeals[static_cast<std::size_t>(dealIndex)];
    deal.isBroken = true;

    PlayerId victim = (deal.playerA == breaker) ? deal.playerB : deal.playerA;

    // NonAggression broken: severe penalties
    if (deal.hasTerm(DealTermType::NonAggression)) {
        // -30 reputation with victim
        diplomacy.addReputationModifier(breaker, victim, -30, 0);  // permanent

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
                                const aoc::map::HexGrid& grid,
                                PlayerId violator, PlayerId borderOwner,
                                int32_t radius) {
    const aoc::game::Player* player = gameState.player(violator);
    if (player == nullptr) { return false; }

    for (const std::unique_ptr<aoc::game::Unit>& unit : player->units()) {
        UnitClass uc = unit->typeDef().unitClass;
        if (uc == UnitClass::Settler || uc == UnitClass::Civilian
            || uc == UnitClass::Trader || uc == UnitClass::Scout
            || uc == UnitClass::Religious) {
            continue;
        }

        aoc::hex::AxialCoord pos = unit->position();
        if (!grid.isValid(pos)) { continue; }

        // Check if this unit is within `radius` of any tile owned by borderOwner
        // by scanning hex rings 0..radius around the unit position.
        for (int32_t r = 0; r <= radius; ++r) {
            for (int32_t q = -r; q <= r; ++q) {
                int32_t rMin = std::max(-r, -q - r);
                int32_t rMax = std::min(r, -q + r);
                for (int32_t s = rMin; s <= rMax; ++s) {
                    aoc::hex::AxialCoord neighbor{pos.q + q, pos.r + s};
                    if (!grid.isValid(neighbor)) { continue; }
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

void processDeals(aoc::game::GameState& gameState, GlobalDealTracker& tracker,
                  DiplomacyManager& diplomacy, const aoc::map::HexGrid& grid) {
    // First pass: check for auto-break conditions (NonAggression violated by war)
    for (int32_t i = 0; i < static_cast<int32_t>(tracker.activeDeals.size()); ++i) {
        DiplomaticDeal& deal = tracker.activeDeals[static_cast<std::size_t>(i)];
        if (!deal.isAccepted || deal.isBroken) { continue; }

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
        if (!it->isAccepted || it->isBroken) {
            ++it;
            continue;
        }

        --it->turnsRemaining;

        for (const DealTerm& term : it->terms) {
            switch (term.type) {
                case DealTermType::WarReparations: {
                    if (term.goldPerTurn > 0) {
                        aoc::game::Player* fromPlayer = gameState.player(term.fromPlayer);
                        aoc::game::Player* toPlayer   = gameState.player(term.toPlayer);
                        if (fromPlayer != nullptr) {
                            // Pay what can actually be paid. Previously the
                            // debit was unguarded and drove treasury
                            // arbitrarily negative, corrupting downstream
                            // loan and crisis calculations.
                            const CurrencyAmount available = std::max<CurrencyAmount>(
                                0, fromPlayer->monetary().treasury);
                            const CurrencyAmount paid = std::min<CurrencyAmount>(
                                term.goldPerTurn, available);
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
                    if (hasMilitaryUnitsNearBorder(gameState, grid,
                                                   it->playerA, it->playerB,
                                                   term.zoneRadius)) {
                        diplomacy.addReputationModifier(it->playerB, it->playerA, -5, 10);

                        aoc::game::Player* victimPlayer = gameState.player(it->playerB);
                        if (victimPlayer != nullptr) {
                            victimPlayer->grievances().addGrievance(
                                GrievanceType::DMZViolation, it->playerA);
                        }
                    }
                    if (hasMilitaryUnitsNearBorder(gameState, grid,
                                                   it->playerB, it->playerA,
                                                   term.zoneRadius)) {
                        diplomacy.addReputationModifier(it->playerA, it->playerB, -5, 10);

                        aoc::game::Player* victimPlayer = gameState.player(it->playerA);
                        if (victimPlayer != nullptr) {
                            victimPlayer->grievances().addGrievance(
                                GrievanceType::DMZViolation, it->playerB);
                        }
                    }
                    break;
                }

                case DealTermType::ArmsLimitation: {
                    if (term.maxMilitaryUnits <= 0) { break; }

                    // Check both parties
                    for (PlayerId pid : {it->playerA, it->playerB}) {
                        const aoc::game::Player* player = gameState.player(pid);
                        if (player == nullptr) { continue; }

                        int32_t milCount = player->militaryUnitCount();
                        int32_t excess = milCount - term.maxMilitaryUnits;
                        if (excess > 0) {
                            PlayerId other = (pid == it->playerA)
                                ? it->playerB : it->playerA;
                            // -3 reputation per unit over the limit
                            diplomacy.addReputationModifier(pid, other,
                                                            -3 * excess, 10);

                            LOG_INFO("Arms limitation violation: Player %u has %d military "
                                     "units (limit %d, excess %d)",
                                     static_cast<unsigned>(pid), milCount,
                                     term.maxMilitaryUnits, excess);
                        }
                    }
                    break;
                }

                default:
                    break;
            }
        }

        if (it->turnsRemaining <= 0) {
            LOG_INFO("Deal between player %u and %u expired",
                     static_cast<unsigned>(it->playerA),
                     static_cast<unsigned>(it->playerB));
            it = tracker.activeDeals.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace aoc::sim
