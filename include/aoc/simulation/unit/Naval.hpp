#pragma once

/**
 * @file Naval.hpp
 * @brief Naval unit movement, embarkation, and transport logic.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

namespace aoc::game { class Unit; }
namespace aoc::map  { class HexGrid; }

namespace aoc::sim {

struct PlayerTechComponent;

/**
 * @brief Attempt to embark a land unit onto an adjacent coast tile.
 *
 * Changes the unit state to Embarked and consumes all remaining movement.
 * The unit must be a non-naval land unit adjacent to the target coast tile.
 *
 * @param unit       The land unit to embark.
 * @param coastTile  Target coast tile.
 * @param grid       Hex grid for terrain checks.
 * @return true if embarkation succeeded.
 */
/// With `tech` given, Civ VI's gate applies: civilians embark after Sailing
/// (31), military units after Shipbuilding (42). Before 2026-09-05 a turn-1
/// Warrior could embark.
///
/// ONLY THE HUMAN EVER CALLS THIS. tryEmbark and tryDisembark are reached from
/// exactly one place, the right-click handler in Application.cpp; nothing under
/// src/simulation/ai mentions embarking, transports, or UnitState::Embarked,
/// and requestLoadUnit / requestUnloadUnit in UnitTransport.hpp -- the separate
/// carry-aboard-a-ship mechanism -- have no caller at all. So no AI unit can
/// cross water by any route: no naval invasion, no settling another landmass,
/// no contact with anyone it cannot walk to.
///
/// Measured on seed 42, whose four civs all share one continent so the gap
/// never shows in the golden runs: of 4008 land tiles, civs can reach 1804
/// (45%). The other 2204 across seven landmasses -- including a second
/// continent of 1745, nearly as large as the one they live on -- are
/// permanently outside the game for every AI player, and three city-states
/// (200, 201, 203) sit out there unreachable, unmeetable and unlevyable.
/// On an archipelago map the AI would be crippled rather than merely fenced in.
///
/// Building this is a feature, not a wiring fix: it needs water pathfinding for
/// land units, a landing-site choice, escort logic so a loaded stack is not
/// free kills, and a reason for the AI to want the far shore. It is the
/// "naval invasion planning" item on the unbuilt-features list.
[[nodiscard]] bool tryEmbark(aoc::game::Unit& unit,
                              hex::AxialCoord coastTile,
                              const aoc::map::HexGrid& grid,
                              const PlayerTechComponent* tech = nullptr);

/**
 * @brief Attempt to disembark an embarked unit onto an adjacent land tile.
 *
 * Changes the unit state back to Idle and consumes all remaining movement.
 * The unit must be Embarked and adjacent to the target land tile.
 *
 * @param unit      The embarked unit.
 * @param landTile  Target land tile.
 * @param grid      Hex grid for terrain checks.
 * @return true if disembarkation succeeded.
 */
[[nodiscard]] bool tryDisembark(aoc::game::Unit& unit,
                                hex::AxialCoord landTile,
                                const aoc::map::HexGrid& grid);

/**
 * @brief Check if a unit type can traverse water tiles natively.
 *
 * Returns true for Naval class units. Embarked status is checked separately
 * via the unit's current state.
 *
 * @param typeId The unit type to check.
 * @return true if the unit type is inherently water-capable.
 */
[[nodiscard]] bool canTraverseWater(UnitTypeId typeId);

} // namespace aoc::sim
