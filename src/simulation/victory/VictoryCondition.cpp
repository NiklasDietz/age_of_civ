/**
 * @file VictoryCondition.cpp
 * @brief Victory condition evaluation and tracker updates.
 */

#include "aoc/simulation/victory/VictoryCondition.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>

namespace aoc::sim {

void updateVictoryTrackers(aoc::ecs::World& world,
                            const aoc::map::HexGrid& grid) {
    aoc::ecs::ComponentPool<VictoryTrackerComponent>* trackerPool =
        world.getPool<VictoryTrackerComponent>();
    if (trackerPool == nullptr) {
        return;
    }

    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();

    for (uint32_t i = 0; i < trackerPool->size(); ++i) {
        VictoryTrackerComponent& tracker = trackerPool->data()[i];
        const PlayerId owner = tracker.owner;

        // Count completed techs
        int32_t techsCompleted = 0;
        const aoc::ecs::ComponentPool<PlayerTechComponent>* techPool =
            world.getPool<PlayerTechComponent>();
        if (techPool != nullptr) {
            for (uint32_t t = 0; t < techPool->size(); ++t) {
                const PlayerTechComponent& tech = techPool->data()[t];
                if (tech.owner != owner) {
                    continue;
                }
                for (std::size_t bit = 0; bit < tech.completedTechs.size(); ++bit) {
                    if (tech.completedTechs[bit]) {
                        ++techsCompleted;
                    }
                }
            }
        }
        tracker.scienceProgress = techsCompleted;

        // Accumulate culture this turn
        float culture = computePlayerCulture(world, grid, owner);
        tracker.totalCultureAccumulated += culture;

        // Count cities and population
        int32_t cityCount = 0;
        int32_t totalPopulation = 0;
        if (cityPool != nullptr) {
            for (uint32_t c = 0; c < cityPool->size(); ++c) {
                const CityComponent& city = cityPool->data()[c];
                if (city.owner == owner) {
                    ++cityCount;
                    totalPopulation += city.population;
                }
            }
        }

        // Compute score: population*5 + techs*10 + cities*20 + culture
        tracker.score = totalPopulation * 5
                      + techsCompleted * 10
                      + cityCount * 20
                      + static_cast<int32_t>(tracker.totalCultureAccumulated);
    }
}

VictoryResult checkVictoryConditions(const aoc::ecs::World& world,
                                      TurnNumber currentTurn) {
    const aoc::ecs::ComponentPool<VictoryTrackerComponent>* trackerPool =
        world.getPool<VictoryTrackerComponent>();
    if (trackerPool == nullptr) {
        return {};
    }

    const uint16_t totalTechs = techCount();

    // Check each player's tracker
    for (uint32_t i = 0; i < trackerPool->size(); ++i) {
        const VictoryTrackerComponent& tracker = trackerPool->data()[i];

        // Science victory: all techs researched
        if (totalTechs > 0 && tracker.scienceProgress >= static_cast<int32_t>(totalTechs)) {
            LOG_INFO("Player %u achieved SCIENCE victory!", static_cast<unsigned>(tracker.owner));
            return {VictoryType::Science, tracker.owner};
        }

        // Culture victory: 2000+ accumulated culture
        if (tracker.totalCultureAccumulated >= 2000.0f) {
            LOG_INFO("Player %u achieved CULTURE victory!", static_cast<unsigned>(tracker.owner));
            return {VictoryType::Culture, tracker.owner};
        }
    }

    // Domination victory: one player owns all original capitals
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        // Count original capitals and check ownership
        int32_t totalOriginalCapitals = 0;
        PlayerId dominationCandidate = INVALID_PLAYER;
        bool singleOwner = true;

        for (uint32_t c = 0; c < cityPool->size(); ++c) {
            const CityComponent& city = cityPool->data()[c];
            if (!city.isOriginalCapital) {
                continue;
            }
            ++totalOriginalCapitals;
            if (dominationCandidate == INVALID_PLAYER) {
                dominationCandidate = city.owner;
            } else if (city.owner != dominationCandidate) {
                singleOwner = false;
                break;
            }
        }

        if (totalOriginalCapitals >= 2 && singleOwner &&
            dominationCandidate != INVALID_PLAYER) {
            LOG_INFO("Player %u achieved DOMINATION victory!",
                     static_cast<unsigned>(dominationCandidate));
            return {VictoryType::Domination, dominationCandidate};
        }
    }

    // Religious victory: one religion dominant in >50% of all cities
    {
        PlayerId religionWinner = INVALID_PLAYER;
        if (checkReligiousVictory(world, religionWinner)) {
            return {VictoryType::Religion, religionWinner};
        }
    }

    // Score victory: at turn 500, highest score wins
    constexpr TurnNumber SCORE_VICTORY_TURN = 500;
    if (currentTurn >= SCORE_VICTORY_TURN) {
        PlayerId bestPlayer = INVALID_PLAYER;
        int32_t bestScore = -1;

        for (uint32_t i = 0; i < trackerPool->size(); ++i) {
            const VictoryTrackerComponent& tracker = trackerPool->data()[i];
            if (tracker.score > bestScore) {
                bestScore = tracker.score;
                bestPlayer = tracker.owner;
            }
        }

        if (bestPlayer != INVALID_PLAYER) {
            LOG_INFO("Player %u achieved SCORE victory (score=%d)!",
                     static_cast<unsigned>(bestPlayer), bestScore);
            return {VictoryType::Score, bestPlayer};
        }
    }

    return {};
}

} // namespace aoc::sim
