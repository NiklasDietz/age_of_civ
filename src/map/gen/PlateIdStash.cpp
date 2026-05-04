/**
 * @file PlateIdStash.cpp
 * @brief Plate-id Voronoi stash + majority-vote smoothing.
 */

#include "aoc/map/gen/PlateIdStash.hpp"

#include "aoc/map/HexCoord.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/gen/Noise.hpp"
#include "aoc/map/gen/Plate.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#ifdef _OPENMP
#  define AOC_PIS_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_PIS_PARALLEL_FOR_ROWS
#endif

namespace aoc::map::gen {

// 2026-05-04: point-in-polygon test (ray casting). Polygon is a list
// of (x, y) world-coord vertices forming a closed ring. Returns true
// if (px, py) is inside the polygon. O(n) per call where n = vertex
// count.
[[nodiscard]] static bool pointInPolygon(
    float px, float py,
    const std::vector<std::pair<float, float>>& poly) {
    const std::size_t n = poly.size();
    if (n < 3) { return false; }
    bool inside = false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const float xi = poly[i].first;
        const float yi = poly[i].second;
        const float xj = poly[j].first;
        const float yj = poly[j].second;
        if (((yi > py) != (yj > py))
            && (px < (xj - xi) * (py - yi) / (yj - yi + 1e-9f) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

void runPlateIdStash(HexGrid& grid, bool cylindrical,
                     const std::vector<Plate>& plates,
                     aoc::Random& noiseRng) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const bool cylSim    = cylindrical;
    if (plates.empty()) { return; }

    // 2026-05-04: pre-compute world-space polygon vertices for each
    // plate. Done once outside the per-tile loop. Each plate's
    // boundaryVertices are in plate-local frame; transform via
    // plate.rot + plate.cx to world coords.
    const std::size_t nPlates = plates.size();
    std::vector<std::vector<std::pair<float, float>>> worldPolys(nPlates);
    for (std::size_t pi = 0; pi < nPlates; ++pi) {
        const Plate& p = plates[pi];
        if (p.boundaryVertices.empty()) { continue; }
        worldPolys[pi].reserve(p.boundaryVertices.size());
        const float cs = std::cos(p.rot);
        const float sn = std::sin(p.rot);
        for (const std::pair<float, float>& v : p.boundaryVertices) {
            const float wx = p.cx + v.first * cs - v.second * sn;
            const float wy = p.cy + v.first * sn + v.second * cs;
            worldPolys[pi].emplace_back(wx, wy);
        }
    }

    aoc::Random plateWarpRng(noiseRng);
    // 2026-05-04: warp constants aligned with the elevation-pass +
    // side-correctness-pass warp in MapGenerator.cpp (freq 1.3 amp 0.24,
    // freq 3.5 amp 0.06). Previously this stash used freq 1.2 amp 0.18 +
    // freq 3.0 amp 0.05, so the plateId values stored on the grid did NOT
    // match the plate ownership the renderer computed -- downstream
    // consumers reading grid.plateId() saw a different boundary than the
    // visible elevation.
    AOC_PIS_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const float nx = static_cast<float>(col) / static_cast<float>(width);
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float pwX1 =
                (fractalNoise(nx * 1.3f, ny * 1.3f, 4, 2.0f, 0.55f, plateWarpRng) - 0.5f) * 0.24f;
            const float pwY1 =
                (fractalNoise(nx * 1.3f + 17.0f, ny * 1.3f + 31.0f, 4, 2.0f, 0.55f, plateWarpRng) - 0.5f) * 0.24f;
            const float pwX2 =
                (fractalNoise(nx * 3.5f, ny * 3.5f, 2, 2.0f, 0.5f, plateWarpRng) - 0.5f) * 0.06f;
            const float pwY2 =
                (fractalNoise(nx * 3.5f + 9.0f, ny * 3.5f + 21.0f, 2, 2.0f, 0.5f, plateWarpRng) - 0.5f) * 0.06f;
            const float pwx = nx + pwX1 + pwX2;
            const float pwy = ny + pwY1 + pwY2;
            // 2026-05-04: POLYGON-BASED ownership. For each plate
            // whose polygon contains the warped tile point, record a
            // candidate. If exactly one polygon claims the tile,
            // assigned. If multiple (overlap from independent polygon
            // construction), pick the one whose centroid is closest.
            // If no polygon claims (gap), fall through to Voronoi
            // nearest-center as fallback.
            int32_t polyOwner = -1;
            float bestPolyCenterDsq = 1e9f;
            for (std::size_t pi = 0; pi < nPlates; ++pi) {
                if (worldPolys[pi].empty()) { continue; }
                if (!pointInPolygon(pwx, pwy, worldPolys[pi])) { continue; }
                float cdx = pwx - plates[pi].cx;
                float cdy = pwy - plates[pi].cy;
                if (cylSim) {
                    if (cdx >  0.5f) { cdx -= 1.0f; }
                    if (cdx < -0.5f) { cdx += 1.0f; }
                }
                const float cdsq = cdx * cdx + cdy * cdy;
                if (cdsq < bestPolyCenterDsq) {
                    bestPolyCenterDsq = cdsq;
                    polyOwner = static_cast<int32_t>(pi);
                }
            }
            if (polyOwner >= 0 && polyOwner < 255) {
                grid.setPlateId(row * width + col,
                    static_cast<uint8_t>(polyOwner));
                continue;
            }
            // Fallback: Voronoi nearest-center for tiles outside all
            // polygons (gaps from polygon under-reach).
            float d1Sq = 1e9f;
            int32_t nearest = -1;
            for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                const Plate& p = plates[pi];
                const float cs = std::cos(p.rot);
                const float sn = std::sin(p.rot);
                const float aspectFix = static_cast<float>(width)
                    / static_cast<float>(height);
                auto seedDsq = [&](float sx, float sy) {
                    float dx = pwx - sx;
                    float dy = pwy - sy;
                    if (cylSim) {
                        if (dx >  0.5f) { dx -= 1.0f; }
                        if (dx < -0.5f) { dx += 1.0f; }
                    }
                    dx *= aspectFix;
                    float lx = (dx * cs + dy * sn) / p.aspect;
                    float ly = (-dx * sn + dy * cs) * p.aspect;
                    // 2026-05-04: continuous bilinearly-interpolated
                    // noise instead of piecewise-constant
                    // hashNoise(floor(lx*5), ...). Old version produced
                    // visible 0.2-unit stair-step kinks in plate
                    // boundaries -- the "rectangle/triangle with a bit
                    // of noise" symptom. mixSeed() finalises seedX so
                    // plates with nearby seedX values no longer produce
                    // correlated jitter.
                    const uint64_t pseed = mixSeed(
                        static_cast<uint64_t>(p.seedX * 1.0e6f));
                    const float n1 = smoothHashNoise(lx * 4.0f, ly * 4.0f, pseed);
                    const float n2 = smoothHashNoise(lx * 4.0f, ly * 4.0f,
                                                     pseed ^ 0xA5A5ULL);
                    lx += (n1 - 0.5f) * 0.18f;
                    ly += (n2 - 0.5f) * 0.18f;
                    return lx * lx + ly * ly;
                };
                float minDsq = seedDsq(p.cx, p.cy);
                for (const std::pair<float, float>& es : p.extraSeeds) {
                    const float d = seedDsq(es.first, es.second);
                    if (d < minDsq) { minDsq = d; }
                }
                const float dsq = minDsq / (p.weight * p.weight);
                if (dsq < d1Sq) {
                    d1Sq = dsq;
                    nearest = static_cast<int32_t>(pi);
                }
            }
            if (nearest >= 0 && nearest < 255) {
                grid.setPlateId(row * width + col,
                    static_cast<uint8_t>(nearest));
            }
        }
    }

    // 2026-05-04: dropped from 6 -> 2 passes. Six rounds of 3-of-6 majority
    // vote acts as a median filter that rounds every concavity / promontory
    // until plate boundaries become approximately convex, defeating the
    // domain-warp + extraSeeds irregularity. Two passes still cleans up
    // single-tile checkerboard outliers without smoothing the warp shape.
    for (int32_t pass = 0; pass < 2; ++pass) {
        std::vector<uint8_t> nextId(static_cast<std::size_t>(width * height), 0xFFu);
        AOC_PIS_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t idx = row * width + col;
                const uint8_t my = grid.plateId(idx);
                nextId[static_cast<std::size_t>(idx)] = my;
                if (my == 0xFFu) { continue; }
                const hex::AxialCoord ax = hex::offsetToAxial({col, row});
                std::array<int32_t, 6> nbrCounts{};
                std::array<uint8_t, 6> nbrIds{};
                int32_t uniq = 0;
                int32_t valid = 0;
                for (const hex::AxialCoord& n : hex::neighbors(ax)) {
                    if (!grid.isValid(n)) { continue; }
                    ++valid;
                    const uint8_t nid = grid.plateId(grid.toIndex(n));
                    if (nid == 0xFFu) { continue; }
                    bool found = false;
                    for (int32_t k = 0; k < uniq; ++k) {
                        if (nbrIds[static_cast<std::size_t>(k)] == nid) {
                            ++nbrCounts[static_cast<std::size_t>(k)];
                            found = true; break;
                        }
                    }
                    if (!found && uniq < 6) {
                        nbrIds[static_cast<std::size_t>(uniq)] = nid;
                        nbrCounts[static_cast<std::size_t>(uniq)] = 1;
                        ++uniq;
                    }
                }
                if (valid == 0) { continue; }
                int32_t bestCount = 0;
                uint8_t bestId = my;
                for (int32_t k = 0; k < uniq; ++k) {
                    if (nbrCounts[static_cast<std::size_t>(k)] > bestCount) {
                        bestCount = nbrCounts[static_cast<std::size_t>(k)];
                        bestId = nbrIds[static_cast<std::size_t>(k)];
                    }
                }
                if (bestId != my && bestCount >= 3) {
                    nextId[static_cast<std::size_t>(idx)] = bestId;
                }
            }
        }
        for (int32_t i = 0; i < width * height; ++i) {
            const uint8_t nid = nextId[static_cast<std::size_t>(i)];
            if (nid != 0xFFu) {
                grid.setPlateId(i, nid);
            }
        }
    }
}

} // namespace aoc::map::gen
