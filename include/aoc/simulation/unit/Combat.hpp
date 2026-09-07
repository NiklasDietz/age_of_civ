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
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>

namespace aoc::game {
class GameState;
class Unit;
} // namespace aoc::game

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

struct CombatResult {
    int32_t attackerDamage; ///< HP lost by attacker
    int32_t defenderDamage; ///< HP lost by defender
    bool attackerKilled;
    bool defenderKilled;
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
CombatResult resolveMeleeCombat(aoc::game::GameState& gameState, aoc::Random& rng,
                                const aoc::map::HexGrid& grid, aoc::game::Unit& attacker,
                                aoc::game::Unit& defender);

/**
 * @brief Resolve ranged attack.
 *
 * Ranged units deal damage without taking melee retaliation. The defender
 * must be within range. If the defender dies it is removed from its owning player.
 */
CombatResult resolveRangedCombat(aoc::game::GameState& gameState, aoc::Random& rng,
                                 const aoc::map::HexGrid& grid, aoc::game::Unit& attacker,
                                 aoc::game::Unit& defender);

/**
 * @brief Count friendly units adjacent to a position (for flanking bonus).
 */
/// Military units of `friendlyPlayer` adjacent to `position`, excluding
/// `exclude` (the attacker itself). Until 2026-09-05 the attacker and civilians
/// counted, so every melee attack got a free +10 percent flank.
[[nodiscard]] int32_t countAdjacentFriendlies(const aoc::game::GameState& gameState,
                                              aoc::hex::AxialCoord position,
                                              PlayerId friendlyPlayer,
                                              const aoc::game::Unit* exclude = nullptr);

/**
 * @brief Terrain defense modifier for a tile. Hills/forest/jungle give bonus.
 */
[[nodiscard]] float terrainDefenseModifier(const aoc::map::HexGrid& grid,
                                           aoc::hex::AxialCoord position);

/**
 * @brief Class-based combat bonus multiplier (rock-paper-scissors matchups).
 *
 * Returns a multiplier > 1.0 if the attacker's class is strong against the
 * defender's class, < 1.0 if weak, and 1.0 for neutral matchups.
 *
 * Matchup table (Civ-inspired):
 *   AntiCavalry vs Cavalry/Armor:  +50% (spearmen beat horses)
 *   Cavalry vs Ranged/Artillery:   +33% (fast flankers overwhelm ranged)
 *   Ranged vs Melee:               +25% (kiting advantage)
 *   Armor vs Melee/AntiCavalry:    +25% (mechanised advantage)
 *   Artillery vs Armor:            +33% (indirect fire vs slow targets)
 *   Air vs all ground (non-AA):    +25% (air superiority)
 *   AntiCavalry vs Air/Helicopter: +50% (AA role for modern anti-cav)
 */
[[nodiscard]] float classMatchupModifier(UnitClass attackerClass, UnitClass defenderClass);

/// Preview result for UI tooltip (no state changes applied).
struct CombatPreview {
    int32_t expectedAttackerDamage;
    int32_t expectedDefenderDamage;
};

/// Effective strengths after every modifier (starvation, formation, promotions,
/// unique units, civ and government bonuses, embarkation, health, supply,
/// terrain, river, elevation, flanking, class matchup, fortification, war
/// weariness). resolveMeleeCombat, resolveRangedCombat and previewCombat all
/// use it, so the preview cannot drift from the resolution again (2026-09-05).
/// Strength edge per era a unit is ahead of the one it fights, capped.
///
/// MECHANICS.md has described an era-difference modifier since it was written,
/// and `UnitEra` was read nowhere in Combat.cpp or CombatExtensions.cpp: era
/// affected only maintenance and food. A Warrior and a Mech Infantry met on the
/// strength table alone.
///
/// Capped because the table already rises steeply with era; without a cap the
/// two effects compound into an instant kill across a wide era gap.
inline constexpr float ERA_ADVANTAGE_PER_STEP = 0.06f;
inline constexpr float ERA_ADVANTAGE_MAX      = 0.30f;

/// Multiplier for a unit of `own` era fighting one of `other` era.
/// 1.0 when equal or behind; the loser gets no penalty, the leader gets the
/// edge, so the two sides' modifiers never multiply against each other twice.
[[nodiscard]] float eraAdvantageModifier(UnitEra own, UnitEra other);

struct CombatStrengths {
    float attack;
    float defense;
};
[[nodiscard]] CombatStrengths computeCombatStrengths(const aoc::game::GameState& gameState,
                                                     const aoc::map::HexGrid& grid,
                                                     const aoc::game::Unit& attacker,
                                                     const aoc::game::Unit& defender, bool ranged);

/// The damage curve every fight uses: 30 * (attack / defense) * a roll between
/// 0.8 and 1.2, both operands floored at 0.01 and the result clamped to 0..100.
/// A city siege reads it too, so the two never drift apart.
[[nodiscard]] int32_t computeCombatDamage(float attackStrength, float defenseStrength,
                                          aoc::Random& rng);

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
CombatResult resolveMeleeCombat(aoc::game::GameState& gameState, aoc::Random& rng,
                                const aoc::map::HexGrid& grid, EntityId attackerEntity,
                                EntityId defenderEntity);
CombatResult resolveRangedCombat(aoc::game::GameState& gameState, aoc::Random& rng,
                                 const aoc::map::HexGrid& grid, EntityId attackerEntity,
                                 EntityId defenderEntity);
[[nodiscard]] CombatPreview previewCombat(const aoc::game::GameState& gameState,
                                          const aoc::map::HexGrid& grid, EntityId attackerEntity,
                                          EntityId defenderEntity);

} // namespace aoc::sim
