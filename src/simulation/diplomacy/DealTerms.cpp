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
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/core/Log.hpp"

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
                // Find the city by scanning the from-player's list.
                // DealTerm.cityEntity is a legacy identifier; we match it by
                // searching through all of the from-player's cities until we
                // find the one that was designated at deal creation time.
                // The name stored in the term (from CityComponent.name) is used
                // as the correlation key since no direct entity->City* map exists yet.
                aoc::game::Player* fromPlayer = gameState.player(term.fromPlayer);
                if (fromPlayer == nullptr) { break; }
                // Transfer ownership of every city belonging to fromPlayer whose
                // EntityId index matches the stored term.cityEntity.index.
                // This is a bridge: once DealTerm is updated to store a location
                // instead of an EntityId this loop can be replaced with cityAt().
                for (const std::unique_ptr<aoc::game::City>& cityPtr : fromPlayer->cities()) {
                    // Use entity index as a stable identifier until DealTerm is
                    // updated to carry an AxialCoord instead.
                    const uint32_t cityIndex = static_cast<uint32_t>(
                        cityPtr->location().q * 10000 + cityPtr->location().r);
                    if (cityIndex == term.cityEntity.index) {
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
                g.severity       = 50;
                g.turnsRemaining = 0;  // Permanent
                fromPlayer->grievances().grievances.push_back(g);
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

void breakDeal(aoc::game::GameState& /*gameState*/, GlobalDealTracker& tracker,
               PlayerId breaker, int32_t dealIndex) {
    if (dealIndex < 0 || dealIndex >= static_cast<int32_t>(tracker.activeDeals.size())) {
        return;
    }

    DiplomaticDeal& deal = tracker.activeDeals[static_cast<std::size_t>(dealIndex)];
    deal.isBroken = true;

    LOG_INFO("Player %u broke deal! Diplomatic consequences apply.",
             static_cast<unsigned>(breaker));
}

void processDeals(aoc::game::GameState& gameState, GlobalDealTracker& tracker) {
    std::vector<DiplomaticDeal>::iterator it = tracker.activeDeals.begin();
    while (it != tracker.activeDeals.end()) {
        if (!it->isAccepted || it->isBroken) {
            ++it;
            continue;
        }

        --it->turnsRemaining;

        for (const DealTerm& term : it->terms) {
            if (term.type == DealTermType::WarReparations && term.goldPerTurn > 0) {
                aoc::game::Player* fromPlayer = gameState.player(term.fromPlayer);
                aoc::game::Player* toPlayer   = gameState.player(term.toPlayer);
                if (fromPlayer != nullptr) {
                    fromPlayer->monetary().treasury -= term.goldPerTurn;
                }
                if (toPlayer != nullptr) {
                    toPlayer->monetary().treasury += term.goldPerTurn;
                }
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
