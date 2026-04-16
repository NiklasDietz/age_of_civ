/**
 * @file Grievance.cpp
 * @brief Grievance / casus belli system implementation.
 */

#include "aoc/simulation/diplomacy/Grievance.hpp"
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
