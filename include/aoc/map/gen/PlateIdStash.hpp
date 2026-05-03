#pragma once

/**
 * @file PlateIdStash.hpp
 * @brief Final plate-id Voronoi assignment + 6-pass majority-vote smoothing.
 *
 * Re-runs the same warped Voronoi lookup with the FINAL plate positions so
 * tile -> plate ownership matches the elevation-pass assignment, then runs
 * majority-vote passes to clean up boundary splatter.
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

#include "aoc/core/Random.hpp"

#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

struct Plate;

void runPlateIdStash(HexGrid& grid, bool cylindrical,
                     const std::vector<Plate>& plates,
                     aoc::Random& noiseRng);

} // namespace gen
} // namespace aoc::map
