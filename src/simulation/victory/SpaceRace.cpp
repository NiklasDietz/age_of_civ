/**
 * @file SpaceRace.cpp
 * @brief Space Race progress processor for Science Victory.
 *
 * Each turn, for every non-eliminated player with at least one Campus
 * district, accumulates production toward the next uncompleted project
 * if the project's required tech is researched. Progress is funded by
 * the player's current science output (scaled down) to avoid duplicating
 * tech-tree spend: think of it as a parallel R&D track gated by both
 * science capacity and prerequisite tech.
 */

#include "aoc/simulation/victory/SpaceRace.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/core/Log.hpp"

#include <memory>

namespace aoc::sim {

namespace {

constexpr float SCIENCE_TO_PROGRESS = 0.2f;

[[nodiscard]] bool playerHasCampus(const aoc::game::Player& player) {
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (city->districts().hasDistrict(DistrictType::Campus)) {
            return true;
        }
    }
    return false;
}

} // namespace

void processSpaceRace(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        aoc::game::Player& player = *playerPtr;
        if (player.victoryTracker().isEliminated) { continue; }

        PlayerSpaceRaceComponent& race = player.spaceRace();
        if (race.allCompleted()) { continue; }

        if (!playerHasCampus(player)) { continue; }

        const SpaceProjectId next = race.nextProject();
        if (next == SpaceProjectId::Count) { continue; }

        const int32_t idx = static_cast<int32_t>(next);
        const SpaceProjectDef& def = SPACE_PROJECT_DEFS[static_cast<std::size_t>(idx)];

        if (!player.hasResearched(def.requiredTech)) { continue; }

        const float science = computePlayerScience(player, grid);
        race.progress[idx] += science * SCIENCE_TO_PROGRESS;

        if (race.progress[idx] >= def.productionCost) {
            race.completed[idx] = true;
            race.progress[idx] = def.productionCost;
            LOG_INFO("Player %u [SpaceRace.cpp:processSpaceRace] completed '%.*s' (%d/%d projects)",
                     static_cast<unsigned>(player.id()),
                     static_cast<int>(def.name.size()), def.name.data(),
                     race.completedCount(), SPACE_PROJECT_COUNT);
        }
    }
}

} // namespace aoc::sim
