#pragma once

/**
 * @file DomesticCourier.hpp
 * @brief Player-dispatched goods transport between own cities.
 *
 * Couriers differ from Traders:
 *   - Trader: player-built in production queue, for international routes,
 *             generates gold via market price differentials.
 *   - Courier: auto-spawned when the player dispatches goods from one of
 *              their own cities to another, never buildable directly,
 *              no gold profit -- pure goods movement.
 *
 * A Courier picks up a fixed cargo at the source city, walks to the
 * destination, deposits the cargo into the destination stockpile, and
 * despawns. If the Courier is killed along the way the cargo is lost.
 *
 * Slot budget per source city is derived from city stage:
 *   Hamlet 0, Village 1, Town 2, City 3.
 *
 * Stockpile caps per city are also stage-derived (see stockpileCap()).
 * Overflow above cap is lost -- players are expected to redistribute
 * surplus via Couriers.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game { class GameState; class City; class Unit; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

/// Persistent dispatch rule. Each turn the courier system tries to re-dispatch
/// this (good, batchSize) from its source city to `destCityLocation` whenever
/// a free slot is available and stockpile has enough goods. Removed manually
/// by the player via the Couriers tab.
struct StandingOrder {
    aoc::hex::AxialCoord destCityLocation{};
    uint16_t goodId = 0;
    int32_t  batchSize = 0;
};

/// State of an active Courier unit.
struct DomesticCourierComponent {
    PlayerId             owner = INVALID_PLAYER;
    aoc::hex::AxialCoord originCityLocation{};
    aoc::hex::AxialCoord destCityLocation{};

    uint16_t goodId = 0;
    int32_t  quantity = 0;

    /// Planned path from origin to destination. Empty outside of flight.
    std::vector<aoc::hex::AxialCoord> path;
    int32_t pathIndex = 0;

    /// True once the Courier has reached the destination and can be despawned.
    bool delivered = false;
};

/// Courier slot budget derived from city stage.
[[nodiscard]] int32_t courierSlots(const aoc::game::City& city);

/// Stockpile cap derived from city stage. Any per-good amount above this is
/// clamped at end-of-turn.
[[nodiscard]] int32_t stockpileCap(const aoc::game::City& city);

/// Count active (undelivered) couriers dispatched from `originLoc` by `owner`.
[[nodiscard]] int32_t countActiveCouriersFrom(const aoc::game::GameState& gameState,
                                               PlayerId owner,
                                               aoc::hex::AxialCoord originLoc);

/// Dispatch a Courier. Validates source has the goods and a free slot, then
/// spawns a unit at the source city and primes its CourierComponent.
/// Returns true on success.
bool dispatchCourier(aoc::game::GameState& gameState,
                      const aoc::map::HexGrid& grid,
                      PlayerId owner,
                      aoc::hex::AxialCoord sourceCityLoc,
                      aoc::hex::AxialCoord destCityLoc,
                      uint16_t goodId,
                      int32_t quantity);

/// Advance all active Couriers one turn: step along path, deliver on arrival,
/// despawn delivered couriers. Also applies per-city stockpile cap clamping.
void processDomesticCouriers(aoc::game::GameState& gameState,
                              aoc::map::HexGrid& grid);

} // namespace aoc::sim
