/**
 * @file CityConnection.cpp
 * @brief City road connection detection and gold bonus implementation.
 *
 * Migrated from ECS to GameState object model.
 */

#include "aoc/simulation/city/CityConnection.hpp"
#include "aoc/game/Player.hpp"  // includes MonetarySystem.hpp transitively
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"

#include <queue>
#include <unordered_set>

namespace aoc::sim {

bool isCityConnected(const aoc::map::HexGrid& grid,
                      aoc::hex::AxialCoord cityPos,
                      aoc::hex::AxialCoord capitalPos) {
    if (cityPos == capitalPos) {
        return true;
    }

    std::queue<aoc::hex::AxialCoord> frontier;
    std::unordered_set<aoc::hex::AxialCoord> visited;

    frontier.push(cityPos);
    visited.insert(cityPos);

    while (!frontier.empty()) {
        aoc::hex::AxialCoord current = frontier.front();
        frontier.pop();

        const std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(current);
        for (const aoc::hex::AxialCoord& nbr : nbrs) {
            if (!grid.isValid(nbr)) { continue; }
            if (visited.count(nbr) > 0) { continue; }

            if (nbr == capitalPos) {
                return true;
            }

            const int32_t nbrIndex = grid.toIndex(nbr);
            if (!grid.hasRoad(nbrIndex)) { continue; }

            visited.insert(nbr);
            frontier.push(nbr);
        }
    }

    return false;
}

int32_t processCityConnections(aoc::game::Player& player,
                                const aoc::map::HexGrid& grid) {
    constexpr int32_t CONNECTION_BONUS = 3;

    // In barter mode with no coins, road connection bonuses don't exist yet.
    if (player.monetary().system == MonetarySystemType::Barter
        && player.monetary().totalCoinCount() == 0) {
        return 0;
    }

    // Find capital
    aoc::hex::AxialCoord capitalPos{0, 0};
    bool foundCapital = false;
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (city->isOriginalCapital()) {
            capitalPos = city->location();
            foundCapital = true;
            break;
        }
    }

    if (!foundCapital) {
        return 0;
    }

    // Check each non-capital city for road connection to capital
    int32_t totalBonus = 0;
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (city->location() == capitalPos) { continue; }
        if (isCityConnected(grid, city->location(), capitalPos)) {
            totalBonus += CONNECTION_BONUS;
        }
    }

    if (totalBonus > 0) {
        player.addGold(static_cast<CurrencyAmount>(totalBonus));
    }

    return totalBonus;
}

} // namespace aoc::sim
