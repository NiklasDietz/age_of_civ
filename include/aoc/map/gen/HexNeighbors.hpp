#pragma once

/**
 * @file HexNeighbors.hpp
 * @brief Offset-coord hex neighbour lookup with optional cylindrical wrap.
 *
 * Replaces the captured `nbHelper` lambda inside MapGenerator::assignTerrain
 * so extracted SESSION passes can use the same helper without dragging the
 * whole local lambda capture in.
 */

#include <cstdint>

namespace aoc::map::gen {

/// Compute the linear tile index of the hex neighbour at direction `dir` in
/// [0,5] from offset-coord (col, row). Cylindrical maps wrap on the
/// horizontal axis. Returns false if the neighbour falls off a true edge.
[[nodiscard]] bool hexNeighbor(int32_t width, int32_t height, bool cylindrical,
                                int32_t col, int32_t row, int32_t dir,
                                int32_t& outIdx);

} // namespace aoc::map::gen
