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

void runPlateIdStash(HexGrid& grid, bool cylindrical,
                     const std::vector<Plate>& plates,
                     aoc::Random& noiseRng) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const bool cylSim    = cylindrical;
    if (plates.empty()) { return; }

    aoc::Random plateWarpRng(noiseRng);
    AOC_PIS_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const float nx = static_cast<float>(col) / static_cast<float>(width);
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float pwX1 =
                (fractalNoise(nx * 1.2f, ny * 1.2f, 4, 2.0f, 0.55f, plateWarpRng) - 0.5f) * 0.18f;
            const float pwY1 =
                (fractalNoise(nx * 1.2f + 17.0f, ny * 1.2f + 31.0f, 4, 2.0f, 0.55f, plateWarpRng) - 0.5f) * 0.18f;
            const float pwX2 =
                (fractalNoise(nx * 3.0f, ny * 3.0f, 2, 2.0f, 0.5f, plateWarpRng) - 0.5f) * 0.05f;
            const float pwY2 =
                (fractalNoise(nx * 3.0f + 9.0f, ny * 3.0f + 21.0f, 2, 2.0f, 0.5f, plateWarpRng) - 0.5f) * 0.05f;
            const float pwx = nx + pwX1 + pwX2;
            const float pwy = ny + pwY1 + pwY2;
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
                    const uint64_t pseed =
                        static_cast<uint64_t>(p.seedX * 1.0e6f);
                    const int32_t ix1 = static_cast<int32_t>(
                        std::floor(lx * 5.0f));
                    const int32_t iy1 = static_cast<int32_t>(
                        std::floor(ly * 5.0f));
                    const float n1 = hashNoise(ix1, iy1, pseed);
                    const float n2 = hashNoise(
                        ix1, iy1, pseed ^ 0xA5A5ULL);
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

    for (int32_t pass = 0; pass < 6; ++pass) {
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
