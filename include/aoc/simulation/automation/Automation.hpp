#pragma once

/**
 * @file Automation.hpp
 * @brief Player automation settings and automated unit behaviors.
 *
 * Allows players to automate repetitive tasks:
 *   - Research queue: queue multiple techs in advance
 *   - Auto-explore: scouts move toward unexplored tiles
 *   - Military alert: units auto-wake and intercept nearby enemies
 *   - Trade auto-renewal: expired trade routes auto-restart
 *   - Auto-improve: builders auto-select best improvement for each tile
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

// ============================================================================
// Research Queue
// ============================================================================

/// Per-player research queue: auto-advance to next tech when current completes.
struct PlayerResearchQueueComponent {
    PlayerId owner = INVALID_PLAYER;
    std::vector<TechId> researchQueue;

    /// Pop the next tech to research. Returns invalid TechId if queue empty.
    [[nodiscard]] TechId popNext() {
        if (this->researchQueue.empty()) {
            return TechId{};
        }
        TechId next = this->researchQueue.front();
        this->researchQueue.erase(this->researchQueue.begin());
        return next;
    }
};

// ============================================================================
// Unit Automation Flags
// ============================================================================

/// Per-unit automation settings (ECS component, optional).
struct UnitAutomationComponent {
    /// Scout auto-explore: move toward nearest unexplored tile.
    bool autoExplore = false;

    /// Military alert: auto-wake from sleep/fortify when enemy enters radius.
    bool alertStance = false;
    int32_t alertRadius = 3;  ///< Tiles around unit to watch for enemies.

    /// Builder auto-improve: automatically select and build improvements.
    bool autoImprove = false;

    /// Trader auto-renew: when trade route expires, restart it.
    bool autoRenewRoute = false;
    EntityId autoRenewDestCity = NULL_ENTITY;  ///< City to auto-renew route to.
};

// ============================================================================
// Processing functions
// ============================================================================

/**
 * @brief Process research queue: if current research is complete and queue
 *        has next entry, automatically start researching it.
 */
void processResearchQueue(aoc::game::GameState& gameState, PlayerId player);

/**
 * @brief Process auto-explore for all scout units with the flag set.
 *
 * Scouts move toward the nearest fog-of-war tile they haven't visited.
 */
void processAutoExplore(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, PlayerId player);

/**
 * @brief Process military alert: wake units when enemies enter their radius.
 */
void processAlertStance(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid, PlayerId player);

/**
 * @brief Process all automation for a player (called once per turn).
 */
void processAutomation(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, PlayerId player);

} // namespace aoc::sim
