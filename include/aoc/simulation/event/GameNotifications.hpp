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
#include <vector>

namespace aoc::game { class GameState; }

// 2026-05-03: lifted out of aoc::ui because simulation subsystems push
// notifications and headless aoc_simulate must link the queue without
// pulling in any UI/Vulkan code. Render/UI side still consumes the
// drained queue via aoc::sim::event::drainNotifications().
namespace aoc::sim::event {

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
    /// Second party for diplomacy / trade notifications (wars, alliances, deals).
    /// Either `relevantPlayer` or `otherPlayer` matching the human triggers display.
    PlayerId             otherPlayer = INVALID_PLAYER;
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
 * @brief Drain the pending notification queue, filtering to those whose
 *        relevantPlayer or otherPlayer matches `viewer`, or that are broadcast
 *        (relevantPlayer == INVALID_PLAYER && otherPlayer == INVALID_PLAYER).
 *
 * The queue is cleared regardless of viewer match so each turn's events fire
 * exactly once. Callers (Application, headless sim) route the drained entries
 * to their preferred display sink (toast, event log, console).
 */
[[nodiscard]] std::vector<GameNotification> drainNotifications(PlayerId viewer);

/**
 * @brief Generate notifications from the current game state.
 *
 * Scans for events that occurred this turn and creates appropriate
 * notifications. Called once per turn after all systems have processed.
 *
 * @param gameState  Game state.
 * @param player     Human player to generate notifications for.
 */
void generateTurnNotifications(const aoc::game::GameState& gameState, PlayerId player);

} // namespace aoc::sim::event
