#pragma once

/**
 * @file PostSim.hpp
 * @brief POST-SIM GEOLOGICAL PASSES -- runs after the tectonic-plate
 *        simulation finishes. Six sub-passes:
 *          1. Per-tile crust age (interior = old, edges = young)
 *          2. Suture / ophiolite marking from recorded merge seams
 *          3. Sediment yield + downhill deposition (alluvial plains)
 *          4. Foreland basin flexural loading next to mountain belts
 *          5. Active vs passive margin classification
 *          6. Apply sediment + ophiolite uplift onto elevationMap
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

namespace aoc::map::gen {

struct MapGenContext;

void runPostSimPasses(MapGenContext& ctx);

} // namespace aoc::map::gen
