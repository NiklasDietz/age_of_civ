#pragma once

/**
 * @file TheologicalCombat.hpp
 * @brief Theological combat between religious units (Apostle vs Apostle).
 *
 * Religious units can engage in theological combat on the same or adjacent tiles.
 * Combat uses religious strength (similar to military combat but separate).
 * Losing unit is destroyed. Winner's religion gains spread in the area.
 * Inquisitors can purge foreign religions from cities.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

/// Religious combat strength by unit type.
[[nodiscard]] constexpr int32_t religiousCombatStrength(UnitTypeId typeId) {
    // Missionary: weak in combat (20), Apostle: strong (40), Inquisitor: medium (30)
    switch (typeId.value) {
        case 25: return 20;  // Missionary
        case 26: return 40;  // Apostle
        case 24: return 30;  // Inquisitor
        default: return 0;
    }
}

/**
 * @brief Execute theological combat between two religious units.
 *
 * Both units must be religious. Combat resolves based on religious strength.
 * Loser is destroyed. Winner loses some HP.
 * Winner's religion gains +10 pressure in all cities within 3 tiles.
 *
 * @return Ok if combat occurred.
 */
[[nodiscard]] ErrorCode resolveTheologicalCombat(aoc::game::GameState& gameState,
                                                  const aoc::map::HexGrid& grid,
                                                  EntityId attackerEntity,
                                                  EntityId defenderEntity);

/**
 * @brief Inquisitor purges foreign religion from a city.
 *
 * Removes all non-native religious pressure from the target city.
 * Consumes one charge from the inquisitor.
 *
 * @return Ok if purge occurred.
 */
[[nodiscard]] ErrorCode purgeReligion(aoc::game::GameState& gameState,
                                      EntityId inquisitorEntity,
                                      EntityId cityEntity);

} // namespace aoc::sim
