#pragma once

/**
 * @file LogisticsComponent.hpp
 * @brief WP-S2 military supply unit state. Refills Encampment buffers
 *        from city stockpiles. Separate from Trader so trade slots
 *        stay clean and war doesn't choke commerce.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>

namespace aoc::sim {

/// State machine for a Logistics unit's supply cycle.
enum class LogisticsState : uint8_t {
    AssigningTask,    ///< Idle. Look for an encampment that needs refill.
    LoadingAtCity,    ///< At source city — drain stockpile into onboard cargo.
    EnRouteToDepot,   ///< Carrying supplies toward target encampment.
    UnloadingAtDepot, ///< At encampment — dump cargo into buffer.
    EnRouteToCity,    ///< Empty, returning to source city for next load.
};

/// Attached to every Logistics unit. Tracks supply cycle + cargo.
struct LogisticsComponent {
    /// City that the wagon is associated with as its base.
    aoc::hex::AxialCoord homeCityLocation{};
    /// Encampment tile being supplied this round.
    aoc::hex::AxialCoord targetDepotLocation{};

    /// Goods on board.
    int32_t food = 0;
    int32_t fuel = 0;

    /// Capacity per trip (Supply Wagon: 30/30, Tanker Ship: 0/60, etc).
    int32_t foodCapacity = 30;
    int32_t fuelCapacity = 30;

    LogisticsState state = LogisticsState::AssigningTask;
};

} // namespace aoc::sim
