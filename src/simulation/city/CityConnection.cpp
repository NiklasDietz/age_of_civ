/**
 * @file CityConnection.cpp
 * @brief City road connection detection and gold bonus implementation.
 */

#include "aoc/simulation/city/CityConnection.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <queue>
#include <unordered_set>

namespace aoc::sim {

bool isCityConnected(const aoc::map::HexGrid& grid,
                      hex::AxialCoord cityPos,
                      hex::AxialCoord capitalPos) {
    if (cityPos == capitalPos) {
        return true;
    }

    std::queue<hex::AxialCoord> frontier;
    std::unordered_set<hex::AxialCoord> visited;

    frontier.push(cityPos);
    visited.insert(cityPos);

    while (!frontier.empty()) {
        hex::AxialCoord current = frontier.front();
        frontier.pop();

        const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(current);
        for (const hex::AxialCoord& nbr : nbrs) {
            if (!grid.isValid(nbr)) {
                continue;
            }
            if (visited.count(nbr) > 0) {
                continue;
            }

            // Allow stepping onto the capital itself even without a road
            if (nbr == capitalPos) {
                return true;
            }

            const int32_t nbrIndex = grid.toIndex(nbr);
            if (!grid.hasRoad(nbrIndex)) {
                continue;
            }

            visited.insert(nbr);
            frontier.push(nbr);
        }
    }

    return false;
}

int32_t processCityConnections(aoc::ecs::World& world,
                                const aoc::map::HexGrid& grid,
                                PlayerId player) {
    constexpr int32_t CONNECTION_BONUS = 3;

    // Find capital
    hex::AxialCoord capitalPos{0, 0};
    bool foundCapital = false;

    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return 0;
    }

    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        const CityComponent& city = cityPool->data()[i];
        if (city.owner == player && city.isOriginalCapital) {
            capitalPos = city.location;
            foundCapital = true;
            break;
        }
    }

    if (!foundCapital) {
        return 0;
    }

    // Check each non-capital city
    int32_t totalBonus = 0;
    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        const CityComponent& city = cityPool->data()[i];
        if (city.owner != player) {
            continue;
        }
        if (city.location == capitalPos) {
            continue;
        }

        if (isCityConnected(grid, city.location, capitalPos)) {
            totalBonus += CONNECTION_BONUS;
        }
    }

    if (totalBonus > 0) {
        // Add to treasury
        world.forEach<PlayerEconomyComponent>(
            [player, totalBonus](EntityId, PlayerEconomyComponent& ec) {
                if (ec.owner == player) {
                    ec.treasury += static_cast<CurrencyAmount>(totalBonus);
                    LOG_INFO("Player %u city connection bonus: +%d gold (treasury: %lld)",
                             static_cast<unsigned>(player), totalBonus,
                             static_cast<long long>(ec.treasury));
                }
            });
    }

    return totalBonus;
}

} // namespace aoc::sim
