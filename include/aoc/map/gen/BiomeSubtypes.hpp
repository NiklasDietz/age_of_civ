#pragma once

/**
 * @file Session4.hpp
 * @brief SESSION 4 -- natural hazards / biome subtypes / marine zones /
 *        wildlife / disease / energy potentials / atmospheric extras /
 *        hydrological extras / event markers.
 *
 * Outputs returned via BiomeSubtypesOutputs so MapGenerator.cpp can keep setter
 * calls inline at the original location (preserves setter ORDER).
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

#include "aoc/core/Random.hpp"

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

struct BiomeSubtypesInputs {
    bool                          cylindrical;
    uint64_t                      seed;
    const std::vector<float>*     sediment;
    const std::vector<uint8_t>*   permafrost;
    const std::vector<uint8_t>*   lakeFlag;
    const std::vector<uint8_t>*   volcanism;
    const std::vector<uint8_t>*   hazard;
    const std::vector<uint8_t>*   upwelling;
    const std::vector<uint8_t>*   climateHazard;
    const std::vector<uint8_t>*   oceanZone;
    const std::vector<float>*     cloudCover;
};

struct BiomeSubtypesOutputs {
    std::vector<uint16_t> natHazard;
    std::vector<uint8_t>  bSub;
    std::vector<uint8_t>  marineD;
    std::vector<uint8_t>  wildlife;
    std::vector<uint8_t>  disease;
    std::vector<uint8_t>  windE;
    std::vector<uint8_t>  solarE;
    std::vector<uint8_t>  hydroE;
    std::vector<uint8_t>  geoE;
    std::vector<uint8_t>  tidalE;
    std::vector<uint8_t>  waveE;
    std::vector<uint8_t>  atmExtras;
    std::vector<uint8_t>  hydExtras;
    std::vector<uint8_t>  eventMrk;
};

void runBiomeSubtypes(const HexGrid& grid, const BiomeSubtypesInputs& in,
                 BiomeSubtypesOutputs& out);

} // namespace gen
} // namespace aoc::map
