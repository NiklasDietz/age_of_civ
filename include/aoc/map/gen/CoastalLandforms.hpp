#pragma once

/**
 * @file Session12.hpp
 * @brief SESSION 12 -- coastal landforms / river regime / arid erosion /
 *        transform fault subtypes / lake-effect snow / drumlin alignment /
 *        suture reactivation.
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

void runCoastalLandforms(HexGrid& grid, bool cylindrical,
                  const std::vector<uint8_t>& lakeFlag,
                  const std::vector<float>& orogeny);

} // namespace gen
} // namespace aoc::map
