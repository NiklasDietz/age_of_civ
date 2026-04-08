#pragma once

/**
 * @file GameNotifications.hpp
 * @brief Notification dispatcher for all game systems.
 *
 * Converts game events into player-visible notifications using the
 * existing Notifications toast system. Each system pushes events here,
 * and they're formatted and displayed to the player.
 *
 * Notification categories:
 *   - Economy:   Currency crisis, industrial revolution, trade route established
 *   - Military:  Unit destroyed, city captured, nuclear strike
 *   - Diplomacy: War declared, alliance formed, sanctions imposed
 *   - City:      City founded, wonder built, building completed, strike
 *   - Science:   Tech researched, eureka triggered
 *   - Religion:  Religion founded, city converted
 *   - Disaster:  Volcanic eruption, earthquake, hurricane, drought
 *   - Government: Government changed, policy enacted, anarchy
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string>

namespace aoc::ecs { class World; }
namespace aoc::ui { class UIManager; }

namespace aoc::ui {

enum class NotificationCategory : uint8_t {
    Economy,
    Military,
    Diplomacy,
    City,
    Science,
    Religion,
    Disaster,
    Government,

    Count
};

struct GameNotification {
    NotificationCategory category;
    std::string          title;
    std::string          body;
    PlayerId             relevantPlayer = INVALID_PLAYER;
    int32_t              priority = 0;  ///< Higher = more important (shown first)
};

/**
 * @brief Push a notification to the player's notification queue.
 *
 * High-priority notifications (crises, war declarations, nuclear strikes)
 * pause the game and show a centered alert.
 * Normal notifications show as toast messages.
 */
void pushNotification(const GameNotification& notification);

/**
 * @brief Generate notifications from the current game state.
 *
 * Scans for events that occurred this turn and creates appropriate
 * notifications. Called once per turn after all systems have processed.
 *
 * @param world  ECS world.
 * @param player Human player to generate notifications for.
 */
void generateTurnNotifications(const aoc::ecs::World& world, PlayerId player);

} // namespace aoc::ui
