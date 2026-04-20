/**
 * @file DiplomaticFavor.cpp
 * @brief Per-turn diplomatic favor computation.
 */

#include "aoc/simulation/diplomacy/DiplomaticFavor.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/government/Government.hpp"

#include <algorithm>

namespace aoc::sim {

namespace {

int32_t governmentBaseFavor(GovernmentType gov) {
    switch (gov) {
        case GovernmentType::Democracy:        return 4;
        case GovernmentType::MerchantRepublic: return 3;
        case GovernmentType::Monarchy:         return 2;
        case GovernmentType::Theocracy:        return 2;
        default:                               return 1;
    }
}

} // namespace

int32_t computeDiplomaticFavor(const aoc::game::Player& player,
                                int32_t allianceCount,
                                int32_t suzeraintyCount,
                                int32_t totalGrievancesAgainst) {
    const int32_t base         = governmentBaseFavor(player.government().government);
    const int32_t allianceBonus = 2 * std::max(0, allianceCount);
    const int32_t suzeBonus     = 2 * std::max(0, suzeraintyCount);
    const int32_t grievancePenalty = -std::clamp(totalGrievancesAgainst, 0, 10);
    return base + allianceBonus + suzeBonus + grievancePenalty;
}

} // namespace aoc::sim
