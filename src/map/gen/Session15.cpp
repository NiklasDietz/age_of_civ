/**
 * @file Session15.cpp
 * @brief SESSION 15 implementation.
 */

#include "aoc/map/gen/Session15.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _OPENMP
#  define AOC_S15_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_S15_PARALLEL_FOR_ROWS
#endif

namespace aoc::map::gen {

void runSession15(HexGrid& grid, bool cylindrical,
                  const std::vector<float>& soilFert) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    const bool cylSim    = cylindrical;

    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylSim, col, row, dir, outIdx);
    };

    std::vector<uint8_t> npp     (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> growSeas(static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> frost   (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> carryCap(static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> clay    (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> silt    (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> sand    (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> seasRng (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> diurRng (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> uv      (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> coralB  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> magAnom (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> heatF   (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> volRet  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> tsunRun (static_cast<std::size_t>(totalT), 0);

    AOC_S15_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const std::size_t si = static_cast<std::size_t>(i);
            const TerrainType t = grid.terrain(i);
            const FeatureType f = grid.feature(i);

            // ---- GROWING SEASON / FROST DAYS ----
            int32_t gs = 0;
            if (lat < 0.20f)      { gs = 240; }
            else if (lat < 0.40f) { gs = 200; }
            else if (lat < 0.55f) { gs = 150; }
            else if (lat < 0.70f) { gs = 80;  }
            else                  { gs = 20;  }
            if (t == TerrainType::Mountain) { gs /= 2; }
            if (t == TerrainType::Snow)     { gs = 0; }
            if (t == TerrainType::Tundra)   { gs = std::min(gs, 30); }
            growSeas[si] = static_cast<uint8_t>(gs);
            frost[si] = static_cast<uint8_t>(255 - growSeas[si]);

            // ---- SEASONAL / DIURNAL TEMP RANGE ----
            int32_t seasonalK = static_cast<int32_t>(lat * 200.0f);
            bool nearWater = false;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                const TerrainType nt = grid.terrain(nIdx);
                if (nt == TerrainType::Ocean
                    || nt == TerrainType::ShallowWater) {
                    nearWater = true; break;
                }
            }
            if (nearWater) { seasonalK = seasonalK * 60 / 100; }
            seasRng[si] = static_cast<uint8_t>(
                std::clamp(seasonalK, 0, 255));
            int32_t diurnalK = 80;
            if (t == TerrainType::Desert) { diurnalK = 200; }
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                diurnalK = 30;
            }
            if (f == FeatureType::Jungle) { diurnalK = 40; }
            diurRng[si] = static_cast<uint8_t>(
                std::clamp(diurnalK, 0, 255));

            // ---- UV INDEX ----
            int32_t uvK = static_cast<int32_t>(
                (1.0f - lat) * 200.0f);
            const int8_t elev = grid.elevation(i);
            uvK += elev * 20;
            if (lat > 0.85f) { uvK += 40; }
            uv[si] = static_cast<uint8_t>(std::clamp(uvK, 0, 255));

            // ---- NPP ----
            int32_t nppK = 60;
            if (f == FeatureType::Jungle)      { nppK = 240; }
            else if (f == FeatureType::Forest) { nppK = 180; }
            else if (f == FeatureType::Floodplains
                  || f == FeatureType::Marsh)  { nppK = 200; }
            else if (t == TerrainType::Grassland) { nppK = 120; }
            else if (t == TerrainType::Plains)   { nppK = 80;  }
            else if (t == TerrainType::Tundra)   { nppK = 40;  }
            else if (t == TerrainType::Desert
                  || t == TerrainType::Snow)     { nppK = 10;  }
            if (si < soilFert.size()) {
                nppK = static_cast<int32_t>(
                    static_cast<float>(nppK) * (0.5f + soilFert[si]));
            }
            npp[si] = static_cast<uint8_t>(std::clamp(nppK, 0, 255));

            // ---- CARRYING CAPACITY ----
            int32_t cap = static_cast<int32_t>(npp[si]) / 2;
            if (grid.riverEdges(i) != 0) { cap += 50; }
            if (lat > 0.20f && lat < 0.55f) { cap += 30; }
            if (t == TerrainType::Snow
                || t == TerrainType::Tundra
                || t == TerrainType::Mountain) {
                cap = cap / 4;
            }
            carryCap[si] = static_cast<uint8_t>(std::clamp(cap, 0, 255));

            // ---- SOIL TEXTURE ----
            const auto& so = grid.soilOrder();
            uint8_t cP = 0, sP = 0, sandP = 0;
            if (so.size() > si) {
                switch (so[si]) {
                    case 4:  cP=180; sP=40;  sandP=35;  break;
                    case 5:  cP=140; sP=60;  sandP=55;  break;
                    case 3:  cP=80;  sP=110; sandP=65;  break;
                    case 6:  cP=80;  sP=100; sandP=75;  break;
                    case 7:  cP=40;  sP=70;  sandP=145; break;
                    case 8:  cP=30;  sP=60;  sandP=165; break;
                    case 9:  cP=200; sP=35;  sandP=20;  break;
                    case 10: cP=80;  sP=120; sandP=55;  break;
                    case 11: cP=130; sP=85;  sandP=40;  break;
                    case 12: cP=60;  sP=90;  sandP=105; break;
                    default: cP=80;  sP=85;  sandP=90;  break;
                }
            }
            clay[si] = cP;
            silt[si] = sP;
            sand[si] = sandP;

            // ---- CORAL BLEACH RISK ----
            if (t == TerrainType::ShallowWater
                && lat < 0.30f) {
                const auto& sst = grid.seaSurfaceTemp();
                if (sst.size() > si) {
                    const uint8_t s = sst[si];
                    if (s > 230) {
                        coralB[si] = static_cast<uint8_t>(
                            std::clamp((static_cast<int32_t>(s) - 230) * 10,
                                0, 255));
                    }
                }
            }

            // ---- MAGNETIC ANOMALY STRIPES ----
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                const auto& ages = grid.crustAgeTile();
                if (ages.size() > si) {
                    const float a = ages[si];
                    magAnom[si] = static_cast<uint8_t>(
                        (static_cast<int32_t>(a * 5.0f)) & 0xFF);
                }
            }

            // ---- HEAT FLOW REFINED ----
            int32_t hf = 80;
            const auto& gg = grid.geothermalGradient();
            if (gg.size() > si) { hf = static_cast<int32_t>(gg[si]); }
            const auto& ages = grid.crustAgeTile();
            if (ages.size() > si) {
                const float a = ages[si];
                if (a > 100.0f) { hf = std::max(20, hf - 30); }
            }
            heatF[si] = static_cast<uint8_t>(std::clamp(hf, 0, 255));

            // ---- VOLCANO RETURN PERIOD ----
            const auto& vc = grid.volcanism();
            if (vc.size() > si && vc[si] != 0
                && vc[si] != 6 && vc[si] != 7) {
                int32_t rp = 50;
                if (vc[si] == 2) { rp = 100; }
                if (vc[si] == 3) { rp = 200; }
                if (vc[si] == 5) { rp = 30;  }
                volRet[si] = static_cast<uint8_t>(rp);
            }

            // ---- TSUNAMI RUNUP ----
            const auto& sh = grid.seismicHazard();
            if (sh.size() > si && (sh[si] & 0x08) != 0) {
                int32_t r = 40;
                const auto& sl = grid.slopeAngle();
                if (sl.size() > si && sl[si] < 60) { r += 80; }
                tsunRun[si] = static_cast<uint8_t>(
                    std::clamp(r, 0, 255));
            }
        }
    }

    grid.setNetPrimaryProductivity(std::move(npp));
    grid.setGrowingSeasonDays(std::move(growSeas));
    grid.setFrostDays(std::move(frost));
    grid.setCarryingCapacity(std::move(carryCap));
    grid.setSoilClayPct(std::move(clay));
    grid.setSoilSiltPct(std::move(silt));
    grid.setSoilSandPct(std::move(sand));
    grid.setSeasonalTempRange(std::move(seasRng));
    grid.setDiurnalTempRange(std::move(diurRng));
    grid.setUvIndex(std::move(uv));
    grid.setCoralBleachRisk(std::move(coralB));
    grid.setMagneticAnomaly(std::move(magAnom));
    grid.setHeatFlow(std::move(heatF));
    grid.setVolcanoReturnPeriod(std::move(volRet));
    grid.setTsunamiRunup(std::move(tsunRun));
}

} // namespace aoc::map::gen
