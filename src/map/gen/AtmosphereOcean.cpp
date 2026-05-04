/**
 * @file Session3.cpp
 * @brief SESSION 3 implementation.
 */

#include "aoc/map/gen/AtmosphereOcean.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _OPENMP
#  define AOC_S3_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_S3_PARALLEL_FOR_ROWS
#endif

namespace aoc::map::gen {

void runAtmosphereOcean(const HexGrid& grid, bool cylindrical,
                 const std::vector<float>& sediment,
                 const std::vector<uint8_t>& lakeFlag,
                 AtmosphereOceanOutputs& out) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    const bool cylSim    = cylindrical;

    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylSim, col, row, dir, outIdx);
    };

    out.climateHazard.assign(static_cast<std::size_t>(totalT), 0);
    out.glacialFeat  .assign(static_cast<std::size_t>(totalT), 0);
    out.oceanZone    .assign(static_cast<std::size_t>(totalT), 0);
    out.cloudCover   .assign(static_cast<std::size_t>(totalT), 0.0f);
    out.flowDir      .assign(static_cast<std::size_t>(totalT), 0xFFu);

    std::vector<uint8_t>& climateHazard = out.climateHazard;
    std::vector<uint8_t>& glacialFeat   = out.glacialFeat;
    std::vector<uint8_t>& oceanZone     = out.oceanZone;
    std::vector<float>&   cloudCover    = out.cloudCover;
    std::vector<uint8_t>& flowDir       = out.flowDir;

    // ---- HURRICANE / TORNADO / STORM TRACK / JET STREAM ----
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row)
                       / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            const bool isWaterT = (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater);
            uint8_t flag = 0;
            if (isWaterT && lat >= 0.10f && lat <= 0.40f) {
                flag |= 0x01;
            }
            if (!isWaterT
                && lat >= 0.30f && lat <= 0.55f
                && (t == TerrainType::Plains
                    || t == TerrainType::Grassland)) {
                bool warmOcean = false;
                bool nearMtn   = false;
                for (int32_t dr = -6; dr <= 6; ++dr) {
                    const int32_t rr = row + dr;
                    if (rr < 0 || rr >= height) { continue; }
                    for (int32_t dc = -6; dc <= 6; ++dc) {
                        int32_t cc = col + dc;
                        if (cylSim) {
                            if (cc < 0)        { cc += width; }
                            if (cc >= width)   { cc -= width; }
                        } else if (cc < 0 || cc >= width) { continue; }
                        const int32_t nIdx = rr * width + cc;
                        const TerrainType nt = grid.terrain(nIdx);
                        const float nny = static_cast<float>(rr)
                            / static_cast<float>(height);
                        const float nlat = 2.0f * std::abs(nny - 0.5f);
                        if ((nt == TerrainType::Ocean
                            || nt == TerrainType::ShallowWater)
                            && nlat < 0.40f) {
                            warmOcean = true;
                        }
                        if (nt == TerrainType::Mountain) {
                            nearMtn = true;
                        }
                    }
                }
                if (warmOcean && nearMtn) {
                    flag |= 0x02;
                }
            }
            if (isWaterT && lat >= 0.40f && lat <= 0.65f) {
                flag |= 0x04;
            }
            if (lat >= 0.55f && lat <= 0.75f) {
                flag |= 0x08;
            }
            climateHazard[static_cast<std::size_t>(i)] = flag;
        }
    }

    // ---- GLACIAL FEATURES ----
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row)
                       / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            const FeatureType f = grid.feature(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                continue;
            }
            const auto& rt = grid.rockType();
            if (!rt.empty()
                && rt[static_cast<std::size_t>(i)] == 5
                && (i % 17) == 0) {
                glacialFeat[static_cast<std::size_t>(i)] = 3;
                continue;
            }
            if (lat > 0.50f
                && grid.riverEdges(i) != 0) {
                bool nearMtn = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nb(col, row, d, nIdx)) { continue; }
                    if (grid.terrain(nIdx)
                            == TerrainType::Mountain) {
                        nearMtn = true; break;
                    }
                }
                if (nearMtn) {
                    glacialFeat[static_cast<std::size_t>(i)] = 2;
                    continue;
                }
            }
            if (lat >= 0.55f && lat <= 0.70f
                && i < static_cast<int32_t>(sediment.size())
                && sediment[static_cast<std::size_t>(i)] > 0.04f
                && (t == TerrainType::Plains
                    || t == TerrainType::Grassland)) {
                glacialFeat[static_cast<std::size_t>(i)] = 1;
                continue;
            }
            if (lat >= 0.55f && lat <= 0.70f
                && f == FeatureType::Hills) {
                glacialFeat[static_cast<std::size_t>(i)] = 4;
                continue;
            }
            if (lat >= 0.55f && lat <= 0.70f
                && grid.riverEdges(i) != 0) {
                glacialFeat[static_cast<std::size_t>(i)] = 5;
            }
        }
    }

    // ---- OCEAN ZONES ----
    AOC_S3_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row)
                       / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            if (t != TerrainType::Ocean
                && t != TerrainType::ShallowWater) {
                continue;
            }
            int32_t landNb = 0;
            for (int32_t dr = -3; dr <= 3; ++dr) {
                const int32_t rr = row + dr;
                if (rr < 0 || rr >= height) { continue; }
                for (int32_t dc = -3; dc <= 3; ++dc) {
                    int32_t cc = col + dc;
                    if (cylSim) {
                        if (cc < 0)        { cc += width; }
                        if (cc >= width)   { cc -= width; }
                    } else if (cc < 0 || cc >= width) { continue; }
                    const TerrainType nt =
                        grid.terrain(rr * width + cc);
                    if (nt != TerrainType::Ocean
                        && nt != TerrainType::ShallowWater) {
                        ++landNb;
                    }
                }
            }
            uint8_t tidal = 0;
            if (landNb > 30)      { tidal = 3; }
            else if (landNb > 18) { tidal = 2; }
            else if (landNb > 6)  { tidal = 1; }
            uint8_t salin = 1;
            if (lakeFlag[static_cast<std::size_t>(i)] != 0) {
                salin = 0;
                if (lat < 0.40f) { salin = 2; }
                else             { salin = 3; }
            } else if (landNb > 24) {
                salin = 2;
            } else if (lat > 0.80f) {
                salin = 3;
            }
            oceanZone[static_cast<std::size_t>(i)] =
                static_cast<uint8_t>(
                    (tidal & 0x03) | ((salin & 0x03) << 2));
        }
    }

    // ---- CLOUD COVER ----
    for (int32_t i = 0; i < totalT; ++i) {
        const TerrainType t = grid.terrain(i);
        float c = 0.30f;
        switch (t) {
            case TerrainType::Grassland: c = 0.55f; break;
            case TerrainType::Plains:    c = 0.40f; break;
            case TerrainType::Desert:    c = 0.10f; break;
            case TerrainType::Tundra:    c = 0.50f; break;
            case TerrainType::Snow:      c = 0.70f; break;
            case TerrainType::Mountain:  c = 0.55f; break;
            default: break;
        }
        if (grid.feature(i) == FeatureType::Jungle) {
            c = std::min(1.0f, c + 0.30f);
        } else if (grid.feature(i) == FeatureType::Forest) {
            c += 0.10f;
        }
        if ((climateHazard[static_cast<std::size_t>(i)] & 0x01) != 0) {
            c = std::min(1.0f, c + 0.20f);
        }
        cloudCover[static_cast<std::size_t>(i)]
            = std::clamp(c, 0.0f, 1.0f);
    }

    // ---- DRAINAGE FLOW DIRECTION ----
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                continue;
            }
            const int8_t myE = grid.elevation(i);
            int8_t lowest = myE;
            uint8_t bestDir = 0xFFu;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                const int8_t nE = grid.elevation(nIdx);
                if (nE < lowest) {
                    lowest = nE;
                    bestDir = static_cast<uint8_t>(d);
                }
            }
            flowDir[static_cast<std::size_t>(i)] = bestDir;
        }
    }
}

} // namespace aoc::map::gen
