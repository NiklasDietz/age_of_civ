#pragma once

/**
 * @file Biogeography.hpp
 * @brief Biogeographic realms / land bridges / refugia / metamorphic
 *        core complex passes.
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

void runBiogeographicRealms(HexGrid& grid);
void runLandBridges(HexGrid& grid, bool cylindrical,
                    const std::vector<uint8_t>& lakeFlag);
/// Refugia mutates `soilFert` in-place (microclimate boost) before writing
/// the refugium flag to the grid.
void runRefugia(HexGrid& grid, bool cylindrical,
                std::vector<float>& soilFert);
void runMetamorphicCoreComplex(HexGrid& grid, bool cylindrical);

} // namespace gen
} // namespace aoc::map
