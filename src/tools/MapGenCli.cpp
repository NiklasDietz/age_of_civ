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
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/SphereGeometry.hpp"

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
    bool         serveHttp = false;
    int32_t      httpPort  = 9876;

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
        } else if (arg == "--serve-http") {
            serveHttp = true;
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
        std::mutex            drainMutex;
        std::condition_variable drainCv;

        // RAII helper: increment inFlight on entry, decrement and
        // notify on exit. Construct one at the top of every mutating
        // handler. Read-only handlers do not need it -- their state
        // accesses are guarded by gridMutex which already serialises
        // against the regen path.
        struct HandlerScope {
            std::atomic<int32_t>&    counter;
            std::condition_variable& cv;
            std::mutex&              mtx;

            HandlerScope(std::atomic<int32_t>& c,
                         std::condition_variable& v,
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
        int32_t currentMy = (liveConfig.tectonicTotalMy > 0)
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
        auto regenAtMy = [&](std::lock_guard<std::mutex>& /*heldLock*/,
                             int32_t targetMy) {
            if (targetMy < 0)        targetMy = 0;
            if (targetMy > totalMy)  targetMy = totalMy;
            currentMy = targetMy;
            liveConfig.tectonicTotalMy = std::max(1, targetMy);
            // runEpochsLimit overrides epoch derivation: use it when
            // targetMy is zero so the sim halts before the first epoch
            // and we get the pre-physics initial-cut state.
            liveConfig.runEpochsLimit = (targetMy == 0) ? 1 : 0;
            grid = aoc::map::HexGrid{};
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
            o << '[';
            bool first = true;
            for (int32_t pid = 0; pid < 256; ++pid) {
                if (cellCount[pid] == 0) continue;
                if (!first) o << ',';
                first = false;
                const float lf = static_cast<float>(landCount[pid])
                               / static_cast<float>(cellCount[pid]);
                const float ccol = static_cast<float>(sumCol[pid])
                                 / static_cast<float>(cellCount[pid]);
                const float crow = static_cast<float>(sumRow[pid])
                                 / static_cast<float>(cellCount[pid]);
                o << "{\"plate_id\":" << pid
                  << ",\"cell_count\":" << cellCount[pid]
                  << ",\"land_frac\":" << lf
                  << ",\"min_col\":" << minCol[pid]
                  << ",\"max_col\":" << maxCol[pid]
                  << ",\"min_row\":" << minRow[pid]
                  << ",\"max_row\":" << maxRow[pid]
                  << ",\"centroid_col\":" << ccol
                  << ",\"centroid_row\":" << crow << '}';
            }
            o << ']';
            return o.str();
        };

        using DSM = aoc::debug::DebugServer::Method;
        aoc::debug::DebugServer server(httpPort);

        server.routeJson(DSM::Get, "/ping",
            [](const std::unordered_map<std::string, std::string>&,
               const std::string&) -> std::string {
                return "\"pong\"";
            });

        server.routeJson(DSM::Get, "/info",
            [&](const std::unordered_map<std::string, std::string>&,
                const std::string&) -> std::string {
                std::lock_guard<std::mutex> lock(gridMutex);
                std::ostringstream o;
                int32_t plates = 0;
                int32_t mtnTiles = 0;
                int32_t landTiles = 0;
                int32_t oceanTiles = 0;
                const int32_t total = grid.tileCount();
                for (int32_t i = 0; i < total; ++i) {
                    const aoc::map::TerrainType t = grid.terrain(i);
                    if (t == aoc::map::TerrainType::Mountain) ++mtnTiles;
                    if (aoc::map::isWater(t)) ++oceanTiles;
                    else ++landTiles;
                }
                std::vector<bool> seenPlate(256, false);
                for (int32_t i = 0; i < total; ++i) {
                    const uint8_t pid = grid.plateId(i);
                    if (pid != 0xFFu) seenPlate[pid] = true;
                }
                for (bool s : seenPlate) if (s) ++plates;
                o << "{\"seed\":" << liveConfig.seed
                  << ",\"width\":"  << grid.width()
                  << ",\"height\":" << grid.height()
                  << ",\"plates\":" << plates
                  << ",\"mountainTiles\":" << mtnTiles
                  << ",\"landTiles\":"     << landTiles
                  << ",\"oceanTiles\":"    << oceanTiles
                  << ",\"creatorTime\":"   << currentMy
                  << ",\"creatorTotal\":"  << totalMy
                  << "}";
                return o.str();
            });

        server.routeJson(DSM::Get, "/plates",
            [&](const std::unordered_map<std::string, std::string>&,
                const std::string&) -> std::string {
                return buildPlateStats();
            });

        server.routeJson(DSM::Get, "/tile",
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
                if (col < 0 || col >= grid.width()
                    || row < 0 || row >= grid.height()) {
                    return "{\"error\":\"out of range\"}";
                }
                const int32_t idx = row * grid.width() + col;
                std::ostringstream o;
                o << "{\"col\":" << col << ",\"row\":" << row
                  << ",\"terrain\":" << static_cast<int32_t>(grid.terrain(idx))
                  << ",\"plate_id\":" << static_cast<int32_t>(grid.plateId(idx))
                  << "}";
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
                        if (pid == 0xFFu) glyph = '.';
                        else glyph = static_cast<char>(
                            'a' + (pid % 26));
                        f << glyph;
                    }
                    f << '\n';
                }
                std::ostringstream o;
                o << "{\"path\":\"" << it->second
                  << "\",\"width\":" << W
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
                        f << pid << ',' << cellCount[pid] << ',' << lf << ','
                          << minCol[pid] << ',' << maxCol[pid] << ','
                          << minRow[pid] << ',' << maxRow[pid] << ','
                          << ccol << ',' << crow << '\n';
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
                auto it = q.find("seed");
                if (it != q.end()) {
                    newSeed = std::strtoull(it->second.c_str(), nullptr, 10);
                }
                std::lock_guard<std::mutex> lock(gridMutex);
                liveConfig.seed = newSeed;
                regenAtMy(lock, totalMy);
                std::ostringstream o;
                o << "{\"seed\":" << newSeed
                  << ",\"creatorTime\":" << currentMy
                  << ",\"creatorTotal\":" << totalMy
                  << ",\"width\":"  << grid.width()
                  << ",\"height\":" << grid.height() << "}";
                return o.str();
            });

        server.routeJson(DSM::Post, "/sim/step",
            [&](const std::unordered_map<std::string, std::string>& q,
                const std::string&) -> std::string {
                HandlerScope scope(inFlight, drainCv, drainMutex);
                int32_t dy = aoc::map::MapGenerator::MY_PER_EPOCH_TARGET;
                auto it = q.find("dy");
                if (it != q.end()) {
                    dy = std::atoi(it->second.c_str());
                }
                std::lock_guard<std::mutex> lock(gridMutex);
                regenAtMy(lock, currentMy + dy);
                std::ostringstream o;
                o << "{\"creatorTime\":" << currentMy
                  << ",\"creatorTotal\":" << totalMy
                  << ",\"dy\":" << dy << "}";
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
            std::fprintf(stderr,
                "error: HTTP debug server failed to start on port %d\n",
                httpPort);
            return 1;
        }
        std::printf("aoc_mapgen serving HTTP on 127.0.0.1:%d "
                    "(POST /quit to stop)\n", httpPort);
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
            const bool drained = drainCv.wait_for(
                lock,
                std::chrono::seconds(5),
                [&]() { return inFlight.load(std::memory_order_acquire) == 0; });
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
