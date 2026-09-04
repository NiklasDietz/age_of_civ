#pragma once

/**
 * @file StartPlacement.hpp
 * @brief Start-position picker shared by the graphical game and the headless sim.
 *
 * Both front ends used to place starts independently and neither looked at
 * landmasses: a default 2-player game regularly put the rivals on separate
 * continents with no land path, so they never met. This picker fills the
 * largest landmass first and only moves on when no tile there keeps the
 * minimum spacing.
 */

#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <vector>

namespace aoc {
class Random;
}

namespace aoc::map {

class HexGrid;

struct StartPlacementOptions {
    int32_t minStartDistance = 8;    ///< Hex distance any two starts must keep.
    int32_t minLandmassTiles = 12;   ///< Landmasses smaller than this never host a start.
    int32_t edgeMargin       = 2;    ///< Rows and columns at the map edge that never host a start.
    int32_t samplesPerStart  = 2000; ///< Random candidates scored for each player.
};

/// One start tile per player, in player order, all on the largest landmass
/// while it can hold them at `minStartDistance`, then the next largest.
/// Deterministic for a given grid and rng state. Returns fewer entries than
/// `playerCount` only when the map has no land at all.
[[nodiscard]] std::vector<hex::AxialCoord>
chooseStartPositions(const HexGrid& grid, int32_t playerCount, Random& rng,
                     const StartPlacementOptions& options = {});

} // namespace aoc::map
