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
            g.turnsRemaining = 0; // permanent
            break;
        case GrievanceType::ConqueredCity:
            g.severity       = -15;
            g.turnsRemaining = 40;
            break;
        case GrievanceType::TradeEmbargo:
            g.severity       = -10;
            g.turnsRemaining = 0; // lasts while active
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
            g.turnsRemaining = 0; // permanent
            break;
        case GrievanceType::DMZViolation:
            g.severity       = -10;
            g.turnsRemaining = 20;
            break;
        case GrievanceType::LostCityToSecession:
            g.severity       = -15;
            g.turnsRemaining = 0; // permanent
            break;
        case GrievanceType::IdeologicalDifference:
            g.severity       = -5;
            g.turnsRemaining = 8;
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
