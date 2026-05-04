#pragma once

/**
 * @file Session9.hpp
 * @brief SESSION 9 -- Köppen / mountain structure / ore grade / strait /
 *        harbor / channel pattern / vegetation density / coastal feature /
 *        submarine vent / volcanic profile / karst subtype / desert subtype /
 *        mass wasting / named winds / forest age / soil moisture.
 *
 * Outputs returned via KoppenStructuresOutputs struct so MapGenerator.cpp can keep
 * the setter calls inline at the original location (preserves setter ORDER).
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

struct KoppenStructuresOutputs {
    std::vector<uint8_t> kop;
    std::vector<uint8_t> mtnS;
    std::vector<uint8_t> oreG;
    std::vector<uint8_t> strait;
    std::vector<uint8_t> harbor;
    std::vector<uint8_t> chanP;
    std::vector<uint8_t> vegD;
    std::vector<uint8_t> coast;
    std::vector<uint8_t> subV;
    std::vector<uint8_t> volP;
    std::vector<uint8_t> karstS;
    std::vector<uint8_t> desS;
    std::vector<uint8_t> massW;
    std::vector<uint8_t> namedW;
    std::vector<uint8_t> forA;
    std::vector<uint8_t> soilM;
};

void runKoppenStructures(const HexGrid& grid, bool cylindrical,
                 const std::vector<float>& orogeny,
                 const std::vector<uint8_t>& lakeFlag,
                 const std::vector<float>& sediment,
                 const std::vector<uint8_t>& eventMrk,
                 KoppenStructuresOutputs& out);

} // namespace gen
} // namespace aoc::map
