/**
 * @file Session16.cpp
 * @brief SESSION 16 implementation.
 */

#include "aoc/map/gen/Session16.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _OPENMP
#  define AOC_S16_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_S16_PARALLEL_FOR_ROWS
#endif

namespace aoc::map::gen {

void runSession16(HexGrid& grid, bool cylindrical,
                  const std::vector<float>& soilFert,
                  const std::vector<float>& sediment,
                  const std::vector<uint8_t>& lakeFlag) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    const bool cylSim    = cylindrical;

    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylSim, col, row, dir, outIdx);
    };

    std::vector<uint8_t> tpi    (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> twi    (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> rough  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> curv   (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> rivDisc(static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> drainA (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> wsId   (static_cast<std::size_t>(totalT), 0);
    std::array<std::vector<uint8_t>, 6> livestockS;
    for (auto& v : livestockS) {
        v.assign(static_cast<std::size_t>(totalT), 0);
    }
    std::vector<uint8_t> fault  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> reefTr (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> mineS  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> coalSm (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> sPh    (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> iceCov (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> hydPow (static_cast<std::size_t>(totalT), 0);

    // ---- TPI / TWI / ROUGHNESS / CURVATURE ----
    AOC_S16_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const std::size_t si = static_cast<std::size_t>(i);
            const int8_t myE = grid.elevation(i);
            int8_t maxE = myE;
            int8_t minE = myE;
            int32_t sumE = 0;
            int32_t nbCount = 0;
            int32_t lowerNb = 0;
            int32_t higherNb = 0;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                const int8_t nE = grid.elevation(nIdx);
                if (nE > maxE) { maxE = nE; }
                if (nE < minE) { minE = nE; }
                sumE += nE;
                ++nbCount;
                if (nE < myE) { ++lowerNb; }
                if (nE > myE) { ++higherNb; }
            }
            rough[si] = static_cast<uint8_t>(
                std::clamp((maxE - minE) * 60, 0, 255));
            if (nbCount > 0) {
                const float meanE = static_cast<float>(sumE)
                    / static_cast<float>(nbCount);
                const float diff = static_cast<float>(myE) - meanE;
                if (diff > 0.5f)        { tpi[si] = 3; }
                else if (diff < -0.5f)  { tpi[si] = 1; }
                else if (rough[si] > 30){ tpi[si] = 2; }
                else                    { tpi[si] = 0; }
            }
            if (lowerNb >= 4)        { curv[si] = 2; }
            else if (higherNb >= 4)  { curv[si] = 1; }
            else                     { curv[si] = 0; }
            {
                int32_t w = 30;
                if (curv[si] == 1) { w += 100; }
                if (rough[si] < 40) { w += 60; }
                if (grid.terrain(i) != TerrainType::Mountain
                    && grid.terrain(i) != TerrainType::Ocean
                    && grid.terrain(i) != TerrainType::ShallowWater) {
                    twi[si] = static_cast<uint8_t>(
                        std::clamp(w, 0, 255));
                }
            }
        }
    }

    // ---- DRAINAGE BASIN AREA + RIVER DISCHARGE ----
    std::vector<int32_t> basinAccum(
        static_cast<std::size_t>(totalT), 1);
    for (int32_t pass = 0; pass < 8; ++pass) {
        for (int32_t i = 0; i < totalT; ++i) {
            const auto& fd = grid.flowDir();
            const std::size_t si = static_cast<std::size_t>(i);
            if (fd.size() <= si || fd[si] == 0xFFu) { continue; }
            const int32_t row = i / width;
            const int32_t col = i % width;
            int32_t nIdx;
            if (!nb(col, row, fd[si], nIdx)) { continue; }
            basinAccum[static_cast<std::size_t>(nIdx)] +=
                basinAccum[si];
        }
    }
    for (int32_t i = 0; i < totalT; ++i) {
        const int32_t a = basinAccum[
            static_cast<std::size_t>(i)];
        const float la = std::log2(static_cast<float>(std::max(1, a)));
        drainA[static_cast<std::size_t>(i)] = static_cast<uint8_t>(
            std::clamp(la * 18.0f, 0.0f, 255.0f));
        if (grid.riverEdges(i) != 0) {
            rivDisc[static_cast<std::size_t>(i)] =
                drainA[static_cast<std::size_t>(i)];
        }
    }

    // ---- WATERSHED ID ----
    for (int32_t i = 0; i < totalT; ++i) {
        const TerrainType t = grid.terrain(i);
        if (t == TerrainType::Ocean
            || t == TerrainType::ShallowWater) {
            continue;
        }
        int32_t cur = i;
        for (int32_t step = 0; step < 50; ++step) {
            const auto& fd = grid.flowDir();
            const std::size_t scur = static_cast<std::size_t>(cur);
            if (fd.size() <= scur || fd[scur] == 0xFFu) { break; }
            const int32_t row = cur / width;
            const int32_t col = cur % width;
            int32_t nIdx;
            if (!nb(col, row, fd[scur], nIdx)) { break; }
            cur = nIdx;
            const TerrainType nt = grid.terrain(cur);
            if (nt == TerrainType::Ocean
                || nt == TerrainType::ShallowWater) {
                break;
            }
        }
        const uint32_t h = static_cast<uint32_t>(cur)
            * 2654435761u;
        wsId[static_cast<std::size_t>(i)] = static_cast<uint8_t>(
            1 + ((h >> 16) % 254));
    }

    // ---- PER-LIVESTOCK SUITABILITY ----
    AOC_S16_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const std::size_t si = static_cast<std::size_t>(i);
            const TerrainType t = grid.terrain(i);
            const FeatureType f = grid.feature(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                continue;
            }
            const float fert = (si < soilFert.size())
                ? soilFert[si] : 0.5f;
            if ((t == TerrainType::Grassland || t == TerrainType::Plains)
                && lat > 0.20f && lat < 0.55f) {
                livestockS[0][si] = static_cast<uint8_t>(
                    std::clamp(fert * 220.0f + 30.0f, 0.0f, 255.0f));
            }
            if ((f == FeatureType::Forest || t == TerrainType::Grassland)
                && lat < 0.55f) {
                livestockS[1][si] = static_cast<uint8_t>(
                    std::clamp(fert * 180.0f + 50.0f, 0.0f, 255.0f));
            }
            if (f == FeatureType::Hills
                && lat > 0.30f && lat < 0.60f) {
                livestockS[2][si] = static_cast<uint8_t>(
                    std::clamp(fert * 200.0f + 40.0f, 0.0f, 255.0f));
            }
            if (t == TerrainType::Plains
                && lat > 0.30f && lat < 0.55f
                && f == FeatureType::None) {
                livestockS[3][si] = static_cast<uint8_t>(
                    std::clamp(fert * 200.0f + 50.0f, 0.0f, 255.0f));
            }
            if (f == FeatureType::Hills
                || t == TerrainType::Mountain) {
                livestockS[4][si] = static_cast<uint8_t>(
                    std::clamp(fert * 150.0f + 60.0f, 0.0f, 255.0f));
            }
            if (t == TerrainType::Plains
                || t == TerrainType::Grassland) {
                livestockS[5][si] = static_cast<uint8_t>(
                    std::clamp(fert * 180.0f + 50.0f, 0.0f, 255.0f));
            }
        }
    }

    // ---- ACTIVE / INACTIVE FAULT ----
    AOC_S16_PARALLEL_FOR_ROWS
    for (int32_t i = 0; i < totalT; ++i) {
        const std::size_t si = static_cast<std::size_t>(i);
        const auto& sh = grid.seismicHazard();
        if (sh.size() <= si) { continue; }
        const uint8_t sev = sh[si] & 0x07;
        if (sev >= 2) {
            fault[si] = 1;
        } else {
            const auto& rk = grid.rockType();
            if (rk.size() > si && rk[si] == 3) {
                fault[si] = 2;
            }
        }
    }

    // ---- REEF TERRACES ----
    AOC_S16_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        if (lat > 0.35f) { continue; }
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const std::size_t si = static_cast<std::size_t>(i);
            const TerrainType t = grid.terrain(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                continue;
            }
            const int8_t elev = grid.elevation(i);
            if (elev < 1 || elev > 2) { continue; }
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
            if (nearWater) {
                reefTr[si] = static_cast<uint8_t>(elev * 80);
            }
        }
    }

    // ---- MINE SUITABILITY ----
    AOC_S16_PARALLEL_FOR_ROWS
    for (int32_t i = 0; i < totalT; ++i) {
        const std::size_t si = static_cast<std::size_t>(i);
        if (!grid.resource(i).isValid()) { continue; }
        uint8_t bits = 0;
        const auto& sl = grid.slopeAngle();
        if (sl.size() > si) {
            if (sl[si] < 100) { bits |= 0x01; }
            if (sl[si] >= 60) { bits |= 0x02; }
        } else {
            bits = 0x03;
        }
        mineS[si] = bits;
    }

    // ---- COAL SEAM THICKNESS ----
    AOC_S16_PARALLEL_FOR_ROWS
    for (int32_t i = 0; i < totalT; ++i) {
        const std::size_t si = static_cast<std::size_t>(i);
        const auto& res = grid.resource(i);
        if (res.value != aoc::sim::goods::COAL) { continue; }
        int32_t thick = 60;
        if (i < static_cast<int32_t>(sediment.size())) {
            thick += static_cast<int32_t>(
                sediment[si] * 600.0f);
        }
        const auto& ages = grid.crustAgeTile();
        if (ages.size() > si && ages[si] > 80.0f) {
            thick += 40;
        }
        coalSm[si] = static_cast<uint8_t>(
            std::clamp(thick, 0, 255));
    }

    // ---- SOIL pH ----
    AOC_S16_PARALLEL_FOR_ROWS
    for (int32_t i = 0; i < totalT; ++i) {
        const std::size_t si = static_cast<std::size_t>(i);
        const auto& so = grid.soilOrder();
        if (so.size() <= si) { continue; }
        uint8_t ph = 0;
        switch (so[si]) {
            case 4:  ph = 75;  break;
            case 5:  ph = 75;  break;
            case 7:  ph = 25;  break;
            case 11: ph = 25;  break;
            case 3:  ph = 130; break;
            case 6:  ph = 140; break;
            case 8:  ph = 200; break;
            case 9:  ph = 175; break;
            case 10: ph = 100; break;
            case 12: ph = 130; break;
            case 1:  ph = 130; break;
            case 2:  ph = 130; break;
            default: ph = 130; break;
        }
        sPh[si] = ph;
    }

    // ---- ICE-COVER DURATION ----
    AOC_S16_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const std::size_t si = static_cast<std::size_t>(i);
            if (lakeFlag[si] == 0) { continue; }
            int32_t months = 0;
            if (lat > 0.85f)      { months = 12; }
            else if (lat > 0.70f) { months = 8;  }
            else if (lat > 0.55f) { months = 5;  }
            else if (lat > 0.40f) { months = 2;  }
            iceCov[si] = static_cast<uint8_t>(months * 21);
        }
    }

    // ---- HYDROPOWER CAPACITY ----
    AOC_S16_PARALLEL_FOR_ROWS
    for (int32_t i = 0; i < totalT; ++i) {
        const std::size_t si = static_cast<std::size_t>(i);
        if (grid.riverEdges(i) == 0) { continue; }
        int32_t cap = 0;
        const auto& sl = grid.slopeAngle();
        if (sl.size() > si) { cap += static_cast<int32_t>(sl[si]); }
        if (rivDisc[si] > 0) { cap += rivDisc[si]; }
        cap = cap / 2;
        hydPow[si] = static_cast<uint8_t>(
            std::clamp(cap, 0, 255));
    }

    grid.setTopoPositionIndex(std::move(tpi));
    grid.setTopoWetnessIndex(std::move(twi));
    grid.setRoughness(std::move(rough));
    grid.setCurvature(std::move(curv));
    grid.setRiverDischarge(std::move(rivDisc));
    grid.setDrainageBasinArea(std::move(drainA));
    grid.setWatershedId(std::move(wsId));
    for (int32_t k = 0; k < 6; ++k) {
        grid.setLivestockSuit(k, std::move(livestockS[k]));
    }
    grid.setFaultTrace(std::move(fault));
    grid.setReefTerrace(std::move(reefTr));
    grid.setMineSuitability(std::move(mineS));
    grid.setCoalSeamThickness(std::move(coalSm));
    grid.setSoilPh(std::move(sPh));
    grid.setIceCoverDuration(std::move(iceCov));
    grid.setHydropowerCapacity(std::move(hydPow));
}

} // namespace aoc::map::gen
