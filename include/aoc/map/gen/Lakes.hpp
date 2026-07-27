#pragma once

/**
 * @file Lakes.hpp
 * @brief Endorheic-basin (closed-depression) detection -> inland lakes.
 *
 * Until 2026-07-27 EVERY generated world contained zero lakes. The only writer
 * of `lakeFlag` was gated on `orogeny[i] < -0.06f` (EarthSystem.cpp), but
 * `orogeny` only ever holds {0.0, 0.10, 1.0}, so the branch was unreachable --
 * while five passes downstream (CoastalLandforms, DrainageLivestock,
 * Biogeography, BiomeSubtypes, Resources) read the flag and had therefore been
 * silently inert since it was introduced.
 *
 * A lake is not a noise feature: it is what happens when a closed depression
 * fills. So this finds the closed depressions directly, with the standard
 * priority-flood algorithm (Barnes, Lehman & Mulla 2014, "Priority-flood: an
 * optimal depression-filling and watershed-labeling algorithm for digital
 * elevation models", Computers & Geosciences 62:117-127).
 */

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

/// Result of the depression-fill sweep.
struct LakeResult {
    /// 1 where the tile is inland water produced by a filled depression.
    std::vector<uint8_t> lakeFlag;
    /// Water depth in metres at each flooded tile, 0 elsewhere. Kept separate
    /// from the flag because depth is what distinguishes a shallow playa from
    /// an inland sea, and the resource and biome passes want that distinction.
    std::vector<float> lakeDepthM;
    /// Count of distinct lakes and total flooded tiles, for logging.
    int32_t lakeCount    = 0;
    int32_t floodedTiles = 0;
};

/// Find closed depressions in `elevationMap` and flood each to its spill point.
///
/// `elevationMap` is the unitless world-frame elevation (one unit = 5000 m);
/// tiles below `waterThreshold` are treated as the ocean, i.e. as the drain that
/// terminates every flow path. A land tile ends up flooded when the lowest
/// route from it to the ocean has to climb: the height it must climb to is the
/// spill point, and the water surface sits there.
///
/// `minDepthM` suppresses films thinner than a real lake, which otherwise appear
/// wherever a plateau is flat to within the sampling precision. `minTiles`
/// suppresses single-cell pits that are sampling artefacts rather than basins.
[[nodiscard]] LakeResult findEndorheicLakes(const HexGrid& grid,
                                            const std::vector<float>& elevationMap,
                                            float waterThreshold, float minDepthM,
                                            int32_t minTiles);

} // namespace gen
} // namespace aoc::map
