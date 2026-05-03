#pragma once

/**
 * @file Mappability.hpp
 * @brief Post-SESSION-4 analysis passes: mountain passes, defensibility,
 *        domesticable species, trade-route potential, habitability,
 *        wetland subtype, coral reef placement.
 *
 * Each pass writes its own grid setter inline; these all share the same
 * (HexGrid, cylindrical, plus a few input vectors) signature so they live
 * together. Behaviour-preserving extraction from src/map/MapGenerator.cpp
 * on 2026-05-03.
 */

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

void runMountainPass(HexGrid& grid, bool cylindrical);
void runDefensibility(HexGrid& grid, bool cylindrical);
void runDomesticable(HexGrid& grid);
void runTradeRoutePotential(HexGrid& grid,
                            const std::vector<uint8_t>& marineDepth);
void runHabitability(HexGrid& grid, bool cylindrical,
                     const std::vector<float>& soilFert,
                     const std::vector<uint16_t>& natHazard,
                     const std::vector<uint8_t>& disease,
                     const std::vector<uint8_t>& permafrost);
void runWetlandSubtype(HexGrid& grid);
void runCoralReef(HexGrid& grid,
                  const std::vector<uint8_t>& bSub);

} // namespace gen
} // namespace aoc::map
