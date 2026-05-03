#pragma once

/**
 * @file Session17.hpp
 * @brief SESSION 17 -- terminal Earth-system analytics for Continents maps.
 *
 * Computes per-tile PET (Hargreaves potential evapotranspiration), aridity
 * index, RUSLE-like erosion potential, carbon stock, wilderness flag, flood
 * frequency, canopy stratification, riparian forest density, magnetic
 * intensity proxy, and groundwater depth. All inputs are read from existing
 * grid getters populated by earlier sessions; outputs go to the corresponding
 * setters. Pure terminal pass: nothing else in MapGenerator reads these.
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

namespace aoc::map {

class HexGrid;

namespace gen {

/// Run SESSION 17 analytics. Mutates `grid` in place via grid.set* methods.
void runSession17(HexGrid& grid);

} // namespace gen
} // namespace aoc::map
