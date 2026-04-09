/**
 * @file DealTerms.cpp
 * @brief Diplomatic deal enforcement and processing.
 */

#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

ErrorCode proposeDeal(aoc::ecs::World& /*world*/,
                      GlobalDealTracker& tracker,
                      DiplomaticDeal deal) {
    tracker.activeDeals.push_back(std::move(deal));
    LOG_INFO("Deal proposed between player %u and player %u with %zu terms",
             static_cast<unsigned>(tracker.activeDeals.back().playerA),
             static_cast<unsigned>(tracker.activeDeals.back().playerB),
             tracker.activeDeals.back().terms.size());
    return ErrorCode::Ok;
}

ErrorCode acceptDeal(aoc::ecs::World& world,
                     GlobalDealTracker& tracker,
                     int32_t dealIndex) {
    if (dealIndex < 0 || dealIndex >= static_cast<int32_t>(tracker.activeDeals.size())) {
        return ErrorCode::InvalidArgument;
    }

    DiplomaticDeal& deal = tracker.activeDeals[static_cast<std::size_t>(dealIndex)];
    deal.isAccepted = true;

    // Apply immediate terms
    for (const DealTerm& term : deal.terms) {
        switch (term.type) {
            case DealTermType::CedeCity: {
                CityComponent* city = world.tryGetComponent<CityComponent>(term.cityEntity);
                if (city != nullptr) {
                    city->owner = term.toPlayer;
                    LOG_INFO("City %s ceded to player %u", city->name.c_str(),
                             static_cast<unsigned>(term.toPlayer));
                }
                break;
            }
            case DealTermType::WarGuilt: {
                // Add grievance
                PlayerGrievanceComponent* pg = nullptr;
                aoc::ecs::ComponentPool<PlayerGrievanceComponent>* gPool =
                    world.getPool<PlayerGrievanceComponent>();
                if (gPool != nullptr) {
                    for (uint32_t i = 0; i < gPool->size(); ++i) {
                        if (gPool->data()[i].owner == term.fromPlayer) {
                            pg = &gPool->data()[i];
                            break;
                        }
                    }
                }
                if (pg != nullptr) {
                    Grievance g{};
                    g.type = GrievanceType::BrokePromise;
                    g.against = term.toPlayer;
                    g.severity = 50;
                    g.turnsRemaining = 0; // Permanent
                    pg->grievances.push_back(g);
                }
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

void breakDeal(aoc::ecs::World& /*world*/, GlobalDealTracker& tracker,
               PlayerId breaker, int32_t dealIndex) {
    if (dealIndex < 0 || dealIndex >= static_cast<int32_t>(tracker.activeDeals.size())) {
        return;
    }

    DiplomaticDeal& deal = tracker.activeDeals[static_cast<std::size_t>(dealIndex)];
    deal.isBroken = true;

    LOG_INFO("Player %u broke deal! Diplomatic consequences apply.",
             static_cast<unsigned>(breaker));
}

void processDeals(aoc::ecs::World& world, GlobalDealTracker& tracker) {
    auto it = tracker.activeDeals.begin();
    while (it != tracker.activeDeals.end()) {
        if (!it->isAccepted || it->isBroken) {
            ++it;
            continue;
        }

        --it->turnsRemaining;

        // Process per-turn terms
        for (const DealTerm& term : it->terms) {
            if (term.type == DealTermType::WarReparations && term.goldPerTurn > 0) {
                aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
                    world.getPool<MonetaryStateComponent>();
                if (monetaryPool != nullptr) {
                    for (uint32_t m = 0; m < monetaryPool->size(); ++m) {
                        if (monetaryPool->data()[m].owner == term.fromPlayer) {
                            monetaryPool->data()[m].treasury -= term.goldPerTurn;
                        }
                        if (monetaryPool->data()[m].owner == term.toPlayer) {
                            monetaryPool->data()[m].treasury += term.goldPerTurn;
                        }
                    }
                }
            }
        }

        // Remove expired deals
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
