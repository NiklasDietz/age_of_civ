/**
 * @file Session10.cpp
 * @brief SESSION 10 implementation.
 */

#include "aoc/map/gen/Session10.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _OPENMP
#  define AOC_S10_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_S10_PARALLEL_FOR_ROWS
#endif

namespace aoc::map::gen {

void runSession10(const HexGrid& grid, bool cylindrical,
                  const std::vector<uint8_t>& permafrost,
                  Session10Outputs& out) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    const bool cylSim    = cylindrical;

    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylSim, col, row, dir, outIdx);
    };

    out.litho   .assign(static_cast<std::size_t>(totalT), 0);
    out.bedrock .assign(static_cast<std::size_t>(totalT), 0);
    out.sOrder  .assign(static_cast<std::size_t>(totalT), 0);
    out.crustTh .assign(static_cast<std::size_t>(totalT), 0);
    out.geoGrad .assign(static_cast<std::size_t>(totalT), 0);
    out.albedo  .assign(static_cast<std::size_t>(totalT), 0);
    out.vegType .assign(static_cast<std::size_t>(totalT), 0);
    out.atmRiv  .assign(static_cast<std::size_t>(totalT), 0);
    out.cycBasin.assign(static_cast<std::size_t>(totalT), 0);
    out.sst     .assign(static_cast<std::size_t>(totalT), 0);
    out.iceShelf.assign(static_cast<std::size_t>(totalT), 0);
    out.permaD  .assign(static_cast<std::size_t>(totalT), 0);

    AOC_S10_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            const FeatureType f = grid.feature(i);
            const std::size_t si = static_cast<std::size_t>(i);
            const uint8_t pid = grid.plateId(i);
            const float pLand = (pid != 0xFFu && pid < grid.plateLandFrac().size())
                ? grid.plateLandFrac()[pid] : 0.5f;
            const float tileAge = (si < grid.crustAgeTile().size())
                ? grid.crustAgeTile()[si] : 0.0f;

            // ---- LITHOLOGY ----
            {
                const uint8_t rt = (si < grid.rockType().size())
                    ? grid.rockType()[si] : 0;
                uint8_t L = 0;
                if (rt == 3) {
                    L = 19;
                } else if (rt == 5) {
                    L = 9;
                } else if (t == TerrainType::Mountain) {
                    const auto& mst = grid.mountainStructure();
                    const uint8_t s = (si < mst.size()) ? mst[si] : 0;
                    if (s == 3) {
                        L = 5;
                    } else if (s == 1 || s == 6) {
                        L = 14;
                    } else {
                        L = 1;
                    }
                } else if (rt == 1) {
                    L = (pLand < 0.40f) ? 3 : 1;
                } else if (rt == 2) {
                    L = 13;
                } else {
                    if (t == TerrainType::Desert) {
                        L = 7;
                    } else if (lat < 0.30f) {
                        L = 9;
                    } else if (lat > 0.55f) {
                        L = 8;
                    } else {
                        L = 12;
                    }
                }
                out.litho[si] = L;
            }
            if (out.litho[si] == 7 || out.litho[si] == 8 || out.litho[si] == 9
                || out.litho[si] == 10 || out.litho[si] == 12) {
                out.bedrock[si] = (tileAge > 80.0f) ? 14 : 1;
            } else {
                out.bedrock[si] = out.litho[si];
            }

            // ---- SOIL ORDER ----
            {
                uint8_t S = 0;
                if (t == TerrainType::Snow
                    || t == TerrainType::Tundra) {
                    S = 12;
                } else if (t == TerrainType::Desert) {
                    S = 8;
                } else if (f == FeatureType::Marsh
                        || f == FeatureType::Floodplains) {
                    S = 11;
                } else if (t == TerrainType::Mountain) {
                    S = 13;
                } else if (lat < 0.20f) {
                    if (f == FeatureType::Jungle) {
                        S = 4;
                    } else {
                        S = 5;
                    }
                } else if (lat < 0.45f) {
                    if (f == FeatureType::Forest) {
                        S = 6;
                    } else {
                        S = 9;
                    }
                } else if (lat < 0.60f) {
                    if (f == FeatureType::Forest) {
                        S = 6;
                    } else {
                        S = 3;
                    }
                } else {
                    S = 7;
                }
                const auto& vc = grid.volcanism();
                if (vc.size() > si && vc[si] != 0
                    && vc[si] != 6 && vc[si] != 7) {
                    S = 10;
                }
                out.sOrder[si] = S;
            }

            // ---- CRUSTAL THICKNESS ----
            {
                int32_t kmDepth = 30;
                if (pLand < 0.40f) {
                    kmDepth = 8;
                } else if (t == TerrainType::Mountain) {
                    kmDepth = 70 + static_cast<int32_t>(grid.elevation(i)) * 5;
                } else if (tileAge > 100.0f) {
                    kmDepth = 50;
                }
                out.crustTh[si] = static_cast<uint8_t>(
                    std::clamp(kmDepth * 255 / 100, 0, 255));
            }

            // ---- GEOTHERMAL GRADIENT ----
            {
                int32_t flux = 50;
                const auto& vc = grid.volcanism();
                if (vc.size() > si) {
                    if (vc[si] == 5)      { flux = 250; }
                    else if (vc[si] == 1
                          || vc[si] == 2
                          || vc[si] == 3
                          || vc[si] == 4) { flux = 150; }
                }
                if (pLand >= 0.40f && tileAge > 100.0f) {
                    flux = 35;
                } else if (pLand < 0.40f) {
                    flux = std::max(flux, 80);
                }
                out.geoGrad[si] = static_cast<uint8_t>(
                    std::clamp(flux * 255 / 300, 0, 255));
            }

            // ---- ALBEDO ----
            {
                int32_t alb = 60;
                if (t == TerrainType::Snow)        { alb = 220; }
                else if (t == TerrainType::Tundra) { alb = 150; }
                else if (t == TerrainType::Desert) { alb = 100; }
                else if (t == TerrainType::Mountain) { alb = 130; }
                else if (t == TerrainType::Grassland) { alb = 50; }
                else if (t == TerrainType::Ocean
                      || t == TerrainType::ShallowWater) { alb = 18; }
                if (f == FeatureType::Forest)        { alb -= 20; }
                if (f == FeatureType::Jungle)        { alb -= 30; }
                if (f == FeatureType::Ice)           { alb = 230; }
                out.albedo[si] = static_cast<uint8_t>(
                    std::clamp(alb, 0, 255));
            }

            // ---- VEGETATION TYPE ----
            {
                uint8_t V = 0;
                if (f == FeatureType::Jungle) {
                    V = 2;
                } else if (f == FeatureType::Forest) {
                    if (lat < 0.30f) {
                        V = 1;
                    } else if (lat < 0.55f) {
                        V = 5;
                    } else if (lat < 0.75f) {
                        V = 3;
                    } else {
                        V = 4;
                    }
                } else if (t == TerrainType::Plains
                        && lat < 0.30f) {
                    V = 6;
                } else if (t == TerrainType::Plains
                        && lat > 0.30f && lat < 0.45f) {
                    V = 7;
                }
                const auto& bs = grid.biomeSubtype();
                if (bs.size() > si && bs[si] == 4) {
                    V = 8;
                }
                out.vegType[si] = V;
            }

            // ---- ATMOSPHERIC RIVER ----
            if ((t == TerrainType::Ocean
                 || t == TerrainType::ShallowWater
                 || t == TerrainType::Plains
                 || t == TerrainType::Grassland)
                && lat > 0.40f && lat < 0.60f) {
                const auto& ch = grid.climateHazard();
                if (ch.size() > si && (ch[si] & 0x04) != 0) {
                    out.atmRiv[si] = 1;
                }
            }

            // ---- CYCLONE BASIN ----
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                const auto& ch = grid.climateHazard();
                if (ch.size() > si && (ch[si] & 0x01) != 0) {
                    const float nx0 = static_cast<float>(col)
                        / static_cast<float>(width);
                    const bool north = (ny < 0.5f);
                    if (north) {
                        if (nx0 < 0.30f)      { out.cycBasin[si] = 1; }
                        else if (nx0 < 0.55f) { out.cycBasin[si] = 2; }
                        else if (nx0 < 0.80f) { out.cycBasin[si] = 3; }
                        else                  { out.cycBasin[si] = 4; }
                    } else {
                        if (nx0 < 0.40f)      { out.cycBasin[si] = 5; }
                        else if (nx0 < 0.70f) { out.cycBasin[si] = 6; }
                        else                  { out.cycBasin[si] = 7; }
                    }
                }
            }

            // ---- SEA SURFACE TEMPERATURE ----
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                const float c = std::cos(lat * 1.5708f);
                float sstC = -2.0f + c * 30.0f;
                const auto& up = grid.upwelling();
                if (up.size() > si && up[si] == 1) {
                    sstC -= 6.0f;
                }
                out.sst[si] = static_cast<uint8_t>(
                    std::clamp((sstC + 2.0f) * 255.0f / 32.0f,
                        0.0f, 255.0f));
            }

            // ---- ICE SHELF ZONE ----
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                if (lat > 0.85f) {
                    bool adjIce = false;
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!nb(col, row, d, nIdx)) { continue; }
                        if (grid.terrain(nIdx) == TerrainType::Snow) {
                            adjIce = true; break;
                        }
                    }
                    if (adjIce) {
                        out.iceShelf[si] = 1;
                    } else if (lat > 0.92f) {
                        out.iceShelf[si] = 3;
                    } else {
                        out.iceShelf[si] = 2;
                    }
                }
            }

            // ---- PERMAFROST DEPTH ----
            {
                if (permafrost[si] != 0) {
                    if (lat > 0.85f) { out.permaD[si] = 30; }
                    else if (lat > 0.75f) { out.permaD[si] = 80; }
                    else if (lat > 0.65f) { out.permaD[si] = 130; }
                    else                  { out.permaD[si] = 180; }
                }
            }
        }
    }
}

} // namespace aoc::map::gen
