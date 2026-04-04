#pragma once

/**
 * @file TradeRoute.hpp
 * @brief Trade route entities and trade agreement structures.
 */

#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <vector>

namespace aoc::sim {

/// A single trade offer (one direction of a bilateral agreement).
struct TradeOffer {
    uint16_t goodId;
    int32_t  amountPerTurn;
};

/// Bilateral trade agreement between two players.
struct TradeAgreement {
    PlayerId  playerA;
    PlayerId  playerB;
    std::vector<TradeOffer> offersA;  ///< What player A gives to B each turn
    std::vector<TradeOffer> offersB;  ///< What player B gives to A each turn
    CurrencyAmount goldFromAToB = 0;  ///< Per-turn gold transfer (can be negative)
    TurnNumber     startTurn = 0;
    int32_t        duration  = 30;     ///< Turns until expiration (0 = indefinite)
    float          tariffRate = 0.0f;  ///< Import tax percentage
};

/// ECS component for an active trade route (physical path on the map).
struct TradeRouteComponent {
    EntityId sourceCityId;
    EntityId destCityId;
    PlayerId sourcePlayer;
    PlayerId destPlayer;

    /// Goods being transported this turn.
    std::vector<TradeOffer> cargo;

    /// Path the trade route follows (for rendering and pillage vulnerability).
    std::vector<hex::AxialCoord> path;

    /// Turns remaining until cargo arrives (based on path length).
    int32_t turnsRemaining = 1;
};

} // namespace aoc::sim
