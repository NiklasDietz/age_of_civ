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
#include <utility>

namespace aoc::map {

/// Map generation type controlling landmass shape.
enum class MapType : uint8_t {
    Continents,   ///< 2-3 separate land masses with distinct centers.
    Pangaea,      ///< Single central landmass with strong center gradient.
    Archipelago,  ///< Many small islands with weak gradient and high water.
    Fractal,      ///< Pure noise, no gradient falloff.
    Realistic,    ///< Tectonic plate simulation with geology-based resource placement.
};

/// Predefined map sizes.
enum class MapSize : uint8_t {
    Small,     ///< 60x40
    Standard,  ///< 80x50
    Large,     ///< 100x66
};

/// Get dimensions for a given MapSize preset.
[[nodiscard]] constexpr std::pair<int32_t, int32_t> mapSizeDimensions(MapSize size) {
    switch (size) {
        case MapSize::Small:    return {60, 40};
        case MapSize::Standard: return {80, 50};
        case MapSize::Large:    return {100, 66};
        default:                return {80, 50};
    }
}

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
        MapType  mapType   = MapType::Continents;  ///< Landmass generation style
        MapSize  mapSize   = MapSize::Standard;     ///< Preset size (overrides width/height when applied)
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
    static void placeNaturalWonders(HexGrid& grid, aoc::Random& rng);

    /// Generate terrain using tectonic plate simulation (Realistic map type).
    static void generateRealisticTerrain(const Config& config, HexGrid& grid, aoc::Random& rng);

    /// Place resources based on geology zones (Realistic map type).
    static void placeGeologyResources(const Config& config, HexGrid& grid, aoc::Random& rng);

    /// Place resources using simple terrain-based rules (non-Realistic map types).
    static void placeBasicResources(const Config& config, HexGrid& grid, aoc::Random& rng);
};

} // namespace aoc::map
