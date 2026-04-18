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
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

class Market;
class DiplomacyManager;
enum class TradeRouteType : uint8_t;

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
// Trade Route Auto-Renewal Queue (per-player)
// ============================================================================

/// A pending request to re-establish a trade route. Created when a trader
/// flagged autoRenewRoute expires or is pillaged; consumed next turn.
struct PendingTradeRoute {
    aoc::hex::AxialCoord origin{};     ///< Origin city location.
    aoc::hex::AxialCoord destination{}; ///< Destination city location.
    PlayerId             destOwner = INVALID_PLAYER;
    TradeRouteType       routeType{};
    int32_t              turnsWaiting = 0; ///< Age; request drops after N turns.
};

/// Per-player queue of pending auto-renew trade route requests.
struct PlayerTradeAutoRenewComponent {
    PlayerId owner = INVALID_PLAYER;
    std::vector<PendingTradeRoute> pending;
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
 * @brief Queue a pending auto-renew request after a flagged trader expires or is pillaged.
 */
void queueAutoRenewRequest(aoc::game::GameState& gameState,
                           PlayerId owner,
                           aoc::hex::AxialCoord origin,
                           aoc::hex::AxialCoord destination,
                           PlayerId destOwner,
                           TradeRouteType routeType);

/**
 * @brief Process queued auto-renew requests: spawn replacement traders and
 *        re-establish each route at its original origin/destination pair.
 */
void processAutoRenewTradeRoutes(aoc::game::GameState& gameState,
                                 aoc::map::HexGrid& grid,
                                 const Market& market,
                                 DiplomacyManager* diplomacy,
                                 PlayerId player);

/**
 * @brief Auto-equip best-scoring policy cards into empty slots if the
 *        player's `autoPolicies` flag is set. No-op during anarchy or
 *        when the flag is off. Already-equipped slots are preserved.
 */
void processAutoPolicies(aoc::game::GameState& gameState, PlayerId player);

/**
 * @brief Auto-tune import tariffs and toll rates based on treasury health
 *        and diplomatic stance. No-op when the player's `autoTariffs` flag
 *        is off. Per-player overrides rewritten each turn.
 */
void processAutoTariffs(aoc::game::GameState& gameState,
                        const DiplomacyManager* diplomacy,
                        PlayerId player);

/**
 * @brief Move religious units (Missionaries/Apostles) toward the best
 *        conversion target when `autoSpreadReligion` is set, then spread
 *        a charge when standing on a city whose dominant religion differs
 *        from the unit's. Skips cities belonging to war enemies.
 */
void processAutoSpreadReligion(aoc::game::GameState& gameState,
                               aoc::map::HexGrid& grid,
                               const DiplomacyManager* diplomacy,
                               PlayerId player);

/**
 * @brief Process all automation for a player (called once per turn).
 */
void processAutomation(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, PlayerId player);

} // namespace aoc::sim
