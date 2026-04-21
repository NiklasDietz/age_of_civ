/**
 * @file Grievance.cpp
 * @brief Grievance / casus belli system implementation.
 */

#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void PlayerGrievanceComponent::addGrievance(GrievanceType type, PlayerId against) {
    // Dedup: same (type, against) pair already present = refresh duration on
    // the existing entry instead of appending. Prevents runaway accumulation
    // from repeat incidents (same ally dying in successive wars, repeated
    // border settlements, per-turn ideology accrual).
    for (Grievance& existing : this->grievances) {
        if (existing.type != type || existing.against != against) { continue; }
        switch (type) {
            case GrievanceType::SettledNearBorders:        existing.turnsRemaining = 30; return;
            case GrievanceType::BrokePromise:              existing.turnsRemaining = 50; return;
            case GrievanceType::ConqueredCity:             existing.turnsRemaining = 40; return;
            case GrievanceType::ViolatedEmbargo:           existing.turnsRemaining = 40; return;
            case GrievanceType::FailedAllianceObligation:  existing.turnsRemaining = 60; return;
            case GrievanceType::DMZViolation:              existing.turnsRemaining = 20; return;
            case GrievanceType::IdeologicalDifference:     existing.turnsRemaining = 8;  return;
            case GrievanceType::EspionageCaught:           existing.turnsRemaining = 40; return;
            case GrievanceType::BulliedCityState:          existing.turnsRemaining = 30; return;
            // Historically permanent (H1.10 capped at 100 turns). Re-incident
            // refreshes the countdown so repeated offenses stay fresh.
            case GrievanceType::DeclaredWarOnAlly:         existing.turnsRemaining = 100; return;
            case GrievanceType::TradeEmbargo:              existing.turnsRemaining = 100; return;
            case GrievanceType::BrokeNonAggression:        existing.turnsRemaining = 100; return;
            case GrievanceType::LostCityToSecession:       existing.turnsRemaining = 100; return;
        }
    }

    Grievance g{};
    g.type    = type;
    g.against = against;

    switch (type) {
        case GrievanceType::SettledNearBorders:
            g.severity       = -10;
            g.turnsRemaining = 30;
            break;
        case GrievanceType::BrokePromise:
            g.severity       = -20;
            g.turnsRemaining = 50;
            break;
        case GrievanceType::DeclaredWarOnAlly:
            g.severity       = -30;
            g.turnsRemaining = 100; // H1.10: capped, was previously permanent
            break;
        case GrievanceType::ConqueredCity:
            g.severity       = -15;
            g.turnsRemaining = 40;
            break;
        case GrievanceType::TradeEmbargo:
            g.severity       = -10;
            g.turnsRemaining = 100; // H1.10: capped; embargo lifecycle refreshes while active
            break;
        case GrievanceType::ViolatedEmbargo:
            g.severity       = -15;
            g.turnsRemaining = 40;
            break;
        case GrievanceType::FailedAllianceObligation:
            g.severity       = -25;
            g.turnsRemaining = 60;
            break;
        case GrievanceType::BrokeNonAggression:
            g.severity       = -30;
            g.turnsRemaining = 100; // H1.10: capped, was previously permanent
            break;
        case GrievanceType::DMZViolation:
            g.severity       = -10;
            g.turnsRemaining = 20;
            break;
        case GrievanceType::LostCityToSecession:
            g.severity       = -15;
            g.turnsRemaining = 100; // H1.10: capped, was previously permanent
            break;
        case GrievanceType::IdeologicalDifference:
            g.severity       = -5;
            g.turnsRemaining = 8;
            break;
        case GrievanceType::EspionageCaught:
            g.severity       = -20;
            g.turnsRemaining = 40;
            break;
        case GrievanceType::BulliedCityState:
            g.severity       = -10;
            g.turnsRemaining = 30;
            break;
    }

    this->grievances.push_back(g);
    LOG_INFO("Player %u added grievance (type=%u) against player %u (severity=%d)",
             static_cast<unsigned>(this->owner),
             static_cast<unsigned>(type),
             static_cast<unsigned>(against),
             g.severity);
}

void PlayerGrievanceComponent::tickGrievances() {
    for (std::vector<Grievance>::iterator it = this->grievances.begin(); it != this->grievances.end(); ) {
        if (it->turnsRemaining > 0) {
            --it->turnsRemaining;
            if (it->turnsRemaining == 0) {
                it = this->grievances.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void accrueIdeologicalGrievances(::aoc::game::GameState& gameState) {
    // auto required: lambda type is unnameable
    auto isIdeology = [](GovernmentType gov) {
        return gov == GovernmentType::Democracy
            || gov == GovernmentType::Communism
            || gov == GovernmentType::Fascism;
    };

    constexpr int32_t CAP_PER_PAIR = -50;

    const std::vector<std::unique_ptr<::aoc::game::Player>>& playersVec =
        gameState.players();
    for (std::size_t i = 0; i < playersVec.size(); ++i) {
        ::aoc::game::Player* a = playersVec[i].get();
        if (a == nullptr) { continue; }
        const GovernmentType govA = a->government().government;
        if (!isIdeology(govA)) { continue; }

        for (std::size_t j = i + 1; j < playersVec.size(); ++j) {
            ::aoc::game::Player* b = playersVec[j].get();
            if (b == nullptr) { continue; }
            const GovernmentType govB = b->government().government;
            if (!isIdeology(govB)) { continue; }
            if (govA == govB) { continue; }

            if (a->grievances().totalGrievanceAgainst(b->id()) > CAP_PER_PAIR) {
                a->grievances().addGrievance(GrievanceType::IdeologicalDifference, b->id());
            }
            if (b->grievances().totalGrievanceAgainst(a->id()) > CAP_PER_PAIR) {
                b->grievances().addGrievance(GrievanceType::IdeologicalDifference, a->id());
            }
        }
    }
}

int32_t PlayerGrievanceComponent::totalGrievanceAgainst(PlayerId target) const {
    int32_t total = 0;
    for (const Grievance& g : this->grievances) {
        if (g.against == target) {
            total += g.severity;
        }
    }
    return total;
}

} // namespace aoc::sim
