/**
 * @file Session14.cpp
 * @brief SESSION 14 implementation.
 */

#include "aoc/map/gen/Session14.hpp"

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
#  define AOC_S14_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_S14_PARALLEL_FOR_ROWS
#endif

namespace aoc::map::gen {

void runSession14(HexGrid& grid, bool cylindrical,
                  const std::vector<float>& soilFert,
                  const std::vector<float>& orogeny) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    const bool cylSim    = cylindrical;

    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylSim, col, row, dir, outIdx);
    };

    std::vector<uint8_t> streamOrd(static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> nav      (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> damS     (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> ripa     (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> aqRecharge(static_cast<std::size_t>(totalT), 0);
    std::array<std::vector<uint8_t>, 8> crops;
    for (auto& c : crops) {
        c.assign(static_cast<std::size_t>(totalT), 0);
    }
    std::vector<uint8_t> past   (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> forY   (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> foldA  (static_cast<std::size_t>(totalT), 0xFFu);
    std::vector<uint8_t> metaF  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> pStress(static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> cycInt (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> drSev  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> waveH  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> snowL  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> habFrag(static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> endIdx (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> spRich (static_cast<std::size_t>(totalT), 0);

    // ---- STRAHLER STREAM ORDER ----
    for (int32_t i = 0; i < totalT; ++i) {
        if (grid.riverEdges(i) != 0) {
            streamOrd[static_cast<std::size_t>(i)] = 1;
        }
    }
    for (int32_t pass = 0; pass < 6; ++pass) {
        for (int32_t i = 0; i < totalT; ++i) {
            if (grid.riverEdges(i) == 0) { continue; }
            const int32_t row = i / width;
            const int32_t col = i % width;
            int32_t upstreamCount[8] = {0};
            int32_t maxUp = 0;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                if (grid.riverEdges(nIdx) == 0) { continue; }
                const auto& fd = grid.flowDir();
                const std::size_t nsi = static_cast<std::size_t>(nIdx);
                if (fd.size() <= nsi) { continue; }
                if (fd[nsi] == static_cast<uint8_t>((d + 3) % 6)) {
                    const uint8_t up = streamOrd[nsi];
                    if (up > 0 && up <= 7) { upstreamCount[up]++; }
                    if (up > maxUp) { maxUp = up; }
                }
            }
            uint8_t order = static_cast<uint8_t>(std::max(1, maxUp));
            if (upstreamCount[maxUp] >= 2) {
                order = static_cast<uint8_t>(maxUp + 1);
            }
            if (order > streamOrd[static_cast<std::size_t>(i)]) {
                streamOrd[static_cast<std::size_t>(i)] = order;
            }
        }
    }

    // ---- RIVER NAVIGABILITY + DAM SITE + RIPARIAN ----
    AOC_S14_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            if (grid.riverEdges(i) != 0) {
                const std::size_t si = static_cast<std::size_t>(i);
                const uint8_t ord = streamOrd[si];
                const auto& rr = grid.riverRegime();
                const auto& sl = grid.slopeAngle();
                const bool peren = (rr.size() > si && rr[si] == 1);
                const bool flat = (sl.size() > si && sl[si] < 80);
                if (ord >= 4 && peren && flat) {
                    nav[si] = 1;
                }
                if (peren && sl.size() > si && sl[si] > 100) {
                    damS[si] = static_cast<uint8_t>(
                        std::clamp(static_cast<int32_t>(sl[si]) + 50, 0, 255));
                }
            }
            bool nearRiver = (grid.riverEdges(i) != 0);
            if (!nearRiver) {
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nb(col, row, d, nIdx)) { continue; }
                    if (grid.riverEdges(nIdx) != 0) {
                        nearRiver = true; break;
                    }
                }
            }
            if (nearRiver) {
                ripa[static_cast<std::size_t>(i)] = 1;
            }
        }
    }

    // ---- AQUIFER RECHARGE ----
    AOC_S14_PARALLEL_FOR_ROWS
    for (int32_t i = 0; i < totalT; ++i) {
        const auto& he = grid.hydroExtras();
        const std::size_t si = static_cast<std::size_t>(i);
        if (he.size() <= si || (he[si] & 0x01) == 0) { continue; }
        int32_t rate = 80;
        if (i < static_cast<int32_t>(soilFert.size())) {
            rate += static_cast<int32_t>(soilFert[si] * 80.0f);
        }
        const auto& cc = grid.cloudCover();
        if (cc.size() > si) {
            rate += static_cast<int32_t>(cc[si] * 60.0f);
        }
        aqRecharge[si] = static_cast<uint8_t>(
            std::clamp(rate, 0, 255));
    }

    // ---- PER-CROP SUITABILITY ----
    AOC_S14_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            const FeatureType f = grid.feature(i);
            const std::size_t si = static_cast<std::size_t>(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater
                || t == TerrainType::Mountain
                || t == TerrainType::Snow) {
                continue;
            }
            const float fert = (si < soilFert.size())
                ? soilFert[si] : 0.5f;
            if ((t == TerrainType::Plains || t == TerrainType::Grassland)
                && lat > 0.30f && lat < 0.55f) {
                crops[0][si] = static_cast<uint8_t>(
                    std::clamp(fert * 200.0f + 40.0f, 0.0f, 255.0f));
            }
            if (lat < 0.40f
                && (f == FeatureType::Floodplains
                    || f == FeatureType::Marsh
                    || (t == TerrainType::Grassland
                        && grid.riverEdges(i) != 0))) {
                crops[1][si] = static_cast<uint8_t>(
                    std::clamp(fert * 220.0f + 30.0f, 0.0f, 255.0f));
            }
            if (t == TerrainType::Grassland
                && lat > 0.20f && lat < 0.45f) {
                crops[2][si] = static_cast<uint8_t>(
                    std::clamp(fert * 200.0f + 30.0f, 0.0f, 255.0f));
            }
            if (f == FeatureType::Hills
                && lat > 0.40f && lat < 0.60f) {
                crops[3][si] = static_cast<uint8_t>(
                    std::clamp(fert * 180.0f + 50.0f, 0.0f, 255.0f));
            }
            if (lat < 0.20f
                && (t == TerrainType::Grassland
                    || f == FeatureType::Jungle)) {
                crops[4][si] = static_cast<uint8_t>(
                    std::clamp(fert * 200.0f + 40.0f, 0.0f, 255.0f));
            }
            if (f == FeatureType::Hills
                && lat > 0.15f && lat < 0.35f) {
                crops[5][si] = static_cast<uint8_t>(
                    std::clamp(fert * 200.0f + 40.0f, 0.0f, 255.0f));
            }
            if (t == TerrainType::Plains
                && lat > 0.30f && lat < 0.45f) {
                crops[6][si] = static_cast<uint8_t>(
                    std::clamp(fert * 200.0f + 30.0f, 0.0f, 255.0f));
            }
            if (t == TerrainType::Plains
                && lat > 0.20f && lat < 0.40f) {
                crops[7][si] = static_cast<uint8_t>(
                    std::clamp(fert * 200.0f + 20.0f, 0.0f, 255.0f));
            }
        }
    }

    // ---- PASTURE + FORESTRY ----
    AOC_S14_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            const FeatureType f = grid.feature(i);
            const std::size_t si = static_cast<std::size_t>(i);
            if (t == TerrainType::Grassland && lat > 0.20f
                && lat < 0.60f) {
                const float fert = (si < soilFert.size())
                    ? soilFert[si] : 0.5f;
                past[si] = static_cast<uint8_t>(
                    std::clamp(fert * 220.0f + 30.0f, 0.0f, 255.0f));
            }
            if (f == FeatureType::Forest
                || f == FeatureType::Jungle) {
                const auto& vd = grid.vegetationDensity();
                if (vd.size() > si) {
                    forY[si] = vd[si];
                }
            }
        }
    }

    // ---- FOLD AXIS + METAMORPHIC FACIES + PLATE STRESS ----
    AOC_S14_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const std::size_t si = static_cast<std::size_t>(i);
            const auto& mst = grid.mountainStructure();
            if (mst.size() > si && mst[si] == 1) {
                const int8_t myE = grid.elevation(i);
                int32_t bestDir = -1;
                int8_t bestDiff = 127;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nb(col, row, d, nIdx)) { continue; }
                    if (grid.terrain(nIdx) != TerrainType::Mountain) {
                        continue;
                    }
                    const int8_t df = static_cast<int8_t>(
                        std::abs(grid.elevation(nIdx) - myE));
                    if (df < bestDiff) {
                        bestDiff = df;
                        bestDir = d;
                    }
                }
                if (bestDir >= 0) {
                    foldA[si] = static_cast<uint8_t>(bestDir);
                }
            }
            const auto& rk = grid.rockType();
            if (rk.size() > si && rk[si] == 2) {
                const auto& gg = grid.geothermalGradient();
                const auto& ct = grid.crustalThickness();
                const uint8_t G = (gg.size() > si) ? gg[si] : 50;
                const uint8_t T = (ct.size() > si) ? ct[si] : 100;
                if (G > 200 && T > 100) {
                    metaF[si] = 4;
                } else if (T > 130 && G < 60) {
                    metaF[si] = 5;
                } else if (T > 200 && G < 70) {
                    metaF[si] = 6;
                } else if (T > 80 && G > 100) {
                    metaF[si] = 3;
                } else if (T > 50) {
                    metaF[si] = 2;
                } else {
                    metaF[si] = 1;
                }
            }
            const float oro = orogeny[si];
            pStress[si] = static_cast<uint8_t>(
                std::clamp(std::abs(oro) * 800.0f, 0.0f, 255.0f));
        }
    }

    // ---- CYCLONE INTENSITY + DROUGHT SEVERITY + STORM WAVE ----
    AOC_S14_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const std::size_t si = static_cast<std::size_t>(i);
            const auto& ch = grid.climateHazard();
            const auto& sst = grid.seaSurfaceTemp();
            if (ch.size() > si && (ch[si] & 0x01) != 0
                && sst.size() > si && sst[si] > 200) {
                const int32_t cat = std::min(5,
                    static_cast<int32_t>((sst[si] - 200) / 11));
                cycInt[si] = static_cast<uint8_t>(std::max(1, cat));
            }
            const auto& nh = grid.naturalHazard();
            if (nh.size() > si && (nh[si] & 0x0004) != 0) {
                int32_t sev = 1;
                if (lat > 0.20f && lat < 0.30f) { sev = 2; }
                if (grid.terrain(i) == TerrainType::Desert) {
                    sev += 1;
                }
                const auto& cc = grid.cloudCover();
                if (cc.size() > si && cc[si] < 0.20f) { sev += 1; }
                drSev[si] = static_cast<uint8_t>(std::min(4, sev));
            }
            if (ch.size() > si && (ch[si] & 0x04) != 0) {
                const auto& md = grid.marineDepth();
                int32_t wave = 100;
                if (md.size() > si && md[si] >= 4) { wave = 220; }
                waveH[si] = static_cast<uint8_t>(wave);
            }
            const float snowThr = std::max(0.0f, 1.0f - lat * 1.3f);
            const int8_t elev = grid.elevation(i);
            if (static_cast<float>(elev) >= snowThr * 3.0f
                && elev >= 1) {
                snowL[si] = 1;
            }
        }
    }

    // ---- HABITAT FRAGMENTATION + ENDEMISM + SPECIES RICHNESS ----
    AOC_S14_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const std::size_t si = static_cast<std::size_t>(i);
            const TerrainType t = grid.terrain(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                continue;
            }
            int32_t types[16] = {0};
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                const uint8_t nt = static_cast<uint8_t>(grid.terrain(nIdx));
                if (nt < 16) { types[nt]++; }
            }
            int32_t distinctCount = 0;
            for (int32_t k = 0; k < 16; ++k) {
                if (types[k] > 0) { ++distinctCount; }
            }
            habFrag[si] = static_cast<uint8_t>(
                std::clamp(distinctCount * 50, 0, 255));
            const auto& iso = grid.isolatedRealm();
            if (iso.size() > si && iso[si] != 0) {
                const uint8_t pid = grid.plateId(i);
                int32_t score = 150;
                if (pid != 0xFFu && pid < grid.plateLandFrac().size()
                    && i < static_cast<int32_t>(grid.crustAgeTile().size())) {
                    const float age = grid.crustAgeTile()[si];
                    score += static_cast<int32_t>(age * 0.5f);
                }
                endIdx[si] = static_cast<uint8_t>(
                    std::clamp(score, 0, 255));
            }
            int32_t rich = 60;
            if (lat < 0.20f) { rich = 200; }
            else if (lat < 0.40f) { rich = 150; }
            else if (lat < 0.60f) { rich = 100; }
            if (grid.feature(i) == FeatureType::Jungle) {
                rich += 40;
            }
            const auto& ref = grid.refugium();
            if (ref.size() > si && ref[si] != 0) { rich += 30; }
            spRich[si] = static_cast<uint8_t>(
                std::clamp(rich, 0, 255));
        }
    }

    grid.setStreamOrder(std::move(streamOrd));
    grid.setNavigable(std::move(nav));
    grid.setDamSite(std::move(damS));
    grid.setRiparian(std::move(ripa));
    grid.setAquiferRecharge(std::move(aqRecharge));
    for (int32_t k = 0; k < 8; ++k) {
        grid.setCropSuitability(k, std::move(crops[k]));
    }
    grid.setPastureScore(std::move(past));
    grid.setForestryYield(std::move(forY));
    grid.setFoldAxis(std::move(foldA));
    grid.setMetamorphicFacies(std::move(metaF));
    grid.setPlateStress(std::move(pStress));
    grid.setCycloneIntensity(std::move(cycInt));
    grid.setDroughtSeverity(std::move(drSev));
    grid.setStormWaveHeight(std::move(waveH));
    grid.setSnowLine(std::move(snowL));
    grid.setHabitatFragmentation(std::move(habFrag));
    grid.setEndemismIndex(std::move(endIdx));
    grid.setSpeciesRichness(std::move(spRich));
}

} // namespace aoc::map::gen
