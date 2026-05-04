#pragma once

/**
 * @file Session10.hpp
 * @brief SESSION 10 -- lithology / soil order / crustal thickness /
 *        geothermal gradient / albedo / vegetation type / atmos river /
 *        cyclone basin / SST / ice shelf / bedrock / permafrost depth.
 *
 * The compute body fills caller-supplied output vectors. Setter calls remain
 * in MapGenerator.cpp so the original setter ORDER is preserved exactly --
 * downstream sessions read these via grid.set* getters and any reordering
 * could change which getter sees populated vs empty data.
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

/// Compute SESSION 10 outputs. `permafrost` is read-only input from the
/// EARTH-SYSTEM POST-PASSES preamble.
struct LithologyOutputs {
    std::vector<uint8_t> litho;
    std::vector<uint8_t> bedrock;
    std::vector<uint8_t> sOrder;
    std::vector<uint8_t> crustTh;
    std::vector<uint8_t> geoGrad;
    std::vector<uint8_t> albedo;
    std::vector<uint8_t> vegType;
    std::vector<uint8_t> atmRiv;
    std::vector<uint8_t> cycBasin;
    std::vector<uint8_t> sst;
    std::vector<uint8_t> iceShelf;
    std::vector<uint8_t> permaD;
};

void runLithology(const HexGrid& grid, bool cylindrical,
                  const std::vector<uint8_t>& permafrost,
                  LithologyOutputs& out);

} // namespace gen
} // namespace aoc::map
