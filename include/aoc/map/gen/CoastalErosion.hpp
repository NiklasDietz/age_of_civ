#pragma once

/**
 * @file CoastalErosion.hpp
 * @brief Final coastal-arm erosion pass for the Continents generator.
 *
 * Two passes of cellular-automaton smoothing: any land tile with >= 4 water
 * neighbours drowns. Removes 1-2 hex wide peninsulas / fjord-like protrusions
 * and bay-fingers, leaving more compact continent silhouettes. Mountains are
 * exempt -- they are structurally locked by orogeny and shouldn't erode by
 * coastal smoothing alone.
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

namespace aoc::map {

class HexGrid;

namespace gen {

/// Run two CA passes that drown coastal stubs. Mutates `grid` in place.
void runCoastalErosion(HexGrid& grid);

} // namespace gen
} // namespace aoc::map
