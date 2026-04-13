#pragma once

/**
 * @file GoodyHuts.hpp
 * @brief Ancient ruins / goody huts scattered across the map.
 *
 * Goody huts are one-time exploration rewards discovered by any unit.
 * When a unit enters a tile with a goody hut, the hut is consumed and
 * a random reward is granted. This incentivizes early-game exploration.
 *
 * Rewards (weighted random):
 *   - Gold:         +50-200 gold (weight 30)
 *   - Science:      +20% progress on current research (weight 20)
 *   - Culture:      +20% progress on current civic (weight 15)
 *   - Population:   +1 population in nearest city (weight 10)
 *   - Map reveal:   Reveal 5-tile radius around the hut (weight 10)
 *   - Free unit:    Spawn a free Scout or Warrior (weight 10)
 *   - Eureka:       Grant a random unearned eureka boost (weight 5)
 *
 * Placement: ~1 hut per 80 land tiles, never adjacent to starting positions.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace aoc::game { class GameState; class Player; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

enum class GoodyHutReward : uint8_t {
    Gold,
    Science,
    Culture,
    Population,
    MapReveal,
    FreeUnit,
    Eureka,

    Count
};

/// Weighted reward table.
struct GoodyHutRewardDef {
    GoodyHutReward   type;
    std::string_view name;
    int32_t          weight;  ///< Relative probability weight
};

inline constexpr int32_t GOODY_REWARD_COUNT = static_cast<int32_t>(GoodyHutReward::Count);
inline constexpr GoodyHutRewardDef GOODY_REWARD_DEFS[] = {
    {GoodyHutReward::Gold,       "Gold Cache",        30},
    {GoodyHutReward::Science,    "Ancient Scroll",    20},
    {GoodyHutReward::Culture,    "Oral Tradition",    15},
    {GoodyHutReward::Population, "Survivors",         10},
    {GoodyHutReward::MapReveal,  "Ancient Map",       10},
    {GoodyHutReward::FreeUnit,   "Tribal Warriors",   10},
    {GoodyHutReward::Eureka,     "Inspiration",        5},
};

/// Global goody hut state: tracks which tiles have huts.
struct GoodyHutState {
    std::vector<aoc::hex::AxialCoord> hutLocations;

    /// Check if a tile has a hut.
    [[nodiscard]] bool hasHut(aoc::hex::AxialCoord pos) const {
        for (const aoc::hex::AxialCoord& loc : this->hutLocations) {
            if (loc == pos) { return true; }
        }
        return false;
    }

    /// Remove a hut (consumed by exploration).
    void removeHut(aoc::hex::AxialCoord pos) {
        for (auto it = this->hutLocations.begin(); it != this->hutLocations.end(); ++it) {
            if (*it == pos) {
                this->hutLocations.erase(it);
                return;
            }
        }
    }
};

/**
 * @brief Place goody huts on the map during initialization.
 *
 * Places approximately 1 hut per 80 land tiles on valid non-water, non-mountain
 * tiles. Avoids tiles within 5 hexes of any starting position.
 */
void placeGoodyHuts(GoodyHutState& state, const aoc::map::HexGrid& grid,
                     const std::vector<aoc::hex::AxialCoord>& startPositions,
                     aoc::Random& rng);

/**
 * @brief Check if a unit has entered a goody hut tile. If so, grant reward.
 *
 * Called after any unit movement. Returns the reward type granted, or Count if
 * no hut was found at the unit's position.
 */
GoodyHutReward checkAndClaimGoodyHut(GoodyHutState& state,
                                      aoc::game::GameState& gameState,
                                      aoc::game::Player& player,
                                      aoc::hex::AxialCoord unitPosition,
                                      aoc::Random& rng);

} // namespace aoc::sim
