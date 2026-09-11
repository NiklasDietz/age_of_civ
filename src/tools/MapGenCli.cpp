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

#include "aoc/debug/DebugServer.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/MapGenerator.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/map/StartPlacement.hpp"
#include "aoc/map/LandmassMetrics.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/SphereGeometry.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

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
    if (s == "csv") {
        return OutputFormat::Csv;
    }
    if (s == "both") {
        return OutputFormat::Both;
    }
    return OutputFormat::Ascii;
}

/// One char per terrain. Hills feature replaces base-terrain glyph for visual
/// readability; rivers overlay as `~`. Choices below are intentionally unique
/// per terrain so a printed map is unambiguous to a reader who knows the key.
[[nodiscard]] char terrainGlyph(aoc::map::TerrainType t, aoc::map::FeatureType f, bool hasRiver) {
    if (hasRiver && !aoc::map::isWater(t)) {
        return '~';
    }
    if (f == aoc::map::FeatureType::Hills && !aoc::map::isWater(t)) {
        return 'm';
    }
    if (f == aoc::map::FeatureType::Forest) {
        return 'f';
    }
    if (f == aoc::map::FeatureType::Jungle) {
        return 'j';
    }
    if (f == aoc::map::FeatureType::Marsh) {
        return 'M';
    }
    if (f == aoc::map::FeatureType::Floodplains) {
        return 'P';
    }
    if (f == aoc::map::FeatureType::Oasis) {
        return 'O';
    }
    if (f == aoc::map::FeatureType::Reef) {
        return 'r';
    }
    if (f == aoc::map::FeatureType::Ice) {
        return 'I';
    }
    switch (t) {
    case aoc::map::TerrainType::Ocean:
        return ':';
    case aoc::map::TerrainType::Coast:
        return ',';
    case aoc::map::TerrainType::ShallowWater:
        return '.';
    case aoc::map::TerrainType::Desert:
        return 'D';
    case aoc::map::TerrainType::Plains:
        return '-';
    case aoc::map::TerrainType::Grassland:
        return 'g';
    case aoc::map::TerrainType::Tundra:
        return 'T';
    case aoc::map::TerrainType::Snow:
        return '*';
    case aoc::map::TerrainType::Mountain:
        return '^';
    default:
        return '?';
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
        if ((row & 1) == 1) {
            out << ' ';
        }
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx             = row * width + col;
            const aoc::map::TerrainType t = grid.terrain(idx);
            const aoc::map::FeatureType f = grid.feature(idx);
            const bool river              = grid.riverEdges(idx) != 0;
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
[[nodiscard]] char plateGlyph(uint8_t plateId, aoc::map::TerrainType t, bool isMountain) {
    if (isMountain) {
        return '^';
    }
    if (plateId == 0xFFu) {
        return '?';
    }
    const char base = static_cast<char>('A' + (plateId % 26));
    if (aoc::map::isWater(t)) {
        return static_cast<char>(base + ('a' - 'A'));
    }
    return base;
}

void writeFrame(const aoc::map::HexGrid& grid, const std::string& path, int32_t epochK,
                int32_t epochsTotal) {
    std::ofstream out(path);
    if (!out.is_open()) {
        std::fprintf(stderr, "error: cannot open '%s' for writing\n", path.c_str());
        return;
    }
    out << "# Frame epoch=" << epochK << "/" << epochsTotal << "  Width=" << grid.width()
        << " Height=" << grid.height() << "\n";
    out << "# Uppercase = land, lowercase = ocean, ^ = mountain;"
        << " letter = plate id mod 26\n";
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    for (int32_t row = 0; row < height; ++row) {
        if ((row & 1) == 1) {
            out << ' ';
        }
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx             = row * width + col;
            const aoc::map::TerrainType t = grid.terrain(idx);
            const bool isMtn              = (t == aoc::map::TerrainType::Mountain);
            const uint8_t pid             = grid.plateId(idx);
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
    // PlateId is carried so offline tooling can correlate coastline geometry
    // with plate geometry. That correlation is the whole question behind the
    // straight-coastline artifact -- `--dump-plates` only reports per-plate
    // aggregates (bbox, centroid), which cannot show WHERE a boundary runs.
    // 255 is the unowned sentinel (HexGrid stores plate id in a uint8).
    out << "Index,Col,Row,Terrain,Feature,Improvement,Owner,RiverEdgeMask,PlateId,Resource\n";

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
                << static_cast<int>(grid.riverEdges(idx)) << ','
                << static_cast<int>(grid.plateId(idx)) << ','
                << (grid.resource(idx).isValid() ? static_cast<int>(grid.resource(idx).value) : -1)
                << '\n';
        }
    }
}

/// Clamp a caller-supplied grid dimension. width*height sizes ~50 per-tile
/// layers plus the worldgen intermediates, so an unvalidated value is an
/// allocation-size hazard, not just a bad map.
int32_t clampDimension(const char* flag, int32_t value) {
    constexpr int32_t MIN_DIMENSION = 8;
    if (value < MIN_DIMENSION || value > aoc::map::HexGrid::MAX_MAP_DIMENSION) {
        const int32_t clamped =
            std::clamp(value, MIN_DIMENSION, aoc::map::HexGrid::MAX_MAP_DIMENSION);
        std::fprintf(stderr, "warning: %s %d out of range [%d, %d]; using %d\n", flag, value,
                     MIN_DIMENSION, aoc::map::HexGrid::MAX_MAP_DIMENSION, clamped);
        return clamped;
    }
    return value;
}

void usage(const char* prog) {
    std::fprintf(stderr,
                 "Usage: %s [--seed N] [--width W] [--height H] [--output PATH]\n"
                 "          [--format ascii|csv|both]\n"
                 "          [--tectonic-time-my N | --tectonic-time-gy N | --epochs N]\n"
                 "          [--projection lambert|mollweide|equirect|mercator|robinson]\n"
                 "          [--flat] [--frames] [--stop-epoch N] [--dump-plates PATH]\n"
                 "          [--serve-http [--port N]] [--players N[,N...]]\n"
                 "          [--placement realistic|fair|random]\n"
                 "\n"
                 "Generates a single Continents map and writes it to disk for review.\n"
                 "Defaults: --seed 42 --width 140 --height 90 --output /tmp/map\n"
                 "          --format ascii  --topology cylindrical\n"
                 "          --projection lambert (equal-area, so a tile count is\n"
                 "          proportional to planet area and land%% is directly\n"
                 "          comparable to Earth's 29.2%% with no weighting)\n"
                 "\n"
                 "  --flat               generate a non-wrapping grid. The default is\n"
                 "                       Cylindrical because that is what the game\n"
                 "                       ships; only pass this if you specifically want\n"
                 "                       to test the non-wrapping case.\n"
                 "\n"
                 "Diagnostic flags:\n"
                 "  --dump-plates PATH   write per-plate CSV (cell count, land frac,\n"
                 "                       bbox, centroid, connected-component count) to\n"
                 "                       PATH. Consumed by tools/run_diagnostic_matrix.sh.\n"
                 "  --frames             re-run once per epoch and write per-epoch\n"
                 "                       plate-glyph maps plus a concatenated animation.\n"
                 "  --stop-epoch N       halt the tectonic sim after N epochs. Use to\n"
                 "                       inspect an early state in one run instead of\n"
                 "                       the O(n^2) --frames sweep. Elevation-derived\n"
                 "                       layers are not comparable across different N\n"
                 "                       (sea level is re-solved each epoch); plate\n"
                 "                       ownership and continental fraction are.\n"
                 "  --serve-http         HTTP inspection server on 127.0.0.1:<port>.\n"
                 "  --players N[,N...]   for each player count, choose starts as the game\n"
                 "                       does and print a [resgeo] line on stderr: the\n"
                 "                       resource geography within 9 tiles of each start\n"
                 "                       (A absent-luxury share, B every luxury on map,\n"
                 "                       C fewest luxury types, D1 copper or iron at every\n"
                 "                       start, D2 horses share, E complementary pairs).\n"
                 "\n"
                 "Trace env vars: AOC_SPHEREPHYS_TRACE, AOC_ADVECT_TRACE,\n"
                 "                AOC_DUMP_THRESHOLD, AOC_DUMP_MARGINS.\n",
                 prog);
}

} // namespace

int main(int argc, char* argv[]) {
    // Force single-threaded OpenMP so this tool's output is reproducible
    // regardless of the host's core count.
    //
    // 2026-07-27: this comment used to assert that the generator's OMP
    // sections "race-corrupt heap allocations" and produce non-deterministic
    // output at any thread count. Neither reproduces now: aoc_simulate at
    // OMP_NUM_THREADS=16 and =1 produce bit-identical tile dumps, and three
    // identical aoc_mapgen runs hash identically. But one A/B comparison does
    // not refute a data race, and there IS a real thread-count sensitivity
    // still in the pipeline -- PostSim.cpp's sediment accumulation sums
    // per-thread buffers in buffer order, so its float total depends on
    // omp_get_max_threads(). The pin therefore stays as cheap insurance until
    // a thread-sweep determinism test covers the whole generator.
    //
    // setenv affects child processes but not the in-process OpenMP runtime
    // (which reads OMP_NUM_THREADS at library init), so set the limit through
    // the runtime API as well, AFTER OMP is loaded.
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
    std::string dumpPlatesPath;
    OutputFormat format = OutputFormat::Ascii;
    bool frameMode      = false;
    bool serveHttp      = false;
    std::vector<int32_t> playerCounts;
    int32_t httpPort    = 9876;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        }
        if (arg == "--seed" && i + 1 < argc) {
            config.seed = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--width" && i + 1 < argc) {
            config.width = clampDimension("--width", std::atoi(argv[++i]));
        } else if (arg == "--height" && i + 1 < argc) {
            config.height = clampDimension("--height", std::atoi(argv[++i]));
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
            const float gy         = static_cast<float>(std::atof(argv[++i]));
            config.tectonicTotalMy = static_cast<int32_t>(gy * 1000.0f + 0.5f);
        } else if (arg == "--projection" && i + 1 < argc) {
            // Sphere → rectangle projection: mollweide, equirect,
            // mercator, or robinson. Defaults to mollweide.
            const std::string p = argv[++i];
            if (p == "lambert")
                config.projection = aoc::map::gen::MapProjection::LambertCylindricalEqualArea;
            else if (p == "mollweide")
                config.projection = aoc::map::gen::MapProjection::Mollweide;
            else if (p == "equirect")
                config.projection = aoc::map::gen::MapProjection::Equirectangular;
            else if (p == "mercator")
                config.projection = aoc::map::gen::MapProjection::Mercator;
            else if (p == "robinson")
                config.projection = aoc::map::gen::MapProjection::Robinson;
            else {
                std::fprintf(stderr,
                             "error: unknown --projection '%s' (expected "
                             "lambert|mollweide|equirect|mercator|robinson)\n",
                             p.c_str());
                return 2;
            }
        } else if (arg == "--cylindrical") {
            // Now the Config default; kept so existing scripts and the
            // committed diagnostic drivers keep working.
            config.topology = aoc::map::MapTopology::Cylindrical;
        } else if (arg == "--flat") {
            config.topology = aoc::map::MapTopology::Flat;
        } else if (arg == "--stop-epoch" && i + 1 < argc) {
            // Halt the tectonic sim after N epochs and emit that state.
            // `--frames` already does this per frame, but it re-runs the
            // generator once per epoch (O(n^2) total work) to build a strip;
            // inspecting a single early state is O(1) with this.
            //
            // Note the state is genuinely mid-run, not a preview: sea level is
            // still solved against whatever hypsometry exists at epoch N, so
            // the ELEVATION-derived layers are not comparable across different
            // --stop-epoch values. Plate ownership and continental fraction are.
            config.runEpochsLimit = std::atoi(argv[++i]);
        } else if (arg == "--frames") {
            frameMode = true;
        } else if (arg == "--dump-plates" && i + 1 < argc) {
            dumpPlatesPath = argv[++i];
        } else if (arg == "--serve-http") {
            serveHttp = true;
        } else if (arg == "--placement" && i + 1 < argc) {
            const std::string mode = argv[++i];
            config.placement       = mode == "fair"     ? aoc::map::ResourcePlacementMode::Fair
                                     : mode == "random" ? aoc::map::ResourcePlacementMode::Random
                                                        : aoc::map::ResourcePlacementMode::Realistic;
        } else if (arg == "--players" && i + 1 < argc) {
            std::string list = argv[++i];
            for (std::size_t pos = 0; pos <= list.size();) {
                const std::size_t comma = list.find(',', pos);
                const std::size_t end   = comma == std::string::npos ? list.size() : comma;
                playerCounts.push_back(std::atoi(list.substr(pos, end - pos).c_str()));
                pos = end + 1;
            }
        } else if (arg == "--port" && i + 1 < argc) {
            httpPort = std::atoi(argv[++i]);
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
    for (const int32_t count : playerCounts) {
        if (count <= 0) {
            std::fprintf(stderr, "error: --players wants positive counts\n");
            return 2;
        }
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
            requestedEpochs =
                std::max(3, (totalMy + aoc::map::MapGenerator::MY_PER_EPOCH_TARGET / 2) /
                                aoc::map::MapGenerator::MY_PER_EPOCH_TARGET);
        }
        const std::string multiPath = outputBase + ".frames.txt";
        std::ofstream multi(multiPath);
        if (!multi.is_open()) {
            std::fprintf(stderr, "error: cannot open '%s' for writing\n", multiPath.c_str());
            return 1;
        }
        for (int32_t k = 1; k <= requestedEpochs; ++k) {
            aoc::map::MapGenerator::Config frameConfig = config;
            frameConfig.tectonicEpochs                 = requestedEpochs;
            frameConfig.runEpochsLimit                 = k;
            aoc::map::HexGrid frameGrid;
            aoc::map::MapGenerator::generate(frameConfig, frameGrid);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s.frame%03d.txt", outputBase.c_str(), k);
            writeFrame(frameGrid, buf, k, requestedEpochs);
            // ANSI clear screen + cursor home, then frame.
            multi << "\x1b[2J\x1b[H";
            multi << "# Frame " << k << "/" << requestedEpochs << "  (seed=" << config.seed
                  << " size=" << config.width << "x" << config.height << ")\n";
            const int32_t width  = frameGrid.width();
            const int32_t height = frameGrid.height();
            for (int32_t row = 0; row < height; ++row) {
                if ((row & 1) == 1) {
                    multi << ' ';
                }
                for (int32_t col = 0; col < width; ++col) {
                    const int32_t idx             = row * width + col;
                    const aoc::map::TerrainType t = frameGrid.terrain(idx);
                    const bool isMtn              = (t == aoc::map::TerrainType::Mountain);
                    const uint8_t pid             = frameGrid.plateId(idx);
                    multi << plateGlyph(pid, t, isMtn);
                }
                multi << '\n';
            }
            std::printf("frame %d/%d -> %s\n", k, requestedEpochs, buf);
        }
        std::printf("wrote %s (animated playback: cat %s)\n", multiPath.c_str(), multiPath.c_str());
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
    for (const int32_t count : playerCounts) {
        aoc::Random startRng(config.seed);
        const std::vector<aoc::hex::AxialCoord> starts =
            aoc::map::chooseStartPositions(grid, count, startRng);
        // The regional pass depends on the starts, so measure a copy per count.
        aoc::map::HexGrid regional = grid;
        aoc::Random regionRng(config.seed ^ 0x5245474Eu); // "REGN"
        aoc::map::MapGenerator::balanceResourcesFair(regional, starts, config.placement, regionRng);
        const aoc::map::ResourceGeography geo = aoc::map::measureResourceGeography(regional, starts);
        std::fprintf(stderr,
                     "[resgeo] players=%d starts=%zu luxuries=%d A=%.3f B=%d C=%d D1=%d "
                     "D2=%.3f E=%.3f\n",
                     count, starts.size(), geo.luxuryTypes, static_cast<double>(geo.luxuryTypesAbsent),
                     geo.everyLuxuryOnMap ? 1 : 0, geo.minLuxuryTypes,
                     geo.copperOrIronEveryStart ? 1 : 0, static_cast<double>(geo.horsesShare),
                     static_cast<double>(geo.complementaryPairs));
    }
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
                  "min_row,max_row,centroid_col,centroid_row,"
                  "component_count,largest_comp_frac,bbox_arc_cols\n";
            const int32_t W    = grid.width();
            const int32_t H    = grid.height();
            const bool cylGrid = (grid.topology() == aoc::map::MapTopology::Cylindrical);
            std::array<int64_t, 256> cellCount{};
            std::array<int64_t, 256> landCount{};
            std::array<int64_t, 256> sumCol{};
            std::array<int64_t, 256> sumRow{};
            std::array<int32_t, 256> minCol{};
            std::array<int32_t, 256> maxCol{};
            std::array<int32_t, 256> minRow{};
            std::array<int32_t, 256> maxRow{};
            for (int32_t i = 0; i < 256; ++i) {
                minCol[i] = W;
                maxCol[i] = -1;
                minRow[i] = H;
                maxRow[i] = -1;
            }
            // Per-plate column occupancy for the wrap-aware bbox arc:
            // an antimeridian-straddling plate reports a full-width
            // min/max box; the minimal covering lon-arc (width minus
            // the largest empty column gap) is the honest extent.
            std::vector<std::array<uint8_t, 256>> colUsed(static_cast<std::size_t>(W));
            for (std::array<uint8_t, 256>& a : colUsed) {
                a.fill(0u);
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
                    colUsed[static_cast<std::size_t>(col)][pid] = 1u;
                }
            }
            // Contiguity: connected components per plate over the hex
            // adjacency (east-west wrap when cylindrical).
            std::array<int32_t, 256> compCount{};
            std::array<int64_t, 256> largestComp{};
            {
                std::vector<int32_t> comp(static_cast<std::size_t>(W * H), -1);
                std::vector<int32_t> stack;
                int32_t next = 0;
                for (int32_t s = 0; s < W * H; ++s) {
                    if (comp[static_cast<std::size_t>(s)] >= 0) continue;
                    const uint8_t pid = grid.plateId(s);
                    if (pid == 0xFFu) continue;
                    comp[static_cast<std::size_t>(s)] = next;
                    int64_t size                      = 0;
                    stack.clear();
                    stack.push_back(s);
                    while (!stack.empty()) {
                        const int32_t c = stack.back();
                        stack.pop_back();
                        ++size;
                        const aoc::hex::AxialCoord ax = aoc::hex::offsetToAxial({c % W, c / W});
                        for (const aoc::hex::AxialCoord& nb : aoc::hex::neighbors(ax)) {
                            aoc::hex::OffsetCoord oc = aoc::hex::axialToOffset(nb);
                            if (cylGrid) {
                                oc.col = ((oc.col % W) + W) % W;
                            }
                            if (oc.col < 0 || oc.col >= W || oc.row < 0 || oc.row >= H) {
                                continue;
                            }
                            const int32_t ni = oc.row * W + oc.col;
                            if (comp[static_cast<std::size_t>(ni)] >= 0) {
                                continue;
                            }
                            if (grid.plateId(ni) != pid) continue;
                            comp[static_cast<std::size_t>(ni)] = next;
                            stack.push_back(ni);
                        }
                    }
                    ++compCount[pid];
                    if (size > largestComp[pid]) largestComp[pid] = size;
                    ++next;
                }
            }
            for (int32_t pid = 0; pid < 256; ++pid) {
                if (cellCount[pid] == 0) continue;
                const float lf =
                    static_cast<float>(landCount[pid]) / static_cast<float>(cellCount[pid]);
                const float ccol =
                    static_cast<float>(sumCol[pid]) / static_cast<float>(cellCount[pid]);
                const float crow =
                    static_cast<float>(sumRow[pid]) / static_cast<float>(cellCount[pid]);
                // Minimal covering column arc (wrap-aware): W minus
                // the largest circular run of unused columns. Double
                // sweep captures a gap crossing the seam; with at
                // least one used column, runs reset and never reach W.
                int32_t arcCols = maxCol[pid] - minCol[pid] + 1;
                if (cylGrid) {
                    int32_t largestGap = 0;
                    int32_t run        = 0;
                    for (int32_t k = 0; k < 2 * W; ++k) {
                        if (colUsed[static_cast<std::size_t>(k % W)][pid]) {
                            if (run > largestGap) largestGap = run;
                            run = 0;
                        } else {
                            ++run;
                        }
                    }
                    largestGap = std::min(largestGap, W - 1);
                    arcCols    = W - largestGap;
                }
                const float largestFrac =
                    static_cast<float>(largestComp[pid]) / static_cast<float>(cellCount[pid]);
                pf << pid << ',' << cellCount[pid] << ',' << lf << ',' << minCol[pid] << ','
                   << maxCol[pid] << ',' << minRow[pid] << ',' << maxRow[pid] << ',' << ccol << ','
                   << crow << ',' << compCount[pid] << ',' << largestFrac << ',' << arcCols << '\n';
            }
            std::printf("wrote %s (per-plate stats)\n", dumpPlatesPath.c_str());
        }
    }
    // ---------------------------------------------------------------
    // HTTP debug server mode. Map already generated above. The server
    // exposes a read-only inspection API plus a /sim/re-roll mutator
    // that regenerates the map with a fresh seed in-place. Server
    // runs on a worker pool; we lock the HexGrid behind a single
    // mutex shared with the regen path so reads never see a half-
    // generated grid.
    // ---------------------------------------------------------------
    if (serveHttp) {
        std::mutex gridMutex;
        aoc::map::MapGenerator::Config liveConfig = config;
        std::atomic<bool> shutdownRequested{false};

        // /quit drain barrier. Each mutating HTTP handler increments
        // `inFlight` on entry and decrements on exit (via RAII below).
        // After the main thread receives the shutdown signal AND calls
        // server.stop(), it blocks on `drainCv` until `inFlight` reaches
        // zero. cpp-httplib's `task_queue->shutdown()` already drains
        // queued tasks, but this barrier is belt-and-suspenders so the
        // stack-allocated `grid`, `liveConfig`, `currentMy` cannot go
        // out of scope while a handler still references them. C++20
        // `std::latch` is single-shot with a fixed count -- the dynamic
        // in-flight count here calls for atomic + condition_variable
        // instead. (See WP6 hint: latch preferred over shared_ptr
        // juggling, but the count is not knowable up front.)
        std::atomic<int32_t> inFlight{0};
        std::mutex drainMutex;
        std::condition_variable drainCv;

        // RAII helper: increment inFlight on entry, decrement and
        // notify on exit. Construct one at the top of every mutating
        // handler. Read-only handlers do not need it -- their state
        // accesses are guarded by gridMutex which already serialises
        // against the regen path.
        struct HandlerScope {
            std::atomic<int32_t>& counter;
            std::condition_variable& cv;
            std::mutex& mtx;

            HandlerScope(std::atomic<int32_t>& c, std::condition_variable& v,
                         std::mutex& m) noexcept
                : counter(c), cv(v), mtx(m) {
                this->counter.fetch_add(1, std::memory_order_acq_rel);
            }
            ~HandlerScope() {
                if (this->counter.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    // Last handler out: wake any drainer waiting.
                    std::lock_guard<std::mutex> lock(this->mtx);
                    this->cv.notify_all();
                }
            }
            HandlerScope(const HandlerScope&)            = delete;
            HandlerScope& operator=(const HandlerScope&) = delete;
        };
        // Active total-My used for the LAST regen. Single source of
        // truth for /sim/step, /sim/set-creator-time, /info reporting.
        // Starts at the requested run's full duration; /sim/step
        // mutates this and re-runs generate() with the new value.
        int32_t currentMy     = (liveConfig.tectonicTotalMy > 0)
                                    ? liveConfig.tectonicTotalMy
                                    : aoc::map::MapGenerator::DEFAULT_TECTONIC_TOTAL_MY;
        const int32_t totalMy = currentMy;
        // Regenerate the world at a specific total-My. Lock contract:
        // the caller owns `gridMutex` for the entire call -- the
        // `std::lock_guard&` parameter is unused at the call site but
        // makes that requirement explicit and prevents a future
        // refactor from forgetting to take the lock. Runs full sim
        // 0 -> targetMy each call (generate() is not resumable; its
        // determinism per seed makes this acceptable).
        auto regenAtMy = [&](std::lock_guard<std::mutex>& /*heldLock*/, int32_t targetMy) {
            if (targetMy < 0) targetMy = 0;
            if (targetMy > totalMy) targetMy = totalMy;
            currentMy                  = targetMy;
            liveConfig.tectonicTotalMy = std::max(1, targetMy);
            // runEpochsLimit overrides epoch derivation: use it when
            // targetMy is zero so the sim halts before the first epoch
            // and we get the pre-physics initial-cut state.
            liveConfig.runEpochsLimit = (targetMy == 0) ? 1 : 0;
            grid                      = aoc::map::HexGrid{};
            aoc::map::MapGenerator::generate(liveConfig, grid);
        };

        auto buildPlateStats = [&]() -> std::string {
            std::lock_guard<std::mutex> lock(gridMutex);
            std::ostringstream o;
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
                minCol[i] = W;
                maxCol[i] = -1;
                minRow[i] = H;
                maxRow[i] = -1;
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
            o << '[';
            bool first = true;
            for (int32_t pid = 0; pid < 256; ++pid) {
                if (cellCount[pid] == 0) continue;
                if (!first) o << ',';
                first = false;
                const float lf =
                    static_cast<float>(landCount[pid]) / static_cast<float>(cellCount[pid]);
                const float ccol =
                    static_cast<float>(sumCol[pid]) / static_cast<float>(cellCount[pid]);
                const float crow =
                    static_cast<float>(sumRow[pid]) / static_cast<float>(cellCount[pid]);
                o << "{\"plate_id\":" << pid << ",\"cell_count\":" << cellCount[pid]
                  << ",\"land_frac\":" << lf << ",\"min_col\":" << minCol[pid]
                  << ",\"max_col\":" << maxCol[pid] << ",\"min_row\":" << minRow[pid]
                  << ",\"max_row\":" << maxRow[pid] << ",\"centroid_col\":" << ccol
                  << ",\"centroid_row\":" << crow << '}';
            }
            o << ']';
            return o.str();
        };

        using DSM = aoc::debug::DebugServer::Method;
        aoc::debug::DebugServer server(httpPort);

        server.routeJson(DSM::Get, "/ping",
                         [](const std::unordered_map<std::string, std::string>&,
                            const std::string&) -> std::string { return "\"pong\""; });

        server.routeJson(DSM::Get, "/info",
                         [&](const std::unordered_map<std::string, std::string>&,
                             const std::string&) -> std::string {
                             std::lock_guard<std::mutex> lock(gridMutex);
                             std::ostringstream o;
                             int32_t plates      = 0;
                             int32_t mtnTiles    = 0;
                             int32_t landTiles   = 0;
                             int32_t oceanTiles  = 0;
                             const int32_t total = grid.tileCount();
                             for (int32_t i = 0; i < total; ++i) {
                                 const aoc::map::TerrainType t = grid.terrain(i);
                                 if (t == aoc::map::TerrainType::Mountain) ++mtnTiles;
                                 if (aoc::map::isWater(t))
                                     ++oceanTiles;
                                 else
                                     ++landTiles;
                             }
                             std::vector<bool> seenPlate(256, false);
                             for (int32_t i = 0; i < total; ++i) {
                                 const uint8_t pid = grid.plateId(i);
                                 if (pid != 0xFFu) seenPlate[pid] = true;
                             }
                             for (bool s : seenPlate)
                                 if (s) ++plates;
                             o << "{\"seed\":" << liveConfig.seed << ",\"width\":" << grid.width()
                               << ",\"height\":" << grid.height() << ",\"plates\":" << plates
                               << ",\"mountainTiles\":" << mtnTiles
                               << ",\"landTiles\":" << landTiles << ",\"oceanTiles\":" << oceanTiles
                               << ",\"creatorTime\":" << currentMy
                               << ",\"creatorTotal\":" << totalMy << "}";
                             return o.str();
                         });

        server.routeJson(DSM::Get, "/plates",
                         [&](const std::unordered_map<std::string, std::string>&,
                             const std::string&) -> std::string { return buildPlateStats(); });

        server.routeJson(
            DSM::Get, "/tile",
            [&](const std::unordered_map<std::string, std::string>& q,
                const std::string&) -> std::string {
                auto itC = q.find("col");
                auto itR = q.find("row");
                if (itC == q.end() || itR == q.end()) {
                    return "{\"error\":\"missing col / row\"}";
                }
                const int32_t col = std::atoi(itC->second.c_str());
                const int32_t row = std::atoi(itR->second.c_str());
                std::lock_guard<std::mutex> lock(gridMutex);
                if (col < 0 || col >= grid.width() || row < 0 || row >= grid.height()) {
                    return "{\"error\":\"out of range\"}";
                }
                const int32_t idx = row * grid.width() + col;
                std::ostringstream o;
                o << "{\"col\":" << col << ",\"row\":" << row
                  << ",\"terrain\":" << static_cast<int32_t>(grid.terrain(idx))
                  << ",\"plate_id\":" << static_cast<int32_t>(grid.plateId(idx)) << "}";
                return o.str();
            });

        server.routeJson(DSM::Post, "/dump/grid",
                         [&](const std::unordered_map<std::string, std::string>& q,
                             const std::string&) -> std::string {
                             HandlerScope scope(inFlight, drainCv, drainMutex);
                             auto it = q.find("path");
                             if (it == q.end() || it->second.empty()) {
                                 return "{\"error\":\"missing path\"}";
                             }
                             std::ofstream f(it->second);
                             if (!f.is_open()) return "{\"error\":\"open failed\"}";
                             std::lock_guard<std::mutex> lock(gridMutex);
                             const int32_t W = grid.width();
                             const int32_t H = grid.height();
                             for (int32_t row = 0; row < H; ++row) {
                                 if (row & 1) f << ' ';
                                 for (int32_t col = 0; col < W; ++col) {
                                     const int32_t idx = row * W + col;
                                     const uint8_t pid = grid.plateId(idx);
                                     char glyph;
                                     if (pid == 0xFFu)
                                         glyph = '.';
                                     else
                                         glyph = static_cast<char>('a' + (pid % 26));
                                     f << glyph;
                                 }
                                 f << '\n';
                             }
                             std::ostringstream o;
                             o << "{\"path\":\"" << it->second << "\",\"width\":" << W
                               << ",\"height\":" << H << "}";
                             return o.str();
                         });

        server.routeJson(DSM::Post, "/dump/plates",
                         [&](const std::unordered_map<std::string, std::string>& q,
                             const std::string&) -> std::string {
                             HandlerScope scope(inFlight, drainCv, drainMutex);
                             auto it = q.find("path");
                             if (it == q.end() || it->second.empty()) {
                                 return "{\"error\":\"missing path\"}";
                             }
                             std::ofstream f(it->second);
                             if (!f.is_open()) return "{\"error\":\"open failed\"}";
                             f << "plate_id,cell_count,land_frac,min_col,max_col,"
                                  "min_row,max_row,centroid_col,centroid_row\n";
                             {
                                 std::lock_guard<std::mutex> lock(gridMutex);
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
                                     minCol[i] = W;
                                     maxCol[i] = -1;
                                     minRow[i] = H;
                                     maxRow[i] = -1;
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
                                     const float lf   = static_cast<float>(landCount[pid]) /
                                                        static_cast<float>(cellCount[pid]);
                                     const float ccol = static_cast<float>(sumCol[pid]) /
                                                        static_cast<float>(cellCount[pid]);
                                     const float crow = static_cast<float>(sumRow[pid]) /
                                                        static_cast<float>(cellCount[pid]);
                                     f << pid << ',' << cellCount[pid] << ',' << lf << ','
                                       << minCol[pid] << ',' << maxCol[pid] << ',' << minRow[pid]
                                       << ',' << maxRow[pid] << ',' << ccol << ',' << crow << '\n';
                                 }
                             }
                             std::ostringstream o;
                             o << "{\"path\":\"" << it->second << "\"}";
                             return o.str();
                         });

        server.routeJson(DSM::Post, "/sim/re-roll",
                         [&](const std::unordered_map<std::string, std::string>& q,
                             const std::string&) -> std::string {
                             HandlerScope scope(inFlight, drainCv, drainMutex);
                             uint64_t newSeed = liveConfig.seed + 1;
                             auto it          = q.find("seed");
                             if (it != q.end()) {
                                 newSeed = std::strtoull(it->second.c_str(), nullptr, 10);
                             }
                             std::lock_guard<std::mutex> lock(gridMutex);
                             liveConfig.seed = newSeed;
                             regenAtMy(lock, totalMy);
                             std::ostringstream o;
                             o << "{\"seed\":" << newSeed << ",\"creatorTime\":" << currentMy
                               << ",\"creatorTotal\":" << totalMy << ",\"width\":" << grid.width()
                               << ",\"height\":" << grid.height() << "}";
                             return o.str();
                         });

        server.routeJson(DSM::Post, "/sim/step",
                         [&](const std::unordered_map<std::string, std::string>& q,
                             const std::string&) -> std::string {
                             HandlerScope scope(inFlight, drainCv, drainMutex);
                             int32_t dy = aoc::map::MapGenerator::MY_PER_EPOCH_TARGET;
                             auto it    = q.find("dy");
                             if (it != q.end()) {
                                 dy = std::atoi(it->second.c_str());
                             }
                             std::lock_guard<std::mutex> lock(gridMutex);
                             regenAtMy(lock, currentMy + dy);
                             std::ostringstream o;
                             o << "{\"creatorTime\":" << currentMy
                               << ",\"creatorTotal\":" << totalMy << ",\"dy\":" << dy << "}";
                             return o.str();
                         });

        server.routeJson(DSM::Post, "/sim/set-creator-time",
                         [&](const std::unordered_map<std::string, std::string>& q,
                             const std::string&) -> std::string {
                             HandlerScope scope(inFlight, drainCv, drainMutex);
                             auto it = q.find("my");
                             if (it == q.end()) {
                                 return "{\"error\":\"missing my\"}";
                             }
                             const int32_t targetMy = std::atoi(it->second.c_str());
                             std::lock_guard<std::mutex> lock(gridMutex);
                             regenAtMy(lock, targetMy);
                             std::ostringstream o;
                             o << "{\"creatorTime\":" << currentMy
                               << ",\"creatorTotal\":" << totalMy << "}";
                             return o.str();
                         });

        server.routeJson(DSM::Post, "/quit",
                         [&](const std::unordered_map<std::string, std::string>&,
                             const std::string&) -> std::string {
                             HandlerScope scope(inFlight, drainCv, drainMutex);
                             shutdownRequested.store(true);
                             return "{\"ok\":true}";
                         });

        if (!server.start()) {
            std::fprintf(stderr, "error: HTTP debug server failed to start on port %d\n", httpPort);
            return 1;
        }
        std::printf("aoc_mapgen serving HTTP on 127.0.0.1:%d "
                    "(POST /quit to stop)\n",
                    httpPort);
        while (!shutdownRequested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        // Stop accepting new connections and drain queued tasks.
        server.stop();
        // Defensive drain: cpp-httplib's `task_queue->shutdown()` has
        // already waited for in-flight handlers, but a third-party
        // refactor could change that. Block here until our own
        // counter agrees -- the stack-allocated `grid`, `liveConfig`,
        // and `currentMy` go out of scope at the closing brace below
        // and we must not race that with a live handler. Bound the
        // wait so a stuck handler shows up in CI rather than hanging.
        {
            std::unique_lock<std::mutex> lock(drainMutex);
            const bool drained = drainCv.wait_for(lock, std::chrono::seconds(5), [&]() {
                return inFlight.load(std::memory_order_acquire) == 0;
            });
            if (!drained) {
                std::fprintf(stderr,
                             "warning: %d HTTP handler(s) still in flight after "
                             "server.stop(); terminating anyway\n",
                             inFlight.load(std::memory_order_acquire));
            }
        }
    }
    return 0;
}
