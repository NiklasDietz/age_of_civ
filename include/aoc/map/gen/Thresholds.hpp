#pragma once

/**
 * @file Thresholds.hpp
 * @brief Water + mountain elevation-threshold computation, plus the
 *        BFS distance-to-coast field and coastal-ridge bias.
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

#include "aoc/map/MapGenerator.hpp"

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

struct ThresholdResult {
    std::vector<float>   mountainElev;
    std::vector<int32_t> distFromCoast;
    float                waterThreshold    = 0.0f;
    float                mountainThreshold = 0.0f;
};

/// Compute water + mountain elevation thresholds and the BFS distance
/// field. `elevationMap` is read-only here. Caller passes it in by value;
/// `mountainElev` (returned) is `elevationMap` with the coastal-ridge
/// adjustment applied so the mountain percentile picks belt-tiles instead
/// of arbitrary peaks.
void runThresholdComputation(HexGrid& grid, MapType mapType,
                             float effectiveWaterRatio, float mountainRatio,
                             const std::vector<float>& elevationMap,
                             ThresholdResult& out);

} // namespace gen
} // namespace aoc::map
