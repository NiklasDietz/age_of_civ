#pragma once

/**
 * @file CityScience.hpp
 * @brief Compute per-city science yield from buildings, tile yields, and population.
 *
 * Science sources per city:
 *   1. Tile yields (science from worked tiles)
 *   2. Building bonuses (flat scienceBonus from each building)
 *   3. Population base (0.5 science per citizen)
 *   4. Building multiplier (e.g., Research Lab gives 1.5x total)
 */

#include "aoc/core/Types.hpp"

namespace aoc::ecs {
class World;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::game {
class Player;
}

namespace aoc::sim {

/**
 * @brief Compute total science yield for a player across all cities.
 *
 * For each city the player owns:
 *   base = sum(worked tile science) + sum(building scienceBonus) + population * 0.5
 *   total = base * max(building scienceMultiplier)
 *
 * @return Total science points to feed into advanceResearch().
 */
[[nodiscard]] float computePlayerScience(const aoc::ecs::World& world,
                                          const aoc::map::HexGrid& grid,
                                          PlayerId player);

/**
 * @brief Compute total culture yield for a player (similar structure to science).
 */
[[nodiscard]] float computePlayerCulture(const aoc::ecs::World& world,
                                          const aoc::map::HexGrid& grid,
                                          PlayerId player);

// ========================================================================
// GameState-native overloads (Phase 3 migration)
// ========================================================================

/**
 * @brief Compute total science yield for a player (GameState version).
 *
 * Includes: tile yields, population base, building bonuses/multipliers,
 * government multiplier, civilization multiplier, monetary stability bonus.
 */
[[nodiscard]] float computePlayerScience(const aoc::game::Player& player,
                                          const aoc::map::HexGrid& grid);

/**
 * @brief Compute total culture yield for a player (GameState version).
 *
 * Includes: tile yields, population base, civilization multiplier,
 * monetary stability bonus.
 */
[[nodiscard]] float computePlayerCulture(const aoc::game::Player& player,
                                          const aoc::map::HexGrid& grid);

} // namespace aoc::sim
