#pragma once

/**
 * @file Session3.hpp
 * @brief SESSION 3 -- atmospheric hazards / glacial features / ocean zones /
 *        cloud cover / drainage flow direction.
 *
 * Outputs returned via Session3Outputs so MapGenerator.cpp can keep setter
 * calls inline and preserve original setter ORDER.
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

struct Session3Outputs {
    std::vector<uint8_t> climateHazard;
    std::vector<uint8_t> glacialFeat;
    std::vector<uint8_t> oceanZone;
    std::vector<float>   cloudCover;
    std::vector<uint8_t> flowDir;
};

void runSession3(const HexGrid& grid, bool cylindrical,
                 const std::vector<float>& sediment,
                 const std::vector<uint8_t>& lakeFlag,
                 Session3Outputs& out);

} // namespace gen
} // namespace aoc::map
