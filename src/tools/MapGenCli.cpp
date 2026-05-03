/**
 * @file MapGenCli.cpp
 * @brief Standalone map generator CLI -- runs the Continents tectonic-plate
 *        pipeline and dumps the result to ASCII / CSV files for offline review.
 *
 * Usage:
 *   aoc_mapgen --seed N --width W --height H --output PATH [--format ascii|csv|both]
 *
 * The intent is to iterate on map generation without launching the full
 * simulation. ASCII output uses one character per terrain type so a generated
 * world fits in a terminal scroll. CSV output reuses the same per-tile schema
 * as `simulation_log_tiles.csv` so existing analysis scripts keep working.
 */

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/MapGenerator.hpp"
#include "aoc/map/Terrain.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

enum class OutputFormat : uint8_t {
    Ascii = 0,
    Csv   = 1,
    Both  = 2,
};

[[nodiscard]] OutputFormat parseFormat(std::string_view s) {
    if (s == "csv")   { return OutputFormat::Csv; }
    if (s == "both")  { return OutputFormat::Both; }
    return OutputFormat::Ascii;
}

/// One char per terrain. Hills feature replaces base-terrain glyph for visual
/// readability; rivers overlay as `~`. Choices below are intentionally unique
/// per terrain so a printed map is unambiguous to a reader who knows the key.
[[nodiscard]] char terrainGlyph(aoc::map::TerrainType t, aoc::map::FeatureType f,
                                bool hasRiver) {
    if (hasRiver && !aoc::map::isWater(t)) { return '~'; }
    if (f == aoc::map::FeatureType::Hills && !aoc::map::isWater(t)) { return 'm'; }
    if (f == aoc::map::FeatureType::Forest)      { return 'f'; }
    if (f == aoc::map::FeatureType::Jungle)      { return 'j'; }
    if (f == aoc::map::FeatureType::Marsh)       { return 'M'; }
    if (f == aoc::map::FeatureType::Floodplains) { return 'P'; }
    if (f == aoc::map::FeatureType::Oasis)       { return 'O'; }
    if (f == aoc::map::FeatureType::Reef)        { return 'r'; }
    if (f == aoc::map::FeatureType::Ice)         { return 'I'; }
    switch (t) {
        case aoc::map::TerrainType::Ocean:        return ':';
        case aoc::map::TerrainType::Coast:        return ',';
        case aoc::map::TerrainType::ShallowWater: return '.';
        case aoc::map::TerrainType::Desert:       return 'D';
        case aoc::map::TerrainType::Plains:       return '-';
        case aoc::map::TerrainType::Grassland:    return 'g';
        case aoc::map::TerrainType::Tundra:       return 'T';
        case aoc::map::TerrainType::Snow:         return '*';
        case aoc::map::TerrainType::Mountain:     return '^';
        default:                                  return '?';
    }
}

void writeAscii(const aoc::map::HexGrid& grid, const std::string& path) {
    std::ofstream out(path);
    if (!out.is_open()) {
        std::fprintf(stderr, "error: cannot open '%s' for writing\n", path.c_str());
        return;
    }
    out << "# aoc_mapgen ASCII map\n";
    out << "# Legend: : Ocean | , Coast | . ShallowWater | D Desert | - Plains\n";
    out << "#         g Grassland | T Tundra | * Snow | ^ Mountain | m Hills\n";
    out << "#         f Forest | j Jungle | M Marsh | P Floodplains | O Oasis\n";
    out << "#         r Reef | I Ice | ~ River\n";
    out << "# Width=" << grid.width() << " Height=" << grid.height() << "\n";

    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    for (int32_t row = 0; row < height; ++row) {
        // Offset every other row by one space so the hex layout stays
        // visually distinct in a fixed-width terminal.
        if ((row & 1) == 1) { out << ' '; }
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            const aoc::map::TerrainType t = grid.terrain(idx);
            const aoc::map::FeatureType f = grid.feature(idx);
            const bool river = grid.riverEdges(idx) != 0;
            out << terrainGlyph(t, f, river) << ' ';
        }
        out << '\n';
    }
}

void writeCsv(const aoc::map::HexGrid& grid, const std::string& path) {
    std::ofstream out(path);
    if (!out.is_open()) {
        std::fprintf(stderr, "error: cannot open '%s' for writing\n", path.c_str());
        return;
    }
    out << "Index,Col,Row,Terrain,Feature,Improvement,Owner,RiverEdgeMask\n";

    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            out << idx << ',' << col << ',' << row << ','
                << aoc::map::terrainName(grid.terrain(idx)) << ','
                << aoc::map::featureName(grid.feature(idx)) << ','
                << static_cast<int>(grid.improvement(idx)) << ','
                << static_cast<int>(grid.owner(idx)) << ','
                << static_cast<int>(grid.riverEdges(idx)) << '\n';
        }
    }
}

void usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [--seed N] [--width W] [--height H] [--output PATH]\n"
        "          [--format ascii|csv|both] [--epochs N] [--super-sample N]\n"
        "\n"
        "Generates a single Continents map and writes it to disk for review.\n"
        "Defaults: --seed 42 --width 140 --height 90 --output /tmp/map\n"
        "          --format ascii\n",
        prog);
}

} // namespace

int main(int argc, char* argv[]) {
    // Force single-threaded OpenMP. The map generator's OMP-parallel sections
    // race-corrupt heap allocations when multiple workers run concurrently,
    // and (more subtly) produce non-deterministic output even with a fixed
    // seed because static-schedule chunks are claimed in non-deterministic
    // order. setenv affects child processes but not the in-process OpenMP
    // runtime (which read OMP_NUM_THREADS at library init). Use the runtime
    // API to set the limit AFTER OMP is loaded.
    setenv("OMP_NUM_THREADS", "1", 1);
#ifdef _OPENMP
    omp_set_num_threads(1);
#endif

    aoc::map::MapGenerator::Config config{};
    config.mapType = aoc::map::MapType::Continents;
    config.width   = 140;
    config.height  = 90;
    config.seed    = 42;

    std::string outputBase = "/tmp/map";
    OutputFormat format    = OutputFormat::Ascii;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        }
        if (arg == "--seed" && i + 1 < argc) {
            config.seed = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--width" && i + 1 < argc) {
            config.width = std::atoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            config.height = std::atoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            outputBase = argv[++i];
        } else if (arg == "--format" && i + 1 < argc) {
            format = parseFormat(argv[++i]);
        } else if (arg == "--epochs" && i + 1 < argc) {
            config.tectonicEpochs = std::atoi(argv[++i]);
        } else if (arg == "--super-sample" && i + 1 < argc) {
            config.superSampleFactor = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "error: unknown argument '%s'\n", arg.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    if (config.width <= 0 || config.height <= 0) {
        std::fprintf(stderr, "error: width and height must be positive\n");
        return 2;
    }

    aoc::map::HexGrid grid;
    aoc::map::MapGenerator::generate(config, grid);

    if (format == OutputFormat::Ascii || format == OutputFormat::Both) {
        const std::string path = outputBase + ".txt";
        writeAscii(grid, path);
        std::printf("wrote %s (%dx%d ASCII map)\n", path.c_str(), grid.width(), grid.height());
    }
    if (format == OutputFormat::Csv || format == OutputFormat::Both) {
        const std::string path = outputBase + ".csv";
        writeCsv(grid, path);
        std::printf("wrote %s (per-tile CSV)\n", path.c_str());
    }
    return 0;
}
