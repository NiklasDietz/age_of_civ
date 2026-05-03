#pragma once

/**
 * @file EarthSystem.hpp
 * @brief EARTH-SYSTEM POST-PASSES (lakes, volcanism, hazard, permafrost,
 *        upwelling, deltas, salt flats, soil fertility, hot springs, karst,
 *        inselbergs, sand dunes, tsunami, sea ice, fjords, treeline, wetlands).
 *
 * Returns the 6 core post-pass vectors that downstream sessions read.
 * Biogeographic realms / land bridges / refugia / metamorphic-core-complex
 * stay inline in MapGenerator.cpp because they need direct Plate-struct
 * access from the assignTerrain stack.
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

struct EarthSystemOutputs {
    std::vector<float>   soilFert;
    std::vector<uint8_t> volcanism;
    std::vector<uint8_t> hazard;
    std::vector<uint8_t> permafrost;
    std::vector<uint8_t> lakeFlag;
    std::vector<uint8_t> upwelling;
};

/// Run the lakes-through-wetlands EARTH-SYSTEM block. Mutates `grid` in
/// place (sets terrain/feature/resource on lake/floodplain/salt/etc tiles)
/// and fills the 6 output vectors.
void runEarthSystemPasses(HexGrid& grid, bool cylindrical,
                          const std::vector<float>& orogeny,
                          const std::vector<float>& sediment,
                          EarthSystemOutputs& out);

} // namespace gen
} // namespace aoc::map
