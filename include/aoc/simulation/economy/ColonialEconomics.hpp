#pragma once

/**
 * @file ColonialEconomics.hpp
 * @brief Colonial economic zones and mercantilism mechanics.
 *
 * A strong-economy civ can establish "economic zones" in weaker civs'
 * cities, extracting raw resources at below-market prices.
 *
 * Economic Zone:
 *   - Established via diplomacy (requires dominant economic position).
 *   - The colonial power extracts a percentage of the city's raw resource
 *     output each turn (default 30%).
 *   - The host city receives a small gold payment (below market value).
 *   - The host civ's loyalty in that city decreases over time.
 *
 * Requirements to establish:
 *   - Colonizer's GDP must be at least 2x the target's GDP.
 *   - Target must not be in an alliance with a stronger power.
 *   - Requires open borders or military presence near the city.
 *
 * Effects:
 *   - Colonizer gets cheap raw materials (fuels production chains).
 *   - Host city gets some gold but loses economic sovereignty.
 *   - Host city loyalty drops 2 per turn (can lead to revolt).
 *   - Independence war: if loyalty hits 0, city may flip to a free
 *     city-state or rebel -- ties into the existing loyalty system.
 *
 * Mercantilist Policy (government policy card):
 *   - +25% tariff on all imports.
 *   - +15% production from economic zones.
 *   - -10% trade efficiency (trading partners don't like it).
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <vector>

namespace aoc::ecs { class World; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

class Market;

// ============================================================================
// Economic zone definition
// ============================================================================

struct EconomicZone {
    PlayerId colonizer = INVALID_PLAYER;   ///< Player extracting resources
    PlayerId host = INVALID_PLAYER;        ///< Player whose city is being exploited
    EntityId hostCityEntity = NULL_ENTITY;  ///< Which city
    float    extractionRate = 0.30f;        ///< Fraction of raw resources extracted (30%)
    float    paymentRate = 0.50f;           ///< Fraction of market value paid to host (50%)
    int32_t  turnsActive = 0;
};

/// Global tracker for all economic zones.
struct GlobalEconomicZoneTracker {
    std::vector<EconomicZone> zones;

    /// Check if a city has an economic zone.
    [[nodiscard]] bool hasZone(EntityId cityEntity) const {
        for (const EconomicZone& z : this->zones) {
            if (z.hostCityEntity == cityEntity) {
                return true;
            }
        }
        return false;
    }

    /// Get the colonizer of a city (INVALID_PLAYER if none).
    [[nodiscard]] PlayerId colonizer(EntityId cityEntity) const {
        for (const EconomicZone& z : this->zones) {
            if (z.hostCityEntity == cityEntity) {
                return z.colonizer;
            }
        }
        return INVALID_PLAYER;
    }
};

// ============================================================================
// Operations
// ============================================================================

/**
 * @brief Establish an economic zone in a target city.
 *
 * @param world       ECS world.
 * @param tracker     Global zone tracker.
 * @param colonizer   Player establishing the zone.
 * @param hostCity    Target city entity.
 * @return Ok if successful, InvalidArgument if requirements not met.
 */
[[nodiscard]] ErrorCode establishEconomicZone(aoc::ecs::World& world,
                                              GlobalEconomicZoneTracker& tracker,
                                              PlayerId colonizer,
                                              EntityId hostCity);

/**
 * @brief Dissolve an economic zone (voluntarily or via revolt).
 *
 * @param tracker     Global zone tracker.
 * @param hostCity    City whose zone is being dissolved.
 */
void dissolveEconomicZone(GlobalEconomicZoneTracker& tracker, EntityId hostCity);

/**
 * @brief Per-turn processing of all economic zones.
 *
 * For each zone:
 * 1. Extract raw resources from host city stockpile.
 * 2. Pay the host a fraction of market value.
 * 3. Transfer extracted goods to colonizer's nearest city.
 * 4. Reduce host city loyalty by 2.
 * 5. If loyalty hits 0, dissolve the zone and trigger revolt.
 *
 * @param world   ECS world.
 * @param grid    Hex grid.
 * @param market  Market (for pricing extracted goods).
 * @param tracker Zone tracker.
 */
void processEconomicZones(aoc::ecs::World& world,
                          const aoc::map::HexGrid& grid,
                          const Market& market,
                          GlobalEconomicZoneTracker& tracker);

} // namespace aoc::sim
