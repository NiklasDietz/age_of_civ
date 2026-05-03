#pragma once

/**
 * @file LakePurge.hpp
 * @brief Inland-lake purge pass for the Continents generator.
 *
 * Connected-component flood-fill on WATER tiles. Components below
 * MIN_LAKE_SIZE that do not touch the map border are filled with land
 * (Plains, sediment-deposited basins). Real Earth has only a few large
 * internal seas (Caspian, Black, Aral) so the noise-driven swiss-cheese
 * mid-size internal seas need to be consolidated.
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

namespace aoc::map {

class HexGrid;

namespace gen {

/// Run lake purge. `cylindricalTopology` controls whether the column edges
/// of the map are treated as wrapping (= not a true edge for the
/// touches-border check).
void runLakePurge(HexGrid& grid, bool cylindricalTopology);

} // namespace gen
} // namespace aoc::map
