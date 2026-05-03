#pragma once

/**
 * @file CliffCoast.hpp
 * @brief Cliff coast classification for the Continents generator.
 *
 * Tags coastal land tiles with cliff type:
 *   1 hard rock cliff, 2 fjord wall, 3 wave-cut headland, 4 ice cliff.
 * Beach (passable) coasts are everything else.
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

namespace aoc::map {

class HexGrid;

namespace gen {

void runCliffCoast(HexGrid& grid, bool cylindrical);

} // namespace gen
} // namespace aoc::map
