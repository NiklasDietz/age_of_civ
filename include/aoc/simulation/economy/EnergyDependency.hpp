#pragma once

/**
 * @file EnergyDependency.hpp
 * @brief Oil/gas scarcity, energy dependency, and peak oil mechanics.
 *
 * Oil is a game-changer when discovered: massive production, military, and
 * economic bonuses. But it's FINITE. The threat of running out creates the
 * most consequential strategic decision in the game:
 *
 *   - Burn cheap oil for short-term dominance (tanks, industry, plastics)
 *   - Invest early in renewables/nuclear at higher initial cost
 *   - Or conquer oil-rich neighbors to extend your supply
 *
 * Energy dependency tracks how reliant a civilization's economy is on oil.
 * High dependency means massive bonuses NOW but catastrophic penalties when
 * oil runs out ("oil shock"). Low dependency means slower growth but resilience.
 *
 * Peak oil is a global event: when total oil extraction across all civs
 * exceeds remaining global reserves, prices spike and oil-dependent civs
 * suffer economic crises.
 */

#include "aoc/core/Types.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

// ============================================================================
// Energy dependency per player (ECS component)
// ============================================================================

struct PlayerEnergyComponent {
    PlayerId owner = INVALID_PLAYER;

    /// How dependent the economy is on oil (0.0 = none, 1.0 = fully dependent).
    /// Grows when oil is consumed, shrinks slowly when alternatives are used.
    float oilDependency = 0.0f;

    /// Total oil consumed this turn (from city stockpiles used in production).
    int32_t oilConsumedThisTurn = 0;

    /// Total renewable energy capacity (from Solar, Wind, Hydro, Nuclear buildings).
    int32_t renewableCapacity = 0;

    /// Whether this player has experienced an oil shock (reserves ran out).
    bool inOilShock = false;
    int32_t oilShockTurnsRemaining = 0;

    // ========================================================================
    // Bonuses from oil dependency (the carrot)
    // ========================================================================

    /// Production multiplier from oil-powered industry.
    /// At full dependency: +30% production. At zero: no bonus.
    [[nodiscard]] float oilProductionBonus() const {
        if (this->inOilShock) { return 0.70f; }  // -30% during oil shock
        return 1.0f + this->oilDependency * 0.30f;
    }

    /// Military strength multiplier (mechanized warfare, air power).
    [[nodiscard]] float oilMilitaryBonus() const {
        if (this->inOilShock) { return 0.60f; }  // -40% during shock (no fuel)
        return 1.0f + this->oilDependency * 0.25f;
    }

    /// Gold income multiplier (petrochemicals, plastics, fertilizer).
    [[nodiscard]] float oilEconomyBonus() const {
        if (this->inOilShock) { return 0.75f; }
        return 1.0f + this->oilDependency * 0.20f;
    }

    // ========================================================================
    // Renewable energy offset
    // ========================================================================

    /// Fraction of energy needs met by renewables (reduces oil shock severity).
    [[nodiscard]] float renewableOffset() const {
        if (this->renewableCapacity <= 0) { return 0.0f; }
        // Each renewable building provides ~10% offset, cap at 80%
        float offset = static_cast<float>(this->renewableCapacity) * 0.10f;
        return (offset > 0.80f) ? 0.80f : offset;
    }
};

// ============================================================================
// Global oil reserves tracking
// ============================================================================

struct GlobalOilReserves {
    /// Total oil reserves remaining across the entire map.
    int64_t totalRemaining = 0;

    /// Total oil extracted this turn across all players.
    int32_t extractedThisTurn = 0;

    /// Peak oil threshold: when remaining < 50% of initial, prices spike.
    int64_t initialTotal = 0;

    /// Whether peak oil has been reached.
    bool peakOilReached = false;

    /// Turns since peak oil was reached.
    int32_t turnsSincePeakOil = 0;

    /// Oil price multiplier from scarcity.
    [[nodiscard]] float scarcityPriceMultiplier() const {
        if (this->initialTotal <= 0) { return 1.0f; }
        float remaining = static_cast<float>(this->totalRemaining)
                        / static_cast<float>(this->initialTotal);
        if (remaining > 0.50f) { return 1.0f; }           // Abundant
        if (remaining > 0.25f) { return 1.5f; }           // Getting scarce
        if (remaining > 0.10f) { return 3.0f; }           // Crisis pricing
        return 5.0f;                                        // Near-zero reserves
    }
};

// ============================================================================
// Functions
// ============================================================================

/**
 * @brief Update energy dependency for a player based on oil consumption.
 *
 * Called once per turn. Tracks how much oil was consumed and adjusts
 * the dependency ratio. High consumption increases dependency (and bonuses).
 * Renewable capacity reduces effective dependency.
 */
void updateEnergyDependency(PlayerEnergyComponent& energy,
                             int32_t oilConsumed,
                             int32_t renewableBuildingCount);

/**
 * @brief Update global oil reserves by scanning all oil tiles on the map.
 *
 * Called once per turn. Sums remaining oil reserves across all tiles.
 * Triggers peak oil event when reserves drop below 50% of initial.
 */
void updateGlobalOilReserves(const aoc::map::HexGrid& grid,
                              GlobalOilReserves& reserves);

/**
 * @brief Process oil shock for a player whose oil supply ran out.
 *
 * Oil shock lasts 10 turns and causes severe economic/military penalties.
 * Renewable energy offset reduces the severity.
 */
void processOilShock(PlayerEnergyComponent& energy);

/**
 * @brief Count renewable energy buildings for a player.
 *
 * Counts Solar Array, Wind Farm, Hydroelectric Dam, and Nuclear Plant
 * buildings across all the player's cities.
 */
[[nodiscard]] int32_t countRenewableBuildings(const aoc::game::GameState& gameState,
                                               PlayerId player);

} // namespace aoc::sim
