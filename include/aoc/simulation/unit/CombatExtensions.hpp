#pragma once

/**
 * @file CombatExtensions.hpp
 * @brief Corps/Armies, nuclear weapons, and air combat mechanics.
 *
 * === Corps/Armies ===
 * Two units of the same type on adjacent tiles can merge into a Corps (+10 strength).
 * Three units merge into an Army (+17 strength). Requires Nationalism civic.
 * Naval equivalent: Fleet (2 ships) and Armada (3 ships).
 *
 * === Nuclear Weapons ===
 * Two types:
 *   - Nuclear Device: 1-tile blast radius, requires Manhattan Project + Uranium
 *   - Thermonuclear Device: 2-tile blast radius, requires Nuclear Fusion tech
 * Effects on detonation:
 *   - All units in blast radius destroyed
 *   - Cities in blast lose 50% population (device) or 75% (thermonuclear)
 *   - Tiles become Fallout feature (no yields for 10 turns)
 *   - +50 grievance with ALL civilizations
 *   - Triggers global climate warming (+0.5 degrees per nuke)
 *
 * === Air Combat ===
 * Air units operate from cities with Airport or Aircraft Carrier:
 *   - Fighter: air superiority, intercepts enemy aircraft, patrol range
 *   - Bomber: strategic bombing of cities/improvements, escorts needed
 *   - Anti-air units: ground units that shoot down aircraft
 * Air units have a sortie limit (missions per turn) and must return to base.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>

namespace aoc::ecs { class World; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

// ============================================================================
// Corps / Armies
// ============================================================================

/// Formation level of a unit.
enum class FormationLevel : uint8_t {
    Single = 0,  ///< Normal unit
    Corps  = 1,  ///< 2 units combined
    Army   = 2,  ///< 3 units combined
    Fleet  = 1,  ///< Naval corps equivalent
    Armada = 2,  ///< Naval army equivalent
};

/// Combat strength bonus per formation level.
[[nodiscard]] constexpr int32_t formationStrengthBonus(FormationLevel level) {
    switch (level) {
        case FormationLevel::Single: return 0;
        case FormationLevel::Corps:  return 10;  // Also Fleet
        case FormationLevel::Army:   return 17;  // Also Armada
        default:                     return 0;
    }
}

/// ECS component for unit formation state.
struct UnitFormationComponent {
    FormationLevel level = FormationLevel::Single;
    int32_t unitsInFormation = 1;  ///< 1, 2, or 3
};

/**
 * @brief Merge two units into a Corps (or Fleet for naval).
 *
 * Both units must be the same type, adjacent, and owned by the same player.
 * The source unit is destroyed; the target gains Corps formation.
 *
 * @return Ok if successful.
 */
[[nodiscard]] ErrorCode formCorps(aoc::ecs::World& world,
                                   EntityId targetUnit, EntityId sourceUnit);

/**
 * @brief Add a third unit to a Corps to form an Army (or Armada).
 *
 * @return Ok if successful.
 */
[[nodiscard]] ErrorCode formArmy(aoc::ecs::World& world,
                                  EntityId corpsUnit, EntityId sourceUnit);

// ============================================================================
// Nuclear Weapons
// ============================================================================

enum class NukeType : uint8_t {
    NuclearDevice       = 0,  ///< 1-tile blast, -50% city pop
    ThermonuclearDevice = 1,  ///< 2-tile blast, -75% city pop
};

/// ECS component for a nuclear weapon (attached to the unit carrying it).
struct NuclearWeaponComponent {
    NukeType type = NukeType::NuclearDevice;
};

/**
 * @brief Launch a nuclear strike at a target tile.
 *
 * Effects:
 * - All units in blast radius destroyed (friend and foe)
 * - Cities lose population
 * - Tiles get Fallout feature
 * - Grievance with all civs
 * - Global warming contribution
 *
 * @param world       ECS world.
 * @param grid        Hex grid (tiles modified with Fallout).
 * @param launcherEntity  Unit/city launching the nuke.
 * @param targetTile  Target tile coordinate.
 * @param type        Nuclear or thermonuclear.
 * @return Ok if successful.
 */
[[nodiscard]] ErrorCode launchNuclearStrike(aoc::ecs::World& world,
                                            aoc::map::HexGrid& grid,
                                            EntityId launcherEntity,
                                            hex::AxialCoord targetTile,
                                            NukeType type);

// ============================================================================
// Air Combat
// ============================================================================

/// ECS component for air units (Fighters, Bombers).
struct AirUnitComponent {
    EntityId baseCity = NULL_ENTITY;   ///< City or carrier this unit operates from
    int32_t  sortiesRemaining = 1;     ///< Missions this turn (reset each turn)
    int32_t  maxSorties = 1;           ///< Max missions per turn
    int32_t  operationalRange = 8;     ///< Max hex range from base
    bool     isIntercepting = false;   ///< Fighter set to intercept mode
};

/**
 * @brief Execute a bombing run on a target tile.
 *
 * Damages units and improvements on the target tile.
 * Bomber must be within operational range of its base.
 *
 * @return Ok if successful.
 */
[[nodiscard]] ErrorCode executeBombingRun(aoc::ecs::World& world,
                                          aoc::map::HexGrid& grid,
                                          EntityId bomberEntity,
                                          hex::AxialCoord targetTile);

/**
 * @brief Attempt interception of an enemy air unit.
 *
 * Fighters on intercept mode in range automatically engage enemy
 * aircraft that enter their patrol zone.
 *
 * @return true if interception occurred.
 */
bool attemptInterception(aoc::ecs::World& world,
                         EntityId interceptorEntity,
                         EntityId targetAirUnit);

/**
 * @brief Reset air unit sorties at the start of a turn.
 */
void resetAirSorties(aoc::ecs::World& world, PlayerId player);

} // namespace aoc::sim
