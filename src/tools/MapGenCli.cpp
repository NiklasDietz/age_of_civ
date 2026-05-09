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
#include "aoc/map/gen/SphereGeometry.hpp"

#include <cmath>
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

/// Glyph that encodes both plate ownership (lower-cased letter cycling A-Z
/// per plate id) and water/land/mountain status (uppercase = land, lowercase
/// = ocean, '^' = mountain). Renders the tectonic-sim plate distribution
/// alongside the resulting terrain so the viewer can correlate plate
/// boundaries with mountain ranges and ocean lanes.
[[nodiscard]] char plateGlyph(uint8_t plateId, aoc::map::TerrainType t,
                              bool isMountain) {
    if (isMountain) { return '^'; }
    if (plateId == 0xFFu) { return '?'; }
    const char base = static_cast<char>('A' + (plateId % 26));
    if (aoc::map::isWater(t)) {
        return static_cast<char>(base + ('a' - 'A'));
    }
    return base;
}

void writeFrame(const aoc::map::HexGrid& grid, const std::string& path,
                int32_t epochK, int32_t epochsTotal) {
    std::ofstream out(path);
    if (!out.is_open()) {
        std::fprintf(stderr, "error: cannot open '%s' for writing\n", path.c_str());
        return;
    }
    out << "# Frame epoch=" << epochK << "/" << epochsTotal
        << "  Width=" << grid.width() << " Height=" << grid.height() << "\n";
    out << "# Uppercase = land, lowercase = ocean, ^ = mountain;"
        << " letter = plate id mod 26\n";
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    for (int32_t row = 0; row < height; ++row) {
        if ((row & 1) == 1) { out << ' '; }
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            const aoc::map::TerrainType t = grid.terrain(idx);
            const bool isMtn = (t == aoc::map::TerrainType::Mountain);
            const uint8_t pid = grid.plateId(idx);
            out << plateGlyph(pid, t, isMtn);
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
        "          [--format ascii|csv|both]\n"
        "          [--tectonic-time-my N | --tectonic-time-gy N | --epochs N]\n"
        "          [--projection mollweide|equirect|mercator|robinson]\n"
        "          [--super-sample N]\n"
        "          [--dump-plates PATH] [--dump-edges PATH]\n"
        "          [--dump-mountain-edges PATH]\n"
        "\n"
        "Generates a single Continents map and writes it to disk for review.\n"
        "Defaults: --seed 42 --width 140 --height 90 --output /tmp/map\n"
        "          --format ascii\n"
        "\n"
        "Diagnostic flags (Phase 13d-A1):\n"
        "  --dump-plates PATH   write per-plate CSV (sphere position, motion,\n"
        "                       Euler pole, polygon size) to PATH for offline\n"
        "                       analysis by tools/diagnose_plate_shapes.py.\n"
        "  --dump-edges PATH    write per-boundary-edge CSV in world frame\n"
        "                       (rotated polygon vertices) with edge_type and\n"
        "                       neighbor_plate_id, for diagnose_mountain_edges.py.\n"
        "  --dump-mountain-edges PATH  write per-mountain-tile CSV with the\n"
        "                       nearest boundary edge (owner, type, distance_km)\n"
        "                       for diagnose_mountain_edges.py.\n",
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

    std::string outputBase     = "/tmp/map";
    std::string dumpPlatesPath;
    std::string dumpEdgesPath;
    std::string dumpMountainEdgesPath;
    OutputFormat format    = OutputFormat::Ascii;
    bool         frameMode = false;

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
            // Legacy direct epoch override.
            config.tectonicEpochs = std::atoi(argv[++i]);
        } else if (arg == "--tectonic-time-my" && i + 1 < argc) {
            // Total simulated geological time in millions of years.
            config.tectonicTotalMy = std::atoi(argv[++i]);
        } else if (arg == "--tectonic-time-gy" && i + 1 < argc) {
            // Convenience: same as --tectonic-time-my but in Gy.
            const float gy = static_cast<float>(std::atof(argv[++i]));
            config.tectonicTotalMy = static_cast<int32_t>(gy * 1000.0f + 0.5f);
        } else if (arg == "--projection" && i + 1 < argc) {
            // Sphere → rectangle projection: mollweide, equirect,
            // mercator, or robinson. Defaults to mollweide.
            const std::string p = argv[++i];
            if      (p == "mollweide") config.projection = aoc::map::gen::MapProjection::Mollweide;
            else if (p == "equirect")  config.projection = aoc::map::gen::MapProjection::Equirectangular;
            else if (p == "mercator")  config.projection = aoc::map::gen::MapProjection::Mercator;
            else if (p == "robinson")  config.projection = aoc::map::gen::MapProjection::Robinson;
            else {
                std::fprintf(stderr, "error: unknown --projection '%s' "
                    "(expected mollweide|equirect|mercator|robinson)\n",
                    p.c_str());
                return 2;
            }
        } else if (arg == "--super-sample" && i + 1 < argc) {
            config.superSampleFactor = std::atoi(argv[++i]);
        } else if (arg == "--cylindrical") {
            config.topology = aoc::map::MapTopology::Cylindrical;
        } else if (arg == "--frames") {
            frameMode = true;
        } else if (arg == "--dump-plates" && i + 1 < argc) {
            dumpPlatesPath = argv[++i];
        } else if (arg == "--dump-edges" && i + 1 < argc) {
            dumpEdgesPath = argv[++i];
        } else if (arg == "--dump-mountain-edges" && i + 1 < argc) {
            dumpMountainEdgesPath = argv[++i];
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

    if (frameMode) {
        // Frame mode: re-run generate() once per epoch K=1..EPOCHS with
        // runEpochsLimit=K so each invocation halts the tectonic sim mid-
        // flight at epoch K. Determinism on a fixed seed makes the
        // sequence coherent: frame K+1 = frame K state plus one more sim
        // step. Writes both per-frame plate-glyph files and a
        // concatenated multiframe.txt suitable for `cat` playback with
        // ANSI clear escapes between frames.
        // Resolve epoch count from either explicit override or total time.
        int32_t requestedEpochs;
        if (config.tectonicEpochs > 0) {
            requestedEpochs = config.tectonicEpochs;
        } else {
            const int32_t totalMy = (config.tectonicTotalMy > 0)
                ? config.tectonicTotalMy
                : aoc::map::MapGenerator::DEFAULT_TECTONIC_TOTAL_MY;
            requestedEpochs = std::max(3, (totalMy
                + aoc::map::MapGenerator::MY_PER_EPOCH_TARGET / 2)
                / aoc::map::MapGenerator::MY_PER_EPOCH_TARGET);
        }
        const std::string multiPath = outputBase + ".frames.txt";
        std::ofstream multi(multiPath);
        if (!multi.is_open()) {
            std::fprintf(stderr, "error: cannot open '%s' for writing\n",
                         multiPath.c_str());
            return 1;
        }
        for (int32_t k = 1; k <= requestedEpochs; ++k) {
            aoc::map::MapGenerator::Config frameConfig = config;
            frameConfig.tectonicEpochs = requestedEpochs;
            frameConfig.runEpochsLimit = k;
            aoc::map::HexGrid frameGrid;
            aoc::map::MapGenerator::generate(frameConfig, frameGrid);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s.frame%03d.txt",
                          outputBase.c_str(), k);
            writeFrame(frameGrid, buf, k, requestedEpochs);
            // ANSI clear screen + cursor home, then frame.
            multi << "\x1b[2J\x1b[H";
            multi << "# Frame " << k << "/" << requestedEpochs
                  << "  (seed=" << config.seed
                  << " size=" << config.width << "x" << config.height << ")\n";
            const int32_t width  = frameGrid.width();
            const int32_t height = frameGrid.height();
            for (int32_t row = 0; row < height; ++row) {
                if ((row & 1) == 1) { multi << ' '; }
                for (int32_t col = 0; col < width; ++col) {
                    const int32_t idx = row * width + col;
                    const aoc::map::TerrainType t = frameGrid.terrain(idx);
                    const bool isMtn = (t == aoc::map::TerrainType::Mountain);
                    const uint8_t pid = frameGrid.plateId(idx);
                    multi << plateGlyph(pid, t, isMtn);
                }
                multi << '\n';
            }
            std::printf("frame %d/%d -> %s\n", k, requestedEpochs, buf);
        }
        std::printf("wrote %s (animated playback: cat %s)\n",
                    multiPath.c_str(), multiPath.c_str());
        return 0;
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
    // 2026-05-06 cleanup: --dump-plates / --dump-edges /
    // --dump-plates: per-plate diagnostic CSV. Writes one row per
    // plate id present on the final HexGrid: cell count, land
    // fraction, bounding box (min/max col/row), centroid screen
    // coords, and contiguity (number of disconnected components).
    // Use case: spot lat-banded layouts, fragmented plates, pure-
    // ocean dominators. Pipe to a column viewer to spot-check.
    if (!dumpPlatesPath.empty()) {
        std::ofstream pf(dumpPlatesPath);
        if (pf.is_open()) {
            pf << "plate_id,cell_count,land_frac,min_col,max_col,"
                  "min_row,max_row,centroid_col,centroid_row\n";
            const int32_t W = grid.width();
            const int32_t H = grid.height();
            std::array<int64_t, 256> cellCount{};
            std::array<int64_t, 256> landCount{};
            std::array<int64_t, 256> sumCol{};
            std::array<int64_t, 256> sumRow{};
            std::array<int32_t, 256> minCol{};
            std::array<int32_t, 256> maxCol{};
            std::array<int32_t, 256> minRow{};
            std::array<int32_t, 256> maxRow{};
            for (int32_t i = 0; i < 256; ++i) {
                minCol[i] = W; maxCol[i] = -1;
                minRow[i] = H; maxRow[i] = -1;
            }
            for (int32_t row = 0; row < H; ++row) {
                for (int32_t col = 0; col < W; ++col) {
                    const int32_t idx = row * W + col;
                    const uint8_t pid = grid.plateId(idx);
                    if (pid == 0xFFu) continue;
                    ++cellCount[pid];
                    if (!aoc::map::isWater(grid.terrain(idx))) {
                        ++landCount[pid];
                    }
                    sumCol[pid] += col;
                    sumRow[pid] += row;
                    if (col < minCol[pid]) minCol[pid] = col;
                    if (col > maxCol[pid]) maxCol[pid] = col;
                    if (row < minRow[pid]) minRow[pid] = row;
                    if (row > maxRow[pid]) maxRow[pid] = row;
                }
            }
            for (int32_t pid = 0; pid < 256; ++pid) {
                if (cellCount[pid] == 0) continue;
                const float lf = static_cast<float>(landCount[pid])
                               / static_cast<float>(cellCount[pid]);
                const float ccol = static_cast<float>(sumCol[pid])
                                 / static_cast<float>(cellCount[pid]);
                const float crow = static_cast<float>(sumRow[pid])
                                 / static_cast<float>(cellCount[pid]);
                pf << pid << ',' << cellCount[pid] << ',' << lf << ','
                   << minCol[pid] << ',' << maxCol[pid] << ','
                   << minRow[pid] << ',' << maxRow[pid] << ','
                   << ccol << ',' << crow << '\n';
            }
            std::printf("wrote %s (per-plate stats)\n",
                        dumpPlatesPath.c_str());
        }
    }
    (void)dumpEdgesPath;
    (void)dumpMountainEdgesPath;
    return 0;
}
