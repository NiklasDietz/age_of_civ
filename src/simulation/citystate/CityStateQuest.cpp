/**
 * @file CityStateQuest.cpp
 * @brief City-state quest generation and completion checking.
 */

#include "aoc/simulation/citystate/CityStateQuest.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

void generateCityStateQuest(aoc::game::GameState& /*gameState*/,
                            std::size_t /*cityStateIndex*/,
                            PlayerId /*targetPlayer*/) {
    // Quest generation based on city-state type:
    // Militaristic -> DefeatBarbarian or TrainUnit
    // Scientific -> ResearchTech
    // Cultural -> BuildWonder
    // Trade -> SendTradeRoute
    // Religious -> ConvertToReligion
    // Industrial -> TrainUnit (builder)
    LOG_INFO("City-state quest generated");
}

void checkCityStateQuests(aoc::game::GameState& /*gameState*/) {
    // Iterate all city-states with active quests via GameState::cityStates().
    // Check if the assigned player has completed the quest objective.
    // If completed: award envoys, clear quest, generate new quest.
}

} // namespace aoc::sim
