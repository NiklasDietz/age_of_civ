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

/// 2026-05-05 Phase 13d-A1: per-plate state dump for offline diagnostics.
/// Columns mirror the ground-truth Plate struct fields persisted on
/// HexGrid in MapGenerator.cpp. Consumers live under tools/ (Python
/// scripts) and recover sphere positions, motion, polygon vertex count,
/// and rotation needed to reproject local-frame polygons into world
/// coordinates.
void writePlateDump(const aoc::map::HexGrid& grid, const std::string& path) {
    std::ofstream out(path);
    if (!out.is_open()) {
        std::fprintf(stderr,
            "%s:%d error: cannot open '%s' for writing (--dump-plates)\n",
            __FILE__, __LINE__, path.c_str());
        return;
    }
    out << "plate_id,latDeg,lonDeg,weight,landFraction,crustAge,"
           "isPolar,mergesAbsorbed,vx,vy,eulerPoleLatDeg,eulerPoleLonDeg,"
           "angularVelDeg,cx,cy,rot,polygon_vertex_count\n";
    const auto& latLons       = grid.plateLatLon();
    const auto& weights       = grid.plateWeight();
    const auto& landFracs     = grid.plateLandFrac();
    const auto& crustAges     = grid.plateCrustAge();
    const auto& isPolars      = grid.plateIsPolar();
    const auto& mergesAbs     = grid.plateMergesAbsorbed();
    const auto& motions       = grid.plateMotions();
    const auto& eulerPoles    = grid.plateEulerPole();
    const auto& angularVels   = grid.plateAngularVelDeg();
    const auto& centers       = grid.plateCenters();
    const auto& rots          = grid.plateRot();
    const auto& polys         = grid.platePolygons();
    const std::size_t n = latLons.size();
    if (weights.size() != n || landFracs.size() != n
        || crustAges.size() != n || isPolars.size() != n
        || mergesAbs.size() != n || motions.size() != n
        || eulerPoles.size() != n || angularVels.size() != n
        || centers.size() != n || rots.size() != n
        || polys.size() != n) {
        std::fprintf(stderr,
            "%s:%d error: plate metadata vectors have inconsistent "
            "sizes (latLon=%zu motions=%zu polys=%zu)\n",
            __FILE__, __LINE__, n, motions.size(), polys.size());
        return;
    }
    for (std::size_t i = 0; i < n; ++i) {
        out << i << ','
            << latLons[i].first      << ',' << latLons[i].second   << ','
            << weights[i]            << ','
            << landFracs[i]          << ','
            << crustAges[i]          << ','
            << static_cast<int>(isPolars[i]) << ','
            << mergesAbs[i]          << ','
            << motions[i].first      << ',' << motions[i].second   << ','
            << eulerPoles[i].first   << ',' << eulerPoles[i].second << ','
            << angularVels[i]        << ','
            << centers[i].first      << ',' << centers[i].second   << ','
            << rots[i]               << ','
            << polys[i].size()       << '\n';
    }
}

/// 2026-05-05 Phase 13d-A1 step 2: per-boundary-edge dump for offline
/// diagnostics. Polygon vertices are stored on HexGrid in plate-LOCAL
/// frame; this writer applies (cx, cy) + 2D rotation by plateRot to
/// emit edge endpoints in world-space normalized [0,1]^2 coords. Lets
/// tools/diagnose_mountain_edges.py answer "how far is each mountain
/// tile from the nearest type-2/4 edge?". Per-edge schema:
///   plate_id, edge_index, ax, ay, bx, by, edge_type, neighbor_plate_id
void writeEdgeDump(const aoc::map::HexGrid& grid, const std::string& path) {
    std::ofstream out(path);
    if (!out.is_open()) {
        std::fprintf(stderr,
            "%s:%d error: cannot open '%s' for writing (--dump-edges)\n",
            __FILE__, __LINE__, path.c_str());
        return;
    }
    out << "plate_id,edge_index,ax,ay,bx,by,edge_type,neighbor_plate_id\n";
    const auto& polys     = grid.platePolygons();
    const auto& edgeTypes = grid.platePolygonEdgeTypes();
    const auto& nbrIds    = grid.platePolygonNeighborIds();
    const auto& centers   = grid.plateCenters();
    const auto& rots      = grid.plateRot();
    const std::size_t n = polys.size();
    if (edgeTypes.size() != n || nbrIds.size() != n
        || centers.size() != n || rots.size() != n) {
        std::fprintf(stderr,
            "%s:%d error: polygon/center/rot vectors mismatched "
            "(polys=%zu types=%zu nbrs=%zu centers=%zu rots=%zu)\n",
            __FILE__, __LINE__, n, edgeTypes.size(), nbrIds.size(),
            centers.size(), rots.size());
        return;
    }
    for (std::size_t pi = 0; pi < n; ++pi) {
        const auto& ring = polys[pi];
        const auto& types = edgeTypes[pi];
        const auto& nbrs  = nbrIds[pi];
        const std::size_t Nv = ring.size();
        if (Nv < 3) { continue; }
        // Edge-type / neighbor-id arrays may legitimately differ in
        // length when an older HexGrid was loaded; skip mismatches
        // rather than emitting garbage.
        const bool typesOk = (types.size() == Nv);
        const bool nbrsOk  = (nbrs.size()  == Nv);
        const float cs = std::cos(rots[pi]);
        const float sn = std::sin(rots[pi]);
        const float cx = centers[pi].first;
        const float cy = centers[pi].second;
        for (std::size_t i = 0; i < Nv; ++i) {
            const std::size_t j = (i + 1) % Nv;
            const float ax = cx + ring[i].first  * cs - ring[i].second * sn;
            const float ay = cy + ring[i].first  * sn + ring[i].second * cs;
            const float bx = cx + ring[j].first  * cs - ring[j].second * sn;
            const float by = cy + ring[j].first  * sn + ring[j].second * cs;
            const int et  = typesOk ? static_cast<int>(types[i]) : 0;
            const int nbr = nbrsOk  ? static_cast<int>(nbrs[i])  : 0xFF;
            out << pi << ',' << i << ','
                << ax << ',' << ay << ',' << bx << ',' << by << ','
                << et << ',' << nbr << '\n';
        }
    }
}

/// 2026-05-05 Phase 13d-A1 step 3: per-mountain-tile -> nearest-boundary
/// dump for offline diagnostics. For each Mountain-terrain tile the writer
/// computes the tile lat/lon (Mollweide inverse) and walks every persisted
/// boundary edge, sampling N points along the edge in normalised world
/// coords + projecting each to lat/lon, taking the minimum haversine
/// distance. Resulting CSV answers "are mountains co-located with the
/// expected convergent (type-2/4) edges, or do they cluster on type-1/3
/// edges (which would prove peakSample window leak)?".
///
/// Schema: tile_index, col, row, lat, lon, owner_plate_id,
///         nearest_edge_owner, nearest_edge_type, nearest_edge_distance_km
void writeMountainEdgeDump(const aoc::map::HexGrid& grid,
                           const std::string& path) {
    std::ofstream out(path);
    if (!out.is_open()) {
        std::fprintf(stderr,
            "%s:%d error: cannot open '%s' for writing "
            "(--dump-mountain-edges)\n",
            __FILE__, __LINE__, path.c_str());
        return;
    }
    out << "tile_index,col,row,lat,lon,owner_plate_id,"
           "nearest_edge_owner,nearest_edge_type,"
           "nearest_edge_distance_km\n";
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const auto& polys     = grid.platePolygons();
    const auto& edgeTypes = grid.platePolygonEdgeTypes();
    const auto& centers   = grid.plateCenters();
    const auto& rots      = grid.plateRot();
    const std::size_t Np = polys.size();
    if (edgeTypes.size() != Np || centers.size() != Np
        || rots.size() != Np) {
        std::fprintf(stderr,
            "%s:%d error: polygon metadata vectors mismatched\n",
            __FILE__, __LINE__);
        return;
    }
    // Pre-rotate polygon vertices into world frame once. Avoids
    // O(M*E) trig in the inner loop.
    struct WorldEdge {
        float ax;
        float ay;
        float bx;
        float by;
        uint8_t edgeType;
        uint8_t plateId;
    };
    std::vector<WorldEdge> edges;
    {
        std::size_t totalEdges = 0;
        for (std::size_t pi = 0; pi < Np; ++pi) {
            if (polys[pi].size() >= 3) { totalEdges += polys[pi].size(); }
        }
        edges.reserve(totalEdges);
    }
    for (std::size_t pi = 0; pi < Np; ++pi) {
        const auto& ring  = polys[pi];
        const auto& types = edgeTypes[pi];
        const std::size_t Nv = ring.size();
        if (Nv < 3) { continue; }
        const float cs = std::cos(rots[pi]);
        const float sn = std::sin(rots[pi]);
        const float cx = centers[pi].first;
        const float cy = centers[pi].second;
        const bool typesOk = (types.size() == Nv);
        for (std::size_t i = 0; i < Nv; ++i) {
            const std::size_t j = (i + 1) % Nv;
            WorldEdge e{};
            e.ax = cx + ring[i].first * cs - ring[i].second * sn;
            e.ay = cy + ring[i].first * sn + ring[i].second * cs;
            e.bx = cx + ring[j].first * cs - ring[j].second * sn;
            e.by = cy + ring[j].first * sn + ring[j].second * cs;
            e.edgeType = typesOk ? types[i] : 0u;
            e.plateId  = static_cast<uint8_t>(pi);
            edges.push_back(e);
        }
    }
    // Inner sample count: 8 points/edge. Polygon edges span at most a
    // few degrees so 8 samples bound the great-circle distance error
    // below ~50 km on Earth scale -- ample for ranking.
    constexpr int32_t SAMPLES_PER_EDGE = 8;
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            if (grid.terrain(idx) != aoc::map::TerrainType::Mountain) {
                continue;
            }
            const auto tileLL =
                aoc::map::gen::tileToLatLon(col, row, width, height);
            if (!tileLL.valid) { continue; } // polar void
            const aoc::map::gen::LatLon tilePos = tileLL.coord;
            float minKm = 1e9f;
            std::size_t bestEdge = 0;
            for (std::size_t e = 0; e < edges.size(); ++e) {
                const WorldEdge& we = edges[e];
                for (int32_t s = 0; s < SAMPLES_PER_EDGE; ++s) {
                    const float t = (static_cast<float>(s) + 0.5f)
                        / static_cast<float>(SAMPLES_PER_EDGE);
                    const float sx = we.ax + t * (we.bx - we.ax);
                    const float sy = we.ay + t * (we.by - we.ay);
                    const auto sLL =
                        aoc::map::gen::mollweideInverse(sx, sy);
                    if (!sLL.valid) { continue; }
                    const float km = aoc::map::gen::haversineKm(
                        tilePos, sLL.coord);
                    if (km < minKm) { minKm = km; bestEdge = e; }
                }
            }
            const WorldEdge* best = edges.empty() ? nullptr : &edges[bestEdge];
            out << idx << ',' << col << ',' << row << ','
                << tilePos.latDeg << ',' << tilePos.lonDeg << ','
                << static_cast<int>(grid.plateId(idx)) << ','
                << (best ? static_cast<int>(best->plateId)  : 0xFF) << ','
                << (best ? static_cast<int>(best->edgeType) : 0)    << ','
                << ((minKm < 9e8f) ? minKm : -1.0f) << '\n';
        }
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
        "          [--dump-plates PATH] [--dump-edges PATH]\n"
        "          [--dump-mountain-edges PATH] [--dump-physics-cells PATH]\n"
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
        "                       for diagnose_mountain_edges.py.\n"
        "  --dump-physics-cells PATH  write one CSV per plate at\n"
        "                       PATH.plate<id>.csv with full PhysicsGrid SoA\n"
        "                       (lat, lon, crust km, surface elev, strain,\n"
        "                       active) for diagnose_peak_sample_leak.py.\n",
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
    std::string dumpPhysicsCellsPath;
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
            config.tectonicEpochs = std::atoi(argv[++i]);
        } else if (arg == "--super-sample" && i + 1 < argc) {
            config.superSampleFactor = std::atoi(argv[++i]);
        } else if (arg == "--frames") {
            frameMode = true;
        } else if (arg == "--dump-plates" && i + 1 < argc) {
            dumpPlatesPath = argv[++i];
        } else if (arg == "--dump-edges" && i + 1 < argc) {
            dumpEdgesPath = argv[++i];
        } else if (arg == "--dump-mountain-edges" && i + 1 < argc) {
            dumpMountainEdgesPath = argv[++i];
        } else if (arg == "--dump-physics-cells" && i + 1 < argc) {
            dumpPhysicsCellsPath = argv[++i];
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
        const int32_t requestedEpochs = (config.tectonicEpochs > 0)
            ? config.tectonicEpochs : 40;
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

    // Phase 13d-A1 step 4: physics-cell dump runs INSIDE
    // MapGenerator::generate() (plates are local-scope), gated by a
    // non-empty config string.
    config.physicsCellDumpPath = dumpPhysicsCellsPath;

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
    if (!dumpPlatesPath.empty()) {
        writePlateDump(grid, dumpPlatesPath);
        std::printf("wrote %s (per-plate CSV, %zu plates)\n",
                    dumpPlatesPath.c_str(), grid.plateLatLon().size());
    }
    if (!dumpEdgesPath.empty()) {
        writeEdgeDump(grid, dumpEdgesPath);
        std::size_t edgeCount = 0;
        for (const auto& ring : grid.platePolygons()) {
            if (ring.size() >= 3) { edgeCount += ring.size(); }
        }
        std::printf("wrote %s (per-edge CSV, %zu edges)\n",
                    dumpEdgesPath.c_str(), edgeCount);
    }
    if (!dumpMountainEdgesPath.empty()) {
        writeMountainEdgeDump(grid, dumpMountainEdgesPath);
        std::size_t mtnCount = 0;
        const int32_t total = grid.tileCount();
        for (int32_t i = 0; i < total; ++i) {
            if (grid.terrain(i) == aoc::map::TerrainType::Mountain) {
                ++mtnCount;
            }
        }
        std::printf("wrote %s (mountain-edge CSV, %zu mountain tiles)\n",
                    dumpMountainEdgesPath.c_str(), mtnCount);
    }
    if (!dumpPhysicsCellsPath.empty()) {
        std::printf("wrote %s.plateNNN.csv (per-plate PhysicsGrid CSVs, "
                    "%zu plates)\n",
                    dumpPhysicsCellsPath.c_str(),
                    grid.plateLatLon().size());
    }
    return 0;
}
