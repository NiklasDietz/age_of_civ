/**
 * @file Session13.cpp
 * @brief SESSION 13 implementation.
 */

#include "aoc/map/gen/Session13.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _OPENMP
#  define AOC_S13_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_S13_PARALLEL_FOR_ROWS
#endif

namespace aoc::map::gen {

void runSession13(HexGrid& grid, bool cylindrical, float axialTilt) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    const bool cylSim    = cylindrical;

    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylSim, col, row, dir, outIdx);
    };

    std::vector<uint8_t> insol  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> aspect (static_cast<std::size_t>(totalT), 0xFFu);
    std::vector<uint8_t> slope  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> ecot   (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> pelagP (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> shelfSed(static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> rebound(static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> sedDir (static_cast<std::size_t>(totalT), 0xFFu);
    std::vector<uint8_t> coastChg(static_cast<std::size_t>(totalT), 0);

    const float tiltDeg = (axialTilt > 0.0f) ? axialTilt : 23.5f;
    const float tiltRad = tiltDeg * 3.14159f / 180.0f;
    AOC_S13_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = std::abs(ny - 0.5f) * 3.14159f;
        const float baseI = std::cos(std::min(lat, 1.5708f));
        const float polar = 0.30f * std::sin(tiltRad);
        const float annualMean = std::clamp(baseI + polar, 0.0f, 1.0f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            float Ival = annualMean;
            const int8_t elev = grid.elevation(i);
            if (elev > 0) {
                Ival *= (1.0f + 0.06f * static_cast<float>(elev));
            }
            if (t == TerrainType::Mountain) {
                Ival *= 1.10f;
            }
            Ival = std::clamp(Ival, 0.0f, 1.0f);
            insol[static_cast<std::size_t>(i)] =
                static_cast<uint8_t>(Ival * 255.0f);
        }
    }

    // ---- SLOPE ANGLE + TOPOGRAPHIC ASPECT ----
    AOC_S13_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const int8_t myE = grid.elevation(i);
            int8_t maxDrop = 0;
            int8_t maxRise = 0;
            int32_t dropDir = -1;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                const int8_t nE = grid.elevation(nIdx);
                const int8_t diff = static_cast<int8_t>(nE - myE);
                if (diff > maxRise) { maxRise = diff; }
                if (-diff > maxDrop) { maxDrop = static_cast<int8_t>(-diff); dropDir = d; }
            }
            const int32_t steep = std::max<int32_t>(maxRise, maxDrop);
            slope[static_cast<std::size_t>(i)] =
                static_cast<uint8_t>(std::clamp(steep * 80, 0, 255));
            if (dropDir >= 0 && maxDrop > 0) {
                aspect[static_cast<std::size_t>(i)] =
                    static_cast<uint8_t>(dropDir);
            }
        }
    }

    // ---- ECOTONES ----
    AOC_S13_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                continue;
            }
            bool transition = false;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                const TerrainType nt = grid.terrain(nIdx);
                if (nt == TerrainType::Ocean
                    || nt == TerrainType::ShallowWater) {
                    continue;
                }
                if (nt != t) { transition = true; break; }
            }
            if (transition) {
                ecot[static_cast<std::size_t>(i)] = 1;
            }
        }
    }

    // ---- PELAGIC PRIMARY PRODUCTIVITY ----
    AOC_S13_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            if (t != TerrainType::Ocean
                && t != TerrainType::ShallowWater) {
                continue;
            }
            int32_t p = 60;
            if (lat < 0.20f) {
                p = 30;
            } else if (lat > 0.40f && lat < 0.70f) {
                p = 110;
            } else if (lat > 0.85f) {
                p = 50;
            }
            const auto& up = grid.upwelling();
            const std::size_t si = static_cast<std::size_t>(i);
            if (up.size() > si && up[si] == 1) { p += 100; }
            bool nearRiver = false;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                if (grid.riverEdges(nIdx) != 0) {
                    nearRiver = true; break;
                }
            }
            if (nearRiver) { p += 60; }
            pelagP[si] = static_cast<uint8_t>(std::clamp(p, 0, 255));
        }
    }

    // ---- CONTINENTAL SHELF SEDIMENT THICKNESS ----
    AOC_S13_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const auto& md = grid.marineDepth();
            const std::size_t si = static_cast<std::size_t>(i);
            if (md.size() <= si || md[si] != 1) { continue; }
            int32_t thick = 30;
            bool nearRiver = false;
            bool nearLand = false;
            bool nearActive = false;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                const TerrainType nt = grid.terrain(nIdx);
                if (nt != TerrainType::Ocean
                    && nt != TerrainType::ShallowWater) {
                    nearLand = true;
                }
                if (grid.riverEdges(nIdx) != 0) { nearRiver = true; }
                const auto& mt = grid.marginType();
                if (mt.size() > static_cast<std::size_t>(nIdx)
                    && mt[static_cast<std::size_t>(nIdx)] == 1) {
                    nearActive = true;
                }
            }
            if (nearLand) { thick += 80; }
            if (nearRiver) { thick += 90; }
            if (nearActive) { thick = std::max(20, thick - 60); }
            shelfSed[si] = static_cast<uint8_t>(
                std::clamp(thick, 0, 255));
        }
    }

    // ---- GLACIAL ISOSTATIC REBOUND ----
    AOC_S13_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        if (lat < 0.55f) { continue; }
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater
                || t == TerrainType::Mountain) { continue; }
            int32_t r = 0;
            if (lat > 0.65f) { r = 200; }
            else             { r = 120; }
            rebound[static_cast<std::size_t>(i)] =
                static_cast<uint8_t>(std::clamp(r, 0, 255));
        }
    }

    // ---- SEDIMENT TRANSPORT DIRECTION ----
    AOC_S13_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                if (lat < 0.30f) {
                    sedDir[static_cast<std::size_t>(i)] = 3;
                } else if (lat < 0.60f) {
                    sedDir[static_cast<std::size_t>(i)] = 0;
                } else {
                    sedDir[static_cast<std::size_t>(i)] = 3;
                }
            } else {
                const auto& fd = grid.flowDir();
                const std::size_t si = static_cast<std::size_t>(i);
                if (fd.size() > si) {
                    sedDir[si] = fd[si];
                }
            }
        }
    }

    // ---- COASTAL ACCRETION / EROSION ----
    AOC_S13_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                continue;
            }
            bool nearWater = false;
            bool nearRiver = false;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                const TerrainType nt = grid.terrain(nIdx);
                if (nt == TerrainType::Ocean
                    || nt == TerrainType::ShallowWater) {
                    nearWater = true;
                }
                if (grid.riverEdges(nIdx) != 0) { nearRiver = true; }
            }
            if (!nearWater) { continue; }
            const auto& cf = grid.cliffCoastAll();
            const std::size_t si = static_cast<std::size_t>(i);
            if (cf.size() > si
                && (cf[si] == 1 || cf[si] == 2 || cf[si] == 3)) {
                coastChg[si] = 2;
            } else if (nearRiver
                || grid.riverEdges(i) != 0) {
                coastChg[si] = 1;
            } else {
                coastChg[si] = 3;
            }
        }
    }

    grid.setSolarInsolation(std::move(insol));
    grid.setTopographicAspect(std::move(aspect));
    grid.setSlopeAngle(std::move(slope));
    grid.setEcotone(std::move(ecot));
    grid.setPelagicProductivity(std::move(pelagP));
    grid.setShelfSedimentThickness(std::move(shelfSed));
    grid.setGlacialRebound(std::move(rebound));
    grid.setSedimentTransportDir(std::move(sedDir));
    grid.setCoastalChange(std::move(coastChg));
}

} // namespace aoc::map::gen
