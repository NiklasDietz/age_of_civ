#pragma once

/**
 * @file MapGenContext.hpp
 * @brief Shared mutable state passed pass-to-pass during MapGenerator::
 *        assignTerrain. Lets extracted passes work on the same intermediate
 *        arrays that used to be local variables in assignTerrain.
 *
 * The context owns reference-style pointers (HexGrid&, vectors&) -- it
 * does NOT manage lifetime; assignTerrain holds the underlying buffers on
 * its own stack and constructs a Context wrapping them before calling
 * extracted passes.
 *
 * Created on 2026-05-03 as part of Phase 5 modularization.
 */

#include "aoc/map/gen/Plate.hpp"

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

struct MapGenContext {
    // Grid + topology.
    HexGrid* grid          = nullptr;
    int32_t  width         = 0;
    int32_t  height        = 0;
    bool     cylindrical   = false;

    // Plates + post-sim seam record.
    std::vector<Plate>*      plates       = nullptr;
    std::vector<SutureSeam>* sutureSeams  = nullptr;

    // Per-tile intermediate arrays populated during the tectonic sim and
    // consumed by post-sim / elevation / EARTH-SYSTEM passes.
    std::vector<float>*    elevationMap   = nullptr;
    std::vector<float>*    orogeny        = nullptr;
    std::vector<float>*    sediment       = nullptr;
    std::vector<float>*    crustAgeTile   = nullptr;
    std::vector<uint8_t>*  rockTypeTile   = nullptr;
    std::vector<uint8_t>*  marginTypeTile = nullptr;
    std::vector<uint8_t>*  ophioliteMask  = nullptr;
};

} // namespace gen
} // namespace aoc::map
