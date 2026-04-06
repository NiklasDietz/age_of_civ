#pragma once

/**
 * @file WarWeariness.hpp
 * @brief War weariness system that penalizes prolonged warfare.
 *
 * War weariness accumulates while at war and decays during peace. High
 * weariness reduces amenities, production, and combat effectiveness.
 *
 * Thresholds:
 *   0-20:   no effect
 *   20-40:  -1 amenity all cities
 *   40-60:  -2 amenities
 *   60-80:  -3 amenities, -10% production
 *   80-100: -5 amenities, -20% production, units fight at -10% strength
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <unordered_map>

namespace aoc::ecs {
class World;
}

namespace aoc::sim {

class DiplomacyManager;

/// ECS component tracking per-player war weariness.
struct PlayerWarWearinessComponent {
    PlayerId owner = INVALID_PLAYER;
    float    weariness = 0.0f;  ///< [0, 100]

    /// Turns spent at war against each enemy player.
    std::unordered_map<PlayerId, int32_t> turnsAtWar;
};

/**
 * @brief Process war weariness for a single player at end of turn.
 *
 * Increments weariness by (1 + combatLosses * 0.5) per turn at war,
 * decreases by 2 per turn at peace.
 */
void processWarWeariness(aoc::ecs::World& world, PlayerId player,
                         const DiplomacyManager& diplomacy);

/**
 * @brief Amenity penalty from war weariness.
 * @return Non-positive float to subtract from amenities.
 */
[[nodiscard]] float warWearinessHappinessPenalty(float weariness);

/**
 * @brief Production multiplier from war weariness.
 * @return 1.0 (no penalty) down to 0.8 (-20%).
 */
[[nodiscard]] float warWearinessProductionModifier(float weariness);

/**
 * @brief Combat strength multiplier from war weariness.
 * @return 1.0 (no penalty) or 0.9 (-10% at extreme weariness).
 */
[[nodiscard]] float warWearinessCombatModifier(float weariness);

} // namespace aoc::sim
