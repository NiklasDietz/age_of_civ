/**
 * @file Session9.cpp
 * @brief SESSION 9 implementation.
 */

#include "aoc/map/gen/Session9.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _OPENMP
#  define AOC_S9_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_S9_PARALLEL_FOR_ROWS
#endif

namespace aoc::map::gen {

void runSession9(const HexGrid& grid, bool cylindrical,
                 const std::vector<float>& orogeny,
                 const std::vector<uint8_t>& lakeFlag,
                 const std::vector<float>& sediment,
                 const std::vector<uint8_t>& eventMrk,
                 Session9Outputs& out) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    const bool cylSim    = cylindrical;

    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylSim, col, row, dir, outIdx);
    };

    out.kop   .assign(static_cast<std::size_t>(totalT), 0);
    out.mtnS  .assign(static_cast<std::size_t>(totalT), 0);
    out.oreG  .assign(static_cast<std::size_t>(totalT), 0);
    out.strait.assign(static_cast<std::size_t>(totalT), 0);
    out.harbor.assign(static_cast<std::size_t>(totalT), 0);
    out.chanP .assign(static_cast<std::size_t>(totalT), 0);
    out.vegD  .assign(static_cast<std::size_t>(totalT), 0);
    out.coast .assign(static_cast<std::size_t>(totalT), 0);
    out.subV  .assign(static_cast<std::size_t>(totalT), 0);
    out.volP  .assign(static_cast<std::size_t>(totalT), 0);
    out.karstS.assign(static_cast<std::size_t>(totalT), 0);
    out.desS  .assign(static_cast<std::size_t>(totalT), 0);
    out.massW .assign(static_cast<std::size_t>(totalT), 0);
    out.namedW.assign(static_cast<std::size_t>(totalT), 0);
    out.forA  .assign(static_cast<std::size_t>(totalT), 0);
    out.soilM .assign(static_cast<std::size_t>(totalT), 0);

    AOC_S9_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            const FeatureType f = grid.feature(i);

            // ---- KOPPEN ----
            uint8_t kc = 0;
            if (t == TerrainType::Snow) {
                kc = 27;
            } else if (t == TerrainType::Tundra) {
                kc = 26;
            } else if (t == TerrainType::Desert) {
                if (lat < 0.40f) {
                    kc = 4;
                } else {
                    kc = 5;
                }
            } else if (t == TerrainType::Mountain) {
                kc = 28;
            } else if (lat < 0.18f) {
                if (f == FeatureType::Jungle)      { kc = 1; }
                else if (f == FeatureType::Forest) { kc = 2; }
                else                                { kc = 3; }
            } else if (lat < 0.30f) {
                kc = 6;
            } else if (lat < 0.50f) {
                if (f == FeatureType::Forest)      { kc = 12; }
                else                                { kc = 8;  }
            } else if (lat < 0.65f) {
                if (f == FeatureType::Forest)      { kc = 22; }
                else                                { kc = 9;  }
            } else if (lat < 0.80f) {
                kc = 24;
            } else {
                kc = 26;
            }
            out.kop[static_cast<std::size_t>(i)] = kc;

            // ---- MOUNTAIN STRUCTURE ----
            if (t == TerrainType::Mountain) {
                const auto& vc = grid.volcanism();
                if (vc.size() > static_cast<std::size_t>(i)
                    && (vc[static_cast<std::size_t>(i)] == 1
                        || vc[static_cast<std::size_t>(i)] == 2
                        || vc[static_cast<std::size_t>(i)] == 3)) {
                    out.mtnS[static_cast<std::size_t>(i)] = 3;
                } else if (i < static_cast<int32_t>(orogeny.size())
                    && orogeny[static_cast<std::size_t>(i)] > 0.18f) {
                    out.mtnS[static_cast<std::size_t>(i)] = 1;
                } else if (grid.crustAgeTile().size()
                        > static_cast<std::size_t>(i)
                    && grid.crustAgeTile()[static_cast<std::size_t>(i)] > 100.0f) {
                    out.mtnS[static_cast<std::size_t>(i)] = 6;
                } else if (grid.marginType().size()
                        > static_cast<std::size_t>(i)
                    && grid.marginType()[static_cast<std::size_t>(i)] == 1) {
                    out.mtnS[static_cast<std::size_t>(i)] = 2;
                } else {
                    out.mtnS[static_cast<std::size_t>(i)] = 4;
                }
            }

            // ---- ORE GRADE ----
            if (grid.resource(i).isValid()) {
                int32_t g = 100;
                const auto& vc = grid.volcanism();
                if (vc.size() > static_cast<std::size_t>(i)
                    && vc[static_cast<std::size_t>(i)] != 0) {
                    g += 50;
                }
                if (grid.crustAgeTile().size()
                        > static_cast<std::size_t>(i)) {
                    const float a =
                        grid.crustAgeTile()[
                            static_cast<std::size_t>(i)];
                    if (a > 100.0f) { g += 30; }
                    else if (a > 50.0f) { g += 15; }
                }
                if (grid.seismicHazard().size()
                        > static_cast<std::size_t>(i)
                    && (grid.seismicHazard()[
                            static_cast<std::size_t>(i)] & 0x07) >= 3) {
                    g += 40;
                }
                g += static_cast<int32_t>(
                    (i * 1103515245u + 12345u) % 50u) - 25;
                out.oreG[static_cast<std::size_t>(i)] =
                    static_cast<uint8_t>(std::clamp(g, 30, 255));
            }

            // ---- VEGETATION DENSITY ----
            {
                int32_t v = 30;
                if (f == FeatureType::Jungle)      { v = 240; }
                else if (f == FeatureType::Forest) { v = 180; }
                else if (f == FeatureType::Marsh
                      || f == FeatureType::Floodplains) {
                    v = 120;
                } else if (t == TerrainType::Grassland) {
                    v = 80;
                } else if (t == TerrainType::Plains) {
                    v = 60;
                } else if (t == TerrainType::Tundra) {
                    v = 30;
                } else if (t == TerrainType::Desert
                        || t == TerrainType::Snow) {
                    v = 5;
                }
                out.vegD[static_cast<std::size_t>(i)] =
                    static_cast<uint8_t>(std::clamp(v, 0, 255));
            }

            // ---- FOREST AGE CLASS ----
            if (f == FeatureType::Forest
                || f == FeatureType::Jungle) {
                if (grid.isolatedRealm().size()
                        > static_cast<std::size_t>(i)
                    && grid.isolatedRealm()[
                            static_cast<std::size_t>(i)] != 0) {
                    out.forA[static_cast<std::size_t>(i)] = 4;
                } else {
                    out.forA[static_cast<std::size_t>(i)] = 3;
                }
            } else if (t == TerrainType::Plains
                && lat < 0.30f) {
                out.forA[static_cast<std::size_t>(i)] = 1;
            }

            // ---- SOIL MOISTURE REGIME ----
            {
                uint8_t sm = 3;
                if (t == TerrainType::Desert) { sm = 1; }
                else if (lat > 0.30f && lat < 0.50f
                    && t == TerrainType::Plains) {
                    sm = 2;
                } else if (f == FeatureType::Marsh
                        || f == FeatureType::Floodplains) {
                    sm = 5;
                } else if (lakeFlag[static_cast<std::size_t>(i)] != 0) {
                    sm = 6;
                } else if (f == FeatureType::Forest
                        || f == FeatureType::Jungle) {
                    sm = 4;
                }
                out.soilM[static_cast<std::size_t>(i)] = sm;
            }

            // ---- DESERT SUBTYPE ----
            if (t == TerrainType::Desert) {
                const auto& vc = grid.volcanism();
                if (vc.size() > static_cast<std::size_t>(i)
                    && vc[static_cast<std::size_t>(i)] == 7) {
                    out.desS[static_cast<std::size_t>(i)] = 1;
                } else {
                    bool nearLake = false;
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!nb(col, row, d, nIdx)) { continue; }
                        if (lakeFlag[
                                static_cast<std::size_t>(nIdx)] != 0) {
                            nearLake = true; break;
                        }
                    }
                    if (nearLake) {
                        out.desS[static_cast<std::size_t>(i)] = 4;
                    } else if (grid.elevation(i) >= 1) {
                        out.desS[static_cast<std::size_t>(i)] = 3;
                    } else if (i < static_cast<int32_t>(sediment.size())
                            && sediment[static_cast<std::size_t>(i)] < 0.02f) {
                        out.desS[static_cast<std::size_t>(i)] = 2;
                    } else if (i < static_cast<int32_t>(sediment.size())
                            && sediment[static_cast<std::size_t>(i)] > 0.05f) {
                        out.desS[static_cast<std::size_t>(i)] = 5;
                    }
                }
            }

            // ---- KARST SUBTYPE ----
            {
                const auto& rk = grid.rockType();
                if (rk.size() > static_cast<std::size_t>(i)
                    && rk[static_cast<std::size_t>(i)] == 5) {
                    if (lat < 0.25f) {
                        out.karstS[static_cast<std::size_t>(i)] = 3;
                    } else if (f == FeatureType::Hills) {
                        out.karstS[static_cast<std::size_t>(i)] = 1;
                    } else if (lat > 0.40f
                        && t == TerrainType::Plains) {
                        out.karstS[static_cast<std::size_t>(i)] = 2;
                    } else {
                        out.karstS[static_cast<std::size_t>(i)] = 4;
                    }
                }
            }

            // ---- MASS WASTING ----
            if (t == TerrainType::Mountain) {
                if (lat > 0.55f) {
                    out.massW[static_cast<std::size_t>(i)] = 5;
                } else {
                    out.massW[static_cast<std::size_t>(i)] = 1;
                }
            } else if (f == FeatureType::Hills
                && i < static_cast<int32_t>(sediment.size())
                && sediment[static_cast<std::size_t>(i)] > 0.04f) {
                if (lat < 0.30f) {
                    out.massW[static_cast<std::size_t>(i)] = 4;
                } else {
                    out.massW[static_cast<std::size_t>(i)] = 2;
                }
            }

            // ---- NAMED WINDS ----
            if (f == FeatureType::Hills) {
                bool nearMtn = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nb(col, row, d, nIdx)) { continue; }
                    if (grid.terrain(nIdx) == TerrainType::Mountain) {
                        nearMtn = true; break;
                    }
                }
                if (nearMtn) {
                    if (lat > 0.40f && lat < 0.55f) {
                        out.namedW[static_cast<std::size_t>(i)] = 5;
                    }
                }
            }
            if (t == TerrainType::Plains
                && lat > 0.35f && lat < 0.50f) {
                out.namedW[static_cast<std::size_t>(i)] = 1;
            }
            if (t == TerrainType::Desert
                && lat > 0.20f && lat < 0.40f) {
                out.namedW[static_cast<std::size_t>(i)] = 4;
            }
            if (lat > 0.85f) {
                out.namedW[static_cast<std::size_t>(i)] = 6;
            }

            // ---- COASTAL FEATURE ----
            if (t != TerrainType::Ocean
                && t != TerrainType::ShallowWater) {
                int32_t waterNb = 0;
                int32_t totalNb = 0;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nb(col, row, d, nIdx)) { continue; }
                    ++totalNb;
                    const TerrainType nt = grid.terrain(nIdx);
                    if (nt == TerrainType::Ocean
                        || nt == TerrainType::ShallowWater) {
                        ++waterNb;
                    }
                }
                if (totalNb > 0 && waterNb >= 4) {
                    out.coast[static_cast<std::size_t>(i)] = 1;
                } else if (waterNb == 3) {
                    out.coast[static_cast<std::size_t>(i)] = 4;
                } else if (waterNb == 1 && totalNb == 6) {
                    out.coast[static_cast<std::size_t>(i)] = 2;
                }
            }

            // ---- CHANNEL PATTERN ----
            if (grid.riverEdges(i) != 0) {
                const int8_t myE = grid.elevation(i);
                int8_t maxDrop = 0;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nb(col, row, d, nIdx)) { continue; }
                    const int8_t nE = grid.elevation(nIdx);
                    const int8_t drop = static_cast<int8_t>(myE - nE);
                    if (drop > maxDrop) { maxDrop = drop; }
                }
                const float sd = (i < static_cast<int32_t>(sediment.size()))
                    ? sediment[static_cast<std::size_t>(i)] : 0.0f;
                if (maxDrop >= 2)               { out.chanP[static_cast<std::size_t>(i)] = 1; }
                else if (sd > 0.08f && lat > 0.55f)
                                                { out.chanP[static_cast<std::size_t>(i)] = 3; }
                else if (sd > 0.05f)            { out.chanP[static_cast<std::size_t>(i)] = 2; }
                else                            { out.chanP[static_cast<std::size_t>(i)] = 4; }
            }

            // ---- SUBMARINE VENT ----
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                if (i < static_cast<int32_t>(orogeny.size())
                    && orogeny[static_cast<std::size_t>(i)] > 0.005f
                    && orogeny[static_cast<std::size_t>(i)] < 0.06f) {
                    if (((i * 2654435761u) >> 16) % 64u == 0) {
                        out.subV[static_cast<std::size_t>(i)] = 1;
                    }
                } else if (i < static_cast<int32_t>(orogeny.size())
                    && orogeny[static_cast<std::size_t>(i)] < -0.04f) {
                    if (((i * 2654435761u) >> 16) % 96u == 0) {
                        out.subV[static_cast<std::size_t>(i)] = 2;
                    }
                }
            }

            // ---- VOLCANIC PROFILE ----
            if (eventMrk[static_cast<std::size_t>(i)] == 1
                || eventMrk[static_cast<std::size_t>(i)] == 3) {
                const auto& vc = grid.volcanism();
                uint8_t magma = 1;
                if (vc.size() > static_cast<std::size_t>(i)) {
                    if (vc[static_cast<std::size_t>(i)] == 2) {
                        magma = 0;
                    } else if (vc[static_cast<std::size_t>(i)] == 1) {
                        magma = 1;
                    } else if (vc[static_cast<std::size_t>(i)] == 3) {
                        magma = 2;
                    }
                }
                uint8_t vei = 4;
                if (eventMrk[static_cast<std::size_t>(i)] == 3) {
                    vei = 7;
                } else if (magma == 2) {
                    vei = 5;
                } else if (magma == 0) {
                    vei = 2;
                }
                out.volP[static_cast<std::size_t>(i)] =
                    static_cast<uint8_t>(vei | (magma << 4));
            }
        }
    }

    // ---- STRAIT detection ----
    AOC_S9_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            if (t != TerrainType::Ocean
                && t != TerrainType::ShallowWater) { continue; }
            if (lakeFlag[static_cast<std::size_t>(i)] != 0) { continue; }
            std::array<bool, 6> nbLand{};
            int32_t landN = 0, waterN = 0;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                const TerrainType nt = grid.terrain(nIdx);
                if (nt == TerrainType::Ocean
                    || nt == TerrainType::ShallowWater) {
                    ++waterN;
                } else {
                    ++landN;
                    nbLand[static_cast<std::size_t>(d)] = true;
                }
            }
            if (landN >= 2 && waterN >= 2) {
                bool opposite = false;
                for (int32_t d = 0; d < 3; ++d) {
                    if (nbLand[static_cast<std::size_t>(d)]
                        && nbLand[static_cast<std::size_t>(d + 3)]) {
                        opposite = true; break;
                    }
                }
                if (opposite) {
                    out.strait[static_cast<std::size_t>(i)] = 1;
                }
            }
        }
    }

    // ---- HARBOR SCORE ----
    AOC_S9_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            if (grid.terrain(i) != TerrainType::ShallowWater) {
                continue;
            }
            int32_t landNb = 0;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                const TerrainType nt = grid.terrain(nIdx);
                if (nt != TerrainType::Ocean
                    && nt != TerrainType::ShallowWater) {
                    ++landNb;
                }
            }
            if (landNb >= 3) {
                int32_t s = 80 + 30 * landNb;
                out.harbor[static_cast<std::size_t>(i)] =
                    static_cast<uint8_t>(std::clamp(s, 0, 255));
            }
        }
    }
}

} // namespace aoc::map::gen
