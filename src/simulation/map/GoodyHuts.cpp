/**
 * @file GoodyHuts.cpp
 * @brief Ancient ruins placement and reward logic.
 */

#include "aoc/simulation/map/GoodyHuts.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/EurekaBoost.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void placeGoodyHuts(GoodyHutState& state, const aoc::map::HexGrid& grid,
                     const std::vector<aoc::hex::AxialCoord>& startPositions,
                     aoc::Random& rng) {
    state.hutLocations.clear();

    const int32_t totalTiles = grid.width() * grid.height();
    const int32_t targetHuts = std::max(5, totalTiles / 80);

    int32_t placed = 0;
    int32_t attempts = 0;
    constexpr int32_t MAX_ATTEMPTS = 5000;

    while (placed < targetHuts && attempts < MAX_ATTEMPTS) {
        ++attempts;
        const int32_t rx = rng.nextInt(2, grid.width() - 2);
        const int32_t ry = rng.nextInt(2, grid.height() - 2);
        const int32_t idx = ry * grid.width() + rx;

        // Skip water and impassable tiles
        if (aoc::map::isWater(grid.terrain(idx))
            || aoc::map::isImpassable(grid.terrain(idx))) {
            continue;
        }

        const aoc::hex::AxialCoord pos = aoc::hex::offsetToAxial({rx, ry});

        // Not too close to starting positions
        bool tooClose = false;
        for (const aoc::hex::AxialCoord& start : startPositions) {
            if (grid.distance(pos, start) < 5) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) { continue; }

        // Not too close to other huts
        bool nearOtherHut = false;
        for (const aoc::hex::AxialCoord& existing : state.hutLocations) {
            if (grid.distance(pos, existing) < 4) {
                nearOtherHut = true;
                break;
            }
        }
        if (nearOtherHut) { continue; }

        state.hutLocations.push_back(pos);
        ++placed;
    }

    LOG_INFO("Placed %d goody huts on the map (target: %d)", placed, targetHuts);
}

GoodyHutReward checkAndClaimGoodyHut(GoodyHutState& state,
                                      [[maybe_unused]] aoc::game::GameState& gameState,
                                      aoc::game::Player& player,
                                      aoc::hex::AxialCoord unitPosition,
                                      aoc::Random& rng) {
    if (!state.hasHut(unitPosition)) {
        return GoodyHutReward::Count;  // No hut here
    }

    // Remove the hut
    state.removeHut(unitPosition);

    // Weighted random reward selection
    int32_t totalWeight = 0;
    for (const GoodyHutRewardDef& def : GOODY_REWARD_DEFS) {
        totalWeight += def.weight;
    }

    int32_t roll = rng.nextInt(0, totalWeight - 1);
    GoodyHutReward reward = GoodyHutReward::Gold;  // Default fallback
    for (const GoodyHutRewardDef& def : GOODY_REWARD_DEFS) {
        roll -= def.weight;
        if (roll < 0) {
            reward = def.type;
            break;
        }
    }

    // Apply reward
    switch (reward) {
        case GoodyHutReward::Gold: {
            const CurrencyAmount gold = static_cast<CurrencyAmount>(50 + rng.nextInt(0, 150));
            player.addGold(gold);
            LOG_INFO("Goody hut: P%u found %lld gold!",
                     static_cast<unsigned>(player.id()),
                     static_cast<long long>(gold));
            break;
        }

        case GoodyHutReward::Science: {
            PlayerTechComponent& tech = player.tech();
            if (tech.currentResearch.isValid()) {
                const TechDef& def = techDef(tech.currentResearch);
                const float bonus = 0.20f * static_cast<float>(def.researchCost);
                tech.researchProgress += bonus;
                LOG_INFO("Goody hut: P%u found ancient scroll (+%.0f science toward %.*s)",
                         static_cast<unsigned>(player.id()),
                         static_cast<double>(bonus),
                         static_cast<int>(def.name.size()), def.name.data());
            } else {
                // No active research — give gold instead
                player.addGold(100);
            }
            break;
        }

        case GoodyHutReward::Culture: {
            // Add culture progress (use a flat boost since civic advancement is simpler)
            player.addGold(80);  // Fallback: gold as culture proxy
            LOG_INFO("Goody hut: P%u found oral traditions (+80 gold/culture)",
                     static_cast<unsigned>(player.id()));
            break;
        }

        case GoodyHutReward::Population: {
            // +1 population in nearest city
            aoc::game::City* nearestCity = nullptr;
            int32_t bestDist = std::numeric_limits<int32_t>::max();
            for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
                const int32_t dist = aoc::hex::distance(unitPosition, city->location());
                if (dist < bestDist) {
                    bestDist = dist;
                    nearestCity = city.get();
                }
            }
            if (nearestCity != nullptr) {
                nearestCity->setPopulation(nearestCity->population() + 1);
                LOG_INFO("Goody hut: P%u gained +1 pop in %s",
                         static_cast<unsigned>(player.id()),
                         nearestCity->name().c_str());
            }
            break;
        }

        case GoodyHutReward::MapReveal: {
            // Reveal is handled by fog-of-war system; for headless sim, just log
            LOG_INFO("Goody hut: P%u found ancient map (reveal 5-tile radius)",
                     static_cast<unsigned>(player.id()));
            break;
        }

        case GoodyHutReward::FreeUnit: {
            // Spawn a free Scout or Warrior
            const UnitTypeId freeUnitId = rng.chance(0.5f) ? UnitTypeId{2} : UnitTypeId{0};
            player.addUnit(freeUnitId, unitPosition);
            LOG_INFO("Goody hut: P%u found tribal warriors (free %.*s)",
                     static_cast<unsigned>(player.id()),
                     static_cast<int>(unitTypeDef(freeUnitId).name.size()),
                     unitTypeDef(freeUnitId).name.data());
            break;
        }

        case GoodyHutReward::Eureka: {
            checkEurekaConditions(player, EurekaCondition::BuildWonder);  // Trigger any pending
            LOG_INFO("Goody hut: P%u found inspiration (eureka boost)",
                     static_cast<unsigned>(player.id()));
            break;
        }

        default:
            break;
    }

    return reward;
}

} // namespace aoc::sim
