#pragma once

/**
 * @file IceAndRock.hpp
 * @brief Continental ice-sheet expansion + final rock-type assignment.
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

/// Override land tiles owned by polar plates with Snow + Ice feature.
void runIceSheetExpansion(HexGrid& grid);

/// Final rock-type pass. `ophioliteMask` from the suture pass and `sediment`
/// from the post-sim block remain assignTerrain-local; they are passed in
/// and the resulting `rockTypeTile` vector is mutated in-place by the caller.
void runRockTypeAssignment(HexGrid& grid,
                           const std::vector<uint8_t>& ophioliteMask,
                           const std::vector<float>& sediment,
                           std::vector<uint8_t>& rockTypeTile);

} // namespace gen
} // namespace aoc::map
