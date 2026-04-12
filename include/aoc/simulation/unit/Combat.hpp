#pragma once

/**
 * @file Combat.hpp
 * @brief Combat resolution between units using a modified Lanchester model.
 *
 * Combat is deterministic given the same inputs (uses the game PRNG).
 * Both attacker and defender take damage. The amount depends on the strength
 * ratio, terrain modifier, flanking, and promotions.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>

namespace aoc::game {
class GameState;
class Unit;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

struct CombatResult {
    int32_t attackerDamage;    ///< HP lost by attacker
    int32_t defenderDamage;    ///< HP lost by defender
    bool    attackerKilled;
    bool    defenderKilled;
    int32_t attackerXpGained;
    int32_t defenderXpGained;
};

/**
 * @brief Resolve melee combat between two units.
 *
 * Both units must be alive and the attacker must be adjacent to the defender.
 * Both take damage; one or both may die. Dead units are removed from their
 * owning player via GameState.
 *
 * @param gameState Game state (used to remove killed units and access player data).
 * @param rng       Deterministic PRNG for combat variance.
 * @param grid      Hex grid for terrain modifiers.
 * @param attacker  The attacking unit.
 * @param defender  The defending unit.
 * @return Combat outcome.
 */
CombatResult resolveMeleeCombat(aoc::game::GameState& gameState,
                                 aoc::Random& rng,
                                 const aoc::map::HexGrid& grid,
                                 aoc::game::Unit& attacker,
                                 aoc::game::Unit& defender);

/**
 * @brief Resolve ranged attack.
 *
 * Ranged units deal damage without taking melee retaliation. The defender
 * must be within range. If the defender dies it is removed from its owning player.
 */
CombatResult resolveRangedCombat(aoc::game::GameState& gameState,
                                  aoc::Random& rng,
                                  const aoc::map::HexGrid& grid,
                                  aoc::game::Unit& attacker,
                                  aoc::game::Unit& defender);

/**
 * @brief Count friendly units adjacent to a position (for flanking bonus).
 */
[[nodiscard]] int32_t countAdjacentFriendlies(const aoc::game::GameState& gameState,
                                               aoc::hex::AxialCoord position,
                                               PlayerId friendlyPlayer);

/**
 * @brief Terrain defense modifier for a tile. Hills/forest/jungle give bonus.
 */
[[nodiscard]] float terrainDefenseModifier(const aoc::map::HexGrid& grid,
                                            aoc::hex::AxialCoord position);

/// Preview result for UI tooltip (no state changes applied).
struct CombatPreview {
    int32_t expectedAttackerDamage;
    int32_t expectedDefenderDamage;
};

/**
 * @brief Preview expected combat damage without modifying any state.
 *
 * Uses the same formula as resolveMeleeCombat but with a fixed random factor
 * of 1.0 (average outcome). Useful for the combat preview tooltip.
 */
[[nodiscard]] CombatPreview previewCombat(const aoc::game::GameState& gameState,
                                           const aoc::map::HexGrid& grid,
                                           const aoc::game::Unit& attacker,
                                           const aoc::game::Unit& defender);

// Legacy EntityId overloads for callers not yet migrated to Unit&
CombatResult resolveMeleeCombat(aoc::game::GameState& gameState,
                                 aoc::Random& rng,
                                 const aoc::map::HexGrid& grid,
                                 EntityId attackerEntity,
                                 EntityId defenderEntity);
CombatResult resolveRangedCombat(aoc::game::GameState& gameState,
                                  aoc::Random& rng,
                                  const aoc::map::HexGrid& grid,
                                  EntityId attackerEntity,
                                  EntityId defenderEntity);
[[nodiscard]] CombatPreview previewCombat(const aoc::game::GameState& gameState,
                                           const aoc::map::HexGrid& grid,
                                           EntityId attackerEntity,
                                           EntityId defenderEntity);

} // namespace aoc::sim
