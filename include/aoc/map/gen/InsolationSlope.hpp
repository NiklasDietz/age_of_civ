#pragma once

/**
 * @file Session13.hpp
 * @brief SESSION 13 -- solar insolation / topographic aspect / slope /
 *        ecotones / pelagic productivity / shelf sediment thickness /
 *        glacial rebound / sediment transport direction / coastal change.
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

#include <cstdint>

namespace aoc::map {

class HexGrid;

namespace gen {

/// Run SESSION 13 analytics. `axialTilt` in degrees (Earth = 23.5).
void runInsolationSlope(HexGrid& grid, bool cylindrical, float axialTilt);

} // namespace gen
} // namespace aoc::map
