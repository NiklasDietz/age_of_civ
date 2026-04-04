#pragma once

/**
 * @file MapGenerator.hpp
 * @brief Procedural hex map generation using layered noise.
 *
 * Generates terrain, features, elevation, and rivers for a hex grid.
 * Fully deterministic given the same seed.
 */

#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Random.hpp"

#include <cstdint>

namespace aoc::map {

class MapGenerator {
public:
    struct Config {
        int32_t  width     = 80;
        int32_t  height    = 50;
        uint64_t seed      = 42;
        float    waterRatio = 0.35f;   ///< Approximate fraction of water tiles
        float    mountainRatio = 0.05f;
        float    forestRatio   = 0.20f;
        float    hillRatio     = 0.15f;
    };

    /**
     * @brief Generate a complete hex map.
     * @param config Generation parameters.
     * @param outGrid Output grid (will be initialized to config dimensions).
     */
    static void generate(const Config& config, HexGrid& outGrid);

private:
    /// Value noise sampled at (x, y) with a given frequency and PRNG.
    static float noise2D(float x, float y, float frequency, aoc::Random& rng);

    /// Multi-octave fractal noise for natural-looking terrain.
    static float fractalNoise(float x, float y, int octaves, float frequency,
                              float persistence, aoc::Random& rng);

    static void assignTerrain(const Config& config, HexGrid& grid, aoc::Random& rng);
    static void assignFeatures(const Config& config, HexGrid& grid, aoc::Random& rng);
    static void generateRivers(HexGrid& grid, aoc::Random& rng);
    static void smoothCoastlines(HexGrid& grid);
};

} // namespace aoc::map
