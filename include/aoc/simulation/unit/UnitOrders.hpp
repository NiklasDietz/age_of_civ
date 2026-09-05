#pragma once

/**
 * @file UnitOrders.hpp
 * @brief Unit orders the human lacked (Civ VI plan Phase 2.5, 2026-09-05):
 *        pillage an enemy improvement (heal + gold, the tile yields nothing
 *        until a Builder repairs it), delete a unit for part of its cost, and
 *        the alert stance (a sleeping or fortified unit wakes when an enemy
 *        comes within its radius; processAlertStance existed, nothing set it).
 */

#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

class DiplomacyManager;

/// HP a pillaging unit heals (Civ VI: pillaging heals).
inline constexpr int32_t PILLAGE_HEAL = 50;

/// Gold a pillage yields at era `era`: 25 + 10 per era.
[[nodiscard]] int32_t pillageGold(uint16_t era);

/// The military unit of `player` on `at` pillages the improvement there: the
/// tile must belong to another seat (at war when `diplomacy` is given, majors
/// only), hold an improvement other than a road, and not be pillaged already.
/// Heals PILLAGE_HEAL, pays pillageGold, spends the unit's movement.
[[nodiscard]] ErrorCode requestPillage(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                                       PlayerId player, hex::AxialCoord at,
                                       const DiplomacyManager* diplomacy = nullptr);

/// The Builder of `player` on `at` repairs a pillaged tile the player owns (one charge).
[[nodiscard]] ErrorCode requestRepair(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                                      PlayerId player, hex::AxialCoord at);

/// Disband the unit of `player` on `at`; in own territory a quarter of its
/// production cost comes back as gold.
[[nodiscard]] ErrorCode requestDeleteUnit(aoc::game::GameState& gameState,
                                          const aoc::map::HexGrid& grid, PlayerId player,
                                          hex::AxialCoord at);

/// Set or clear the alert stance of the military unit of `player` on `at`;
/// setting it also puts the unit to sleep so the wake-up has an effect.
[[nodiscard]] ErrorCode requestSetAlert(aoc::game::GameState& gameState, PlayerId player,
                                        hex::AxialCoord at, bool alert);

} // namespace aoc::sim
