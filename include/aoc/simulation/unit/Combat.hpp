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

namespace aoc::ecs {
class World;
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
 * Both units must be alive. The attacker must be adjacent to the defender.
 * Both take damage; one or both may die.
 *
 * @param world   ECS world (will modify unit components).
 * @param rng     Deterministic PRNG for combat variance.
 * @param grid    Hex grid for terrain modifiers.
 * @param attacker Entity ID of the attacking unit.
 * @param defender Entity ID of the defending unit.
 * @return Combat outcome.
 */
CombatResult resolveMeleeCombat(aoc::ecs::World& world,
                                 aoc::Random& rng,
                                 const aoc::map::HexGrid& grid,
                                 EntityId attacker,
                                 EntityId defender);

/**
 * @brief Resolve ranged attack.
 *
 * Ranged units deal damage without taking melee retaliation, but deal
 * less damage than melee. Defender must be within range.
 */
CombatResult resolveRangedCombat(aoc::ecs::World& world,
                                  aoc::Random& rng,
                                  const aoc::map::HexGrid& grid,
                                  EntityId attacker,
                                  EntityId defender);

/**
 * @brief Count friendly units adjacent to a position (for flanking bonus).
 */
[[nodiscard]] int32_t countAdjacentFriendlies(const aoc::ecs::World& world,
                                               hex::AxialCoord position,
                                               PlayerId friendlyPlayer);

/**
 * @brief Terrain defense modifier for a tile. Mountains/hills give bonus.
 */
[[nodiscard]] float terrainDefenseModifier(const aoc::map::HexGrid& grid,
                                            hex::AxialCoord position);

} // namespace aoc::sim
