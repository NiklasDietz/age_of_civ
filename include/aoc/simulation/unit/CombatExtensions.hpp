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
#include "aoc/simulation/unit/UnitTypes.hpp"

#include <cstdint>
#include <string_view>

namespace aoc::game { class GameState; class Unit; }
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

/// Combat strength multiplier per formation level. Sub-linear to prevent a
/// 3-unit Army from cascading over a lone defender:
///   Corps/Fleet: +15%    (previously +10 flat)
///   Army/Armada: +25%    (previously +17 flat, additive with base)
/// Additional flat stacking on top is capped externally at +2.
[[nodiscard]] constexpr float formationStrengthMultiplier(FormationLevel level) {
    switch (level) {
        case FormationLevel::Single: return 1.00f;
        case FormationLevel::Corps:  return 1.15f;  // Also Fleet
        case FormationLevel::Army:   return 1.25f;  // Also Armada
        default:                     return 1.00f;
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
[[nodiscard]] ErrorCode formCorps(aoc::game::GameState& gameState,
                                   aoc::game::Unit& targetUnit,
                                   aoc::game::Unit& sourceUnit);

/**
 * @brief Add a third unit to a Corps to form an Army (or Armada).
 *
 * @return Ok if successful.
 */
[[nodiscard]] ErrorCode formArmy(aoc::game::GameState& gameState,
                                  aoc::game::Unit& corpsUnit,
                                  aoc::game::Unit& sourceUnit);

/// Civics that unlock formations: Corps / Fleet at Nationalism, Army / Armada at
/// Mobilization. formCorps / formArmy are gate-free primitives; requestMergeUnits
/// applies these gates for every caller (right-click, debug route, MCP, AI).
inline constexpr CivicId FORMATION_CORPS_CIVIC{11};
inline constexpr CivicId FORMATION_ARMY_CIVIC{37};

/// Empty for a single unit, else Corps / Army, or Fleet / Armada for naval classes.
[[nodiscard]] constexpr std::string_view formationLabel(UnitClass unitClass,
                                                        FormationLevel level) {
    const bool naval = isNaval(unitClass);
    switch (level) {
        case FormationLevel::Corps: return naval ? "Fleet" : "Corps";
        case FormationLevel::Army:  return naval ? "Armada" : "Army";
        default:                    return "";
    }
}

/// Merge the unit `player` owns at `sourceAt` into the one it owns at `at`: a
/// Single target becomes a Corps / Fleet (needs Nationalism), a Corps / Fleet an
/// Army / Armada (needs Mobilization). The source must be a Single unit of the same
/// type on an adjacent tile; it is consumed. InvalidArgument for an unknown seat,
/// missing units, a different type or a non-adjacent tile; InvalidUnitAction for a
/// non-military target, a source that is not Single, or a target already at Army;
/// InvalidState when the civic is missing. Until 2026-09-05 nothing called the
/// primitives, so no formation was ever formed.
[[nodiscard]] ErrorCode requestMergeUnits(aoc::game::GameState& gameState, PlayerId player,
                                          hex::AxialCoord at, hex::AxialCoord sourceAt);

// ============================================================================
// Nuclear Weapons
// ============================================================================

enum class NukeType : uint8_t {
    NuclearDevice       = 0,  ///< 1-tile blast, -50% city pop
    ThermonuclearDevice = 1,  ///< 2-tile blast, -75% city pop
};

/// Component tracking whether a unit is equipped with a nuclear weapon and its type.
/// In the legacy ECS, presence of this component meant the unit was armed; in the
/// new object model, every Unit owns one of these by composition, so `equipped`
/// is used to distinguish armed units from unarmed ones.
struct NuclearWeaponComponent {
    bool     equipped = false;              ///< True when the unit carries an active warhead
    NukeType type     = NukeType::NuclearDevice;
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
 * @param gameState     Game state.
 * @param grid          Hex grid (tiles modified with Fallout).
 * @param launcherOwner Owner player of the launching unit.
 * @param targetTile    Target tile coordinate.
 * @param type          Nuclear or thermonuclear.
 * @return Ok if successful.
 */
[[nodiscard]] ErrorCode launchNuclearStrike(aoc::game::GameState& gameState,
                                            aoc::map::HexGrid& grid,
                                            PlayerId launcherOwner,
                                            hex::AxialCoord targetTile,
                                            NukeType type);

// ============================================================================
// Air Combat
// ============================================================================

/// Per-unit air state (meaningful only for `isAirUnit` classes; every Unit
/// carries one). Saved since v13 except `baseCity`, which nothing assigns.
struct AirUnitComponent {
    EntityId baseCity = NULL_ENTITY;   ///< ECS relic: never assigned, not saved
    int32_t  sortiesRemaining = 1;     ///< Missions this turn (reset each turn)
    int32_t  maxSorties = 1;           ///< Max missions per turn
    int32_t  operationalRange = 8;     ///< Max hex range from base
    bool     isIntercepting = false;   ///< Fighter on patrol; set by resetAirSorties
};

/// Fighters (the Biplane -> Fighter -> Jet Fighter -> Stealth Fighter chain)
/// fly interception; bombers never do. Pinned to the UnitTypes table by
/// test_air_sorties, since unitTypeDef() is not constexpr.
[[nodiscard]] constexpr bool isInterceptorType(UnitTypeId id) {
    return id == UnitTypeId{48} || id == UnitTypeId{18} || id == UnitTypeId{49}
        || id == UnitTypeId{50};
}

/**
 * @brief Execute a bombing run on a target tile.
 *
 * Damages units and improvements on the target tile.
 * Bomber must be within operational range of its base.
 *
 * @return Ok if successful.
 */
[[nodiscard]] ErrorCode executeBombingRun(aoc::game::GameState& gameState,
                                          aoc::map::HexGrid& grid,
                                          aoc::game::Unit& bomber,
                                          hex::AxialCoord targetTile);

/**
 * @brief Attempt interception of an enemy air unit.
 *
 * Fighters on intercept mode in range automatically engage enemy
 * aircraft that enter their patrol zone.
 *
 * @return true if interception occurred.
 */
bool attemptInterception(aoc::game::GameState& gameState,
                         aoc::game::Unit& interceptor,
                         aoc::game::Unit& target);

/**
 * @brief Start-of-turn reset for `player`'s air units: sorties back to
 *        `maxSorties`, fighters go on patrol (`isIntercepting`), bombers
 *        never do. Units that are not air units are left untouched.
 *        Called by processTurn for every seat before the AI loop; until
 *        2026-09-05 nothing called it, so an air unit that flew once never
 *        flew again and no fighter ever intercepted.
 */
void resetAirSorties(aoc::game::GameState& gameState, PlayerId player);

} // namespace aoc::sim
