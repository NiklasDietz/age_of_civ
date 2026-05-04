/**
 * @file Session4.cpp
 * @brief SESSION 4 implementation.
 */

#include "aoc/map/gen/BiomeSubtypes.hpp"

#include "aoc/core/Random.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _OPENMP
#  define AOC_S4_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_S4_PARALLEL_FOR_ROWS
#endif

namespace aoc::map::gen {

void runBiomeSubtypes(const HexGrid& grid, const BiomeSubtypesInputs& in,
                 BiomeSubtypesOutputs& out) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    const bool cylSim    = in.cylindrical;

    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylSim, col, row, dir, outIdx);
    };

    const std::vector<float>&    sediment      = *in.sediment;
    const std::vector<uint8_t>&  permafrost    = *in.permafrost;
    const std::vector<uint8_t>&  lakeFlag      = *in.lakeFlag;
    const std::vector<uint8_t>&  volcanism     = *in.volcanism;
    const std::vector<uint8_t>&  hazard        = *in.hazard;
    const std::vector<uint8_t>&  upwelling     = *in.upwelling;
    const std::vector<uint8_t>&  climateHazard = *in.climateHazard;
    const std::vector<uint8_t>&  oceanZone     = *in.oceanZone;
    const std::vector<float>&    cloudCover    = *in.cloudCover;

    out.natHazard.assign(static_cast<std::size_t>(totalT), 0);
    out.bSub     .assign(static_cast<std::size_t>(totalT), 0);
    out.marineD  .assign(static_cast<std::size_t>(totalT), 0);
    out.wildlife .assign(static_cast<std::size_t>(totalT), 0);
    out.disease  .assign(static_cast<std::size_t>(totalT), 0);
    out.windE    .assign(static_cast<std::size_t>(totalT), 0);
    out.solarE   .assign(static_cast<std::size_t>(totalT), 0);
    out.hydroE   .assign(static_cast<std::size_t>(totalT), 0);
    out.geoE     .assign(static_cast<std::size_t>(totalT), 0);
    out.tidalE   .assign(static_cast<std::size_t>(totalT), 0);
    out.waveE    .assign(static_cast<std::size_t>(totalT), 0);
    out.atmExtras.assign(static_cast<std::size_t>(totalT), 0);
    out.hydExtras.assign(static_cast<std::size_t>(totalT), 0);
    out.eventMrk .assign(static_cast<std::size_t>(totalT), 0);

    std::vector<uint16_t>& natHazard = out.natHazard;
    std::vector<uint8_t>&  bSub      = out.bSub;
    std::vector<uint8_t>&  marineD   = out.marineD;
    std::vector<uint8_t>&  wildlife  = out.wildlife;
    std::vector<uint8_t>&  disease   = out.disease;
    std::vector<uint8_t>&  windE     = out.windE;
    std::vector<uint8_t>&  solarE    = out.solarE;
    std::vector<uint8_t>&  hydroE    = out.hydroE;
    std::vector<uint8_t>&  geoE      = out.geoE;
    std::vector<uint8_t>&  tidalE    = out.tidalE;
    std::vector<uint8_t>&  waveE     = out.waveE;
    std::vector<uint8_t>&  atmExtras = out.atmExtras;
    std::vector<uint8_t>&  hydExtras = out.hydExtras;
    std::vector<uint8_t>&  eventMrk  = out.eventMrk;

    // ---- WP1: NATURAL HAZARDS ----
    AOC_S4_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            const FeatureType f = grid.feature(i);
            if (t == TerrainType::Ocean) { continue; }
            uint16_t haz = 0;
            if ((t == TerrainType::Plains
                 || t == TerrainType::Grassland
                 || t == TerrainType::Desert)
                && (f == FeatureType::Forest
                    || f == FeatureType::Jungle
                    || t == TerrainType::Plains)) {
                if (lat > 0.10f && lat < 0.55f) { haz |= 0x0001; }
            }
            const int8_t elev = grid.elevation(i);
            if (elev <= 0
                && grid.riverEdges(i) != 0
                && (t == TerrainType::Grassland
                    || f == FeatureType::Floodplains
                    || f == FeatureType::Marsh)) {
                haz |= 0x0002;
            }
            if (t == TerrainType::Desert
                || (t == TerrainType::Plains && lat < 0.30f)) {
                haz |= 0x0004;
            }
            if (t == TerrainType::Mountain && lat > 0.40f) {
                haz |= 0x0008;
            }
            if (f == FeatureType::Hills
                && i < static_cast<int32_t>(sediment.size())
                && sediment[static_cast<std::size_t>(i)] > 0.02f) {
                bool nearMtn = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nb(col, row, d, nIdx)) { continue; }
                    if (grid.terrain(nIdx) == TerrainType::Mountain) {
                        nearMtn = true; break;
                    }
                }
                if (nearMtn) { haz |= 0x0010; }
            }
            bool nearVolc = false;
            for (int32_t dr = -6; dr <= 6 && !nearVolc; ++dr) {
                const int32_t rr = row + dr;
                if (rr < 0 || rr >= height) { continue; }
                for (int32_t dc = -6; dc <= 6 && !nearVolc; ++dc) {
                    int32_t cc = col + dc;
                    if (cylSim) {
                        if (cc < 0)        { cc += width; }
                        if (cc >= width)   { cc -= width; }
                    } else if (cc < 0 || cc >= width) { continue; }
                    const int32_t nIdx = rr * width + cc;
                    const uint8_t v =
                        volcanism[static_cast<std::size_t>(nIdx)];
                    if (v == 1 || v == 3) { nearVolc = true; }
                }
            }
            if (nearVolc) { haz |= 0x0020; }
            if (t == TerrainType::Mountain
                && f == FeatureType::Ice) {
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nb(col, row, d, nIdx)) { continue; }
                    if (grid.riverEdges(nIdx) != 0) {
                        haz |= 0x0040; break;
                    }
                }
            }
            const auto& rk = grid.rockType();
            if (!rk.empty()
                && i < static_cast<int32_t>(rk.size())
                && rk[static_cast<std::size_t>(i)] == 5) {
                haz |= 0x0080;
            }
            if ((climateHazard[static_cast<std::size_t>(i)] & 0x01) != 0) {
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
                if (nearWater) { haz |= 0x0100; }
            }
            if (t == TerrainType::Desert
                && lat > 0.10f && lat < 0.40f) {
                haz |= 0x0200;
            }
            natHazard[static_cast<std::size_t>(i)] = haz;
        }
    }

    // ---- WP2: BIOME SUBTYPES ----
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            const FeatureType f = grid.feature(i);
            uint8_t sub = 0;
            if (lat > 0.30f && lat < 0.45f
                && t == TerrainType::Plains) {
                bool westCoast = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nb(col, row, d, nIdx)) { continue; }
                    const TerrainType nt = grid.terrain(nIdx);
                    if ((nt == TerrainType::Ocean
                         || nt == TerrainType::ShallowWater)
                        && (nIdx % width) < (col % width)) {
                        westCoast = true; break;
                    }
                }
                if (westCoast) { sub = 1; }
            }
            if (lat < 0.20f
                && f == FeatureType::Hills
                && (grid.terrain(i) == TerrainType::Grassland)) {
                sub = 2;
            }
            if (lat > 0.40f && lat < 0.70f
                && f == FeatureType::Forest) {
                bool westCoast = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nb(col, row, d, nIdx)) { continue; }
                    const TerrainType nt = grid.terrain(nIdx);
                    if ((nt == TerrainType::Ocean
                         || nt == TerrainType::ShallowWater)
                        && (nIdx % width) < (col % width)) {
                        westCoast = true; break;
                    }
                }
                if (westCoast) { sub = 3; }
            }
            if (lat < 0.25f
                && (f == FeatureType::Marsh
                    || f == FeatureType::Floodplains)) {
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
                if (nearWater) { sub = 4; }
            }
            if (lat >= 0.55f && lat <= 0.78f
                && f == FeatureType::Forest) {
                sub = 5;
            }
            if (t == TerrainType::Tundra
                && grid.elevation(i) >= 1) {
                sub = 6;
            }
            if (t == TerrainType::Snow && lat > 0.85f) {
                sub = 7;
            }
            if (t == TerrainType::Desert && lat > 0.45f) {
                sub = 8;
            }
            if (t == TerrainType::Plains
                && lat > 0.30f && lat < 0.60f
                && f == FeatureType::None) {
                sub = 9;
            }
            if (t == TerrainType::Grassland
                && lat > 0.35f && lat < 0.55f
                && f == FeatureType::None) {
                sub = 10;
            }
            bSub[static_cast<std::size_t>(i)] = sub;
        }
    }

    // ---- WP3: MARINE DEPTH ZONATION ----
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            if (t != TerrainType::Ocean
                && t != TerrainType::ShallowWater) {
                continue;
            }
            if (lakeFlag[static_cast<std::size_t>(i)] != 0) { continue; }
            int32_t dist = 99;
            for (int32_t r = 1; r <= 8 && dist == 99; ++r) {
                for (int32_t dr = -r; dr <= r && dist == 99; ++dr) {
                    const int32_t rr = row + dr;
                    if (rr < 0 || rr >= height) { continue; }
                    for (int32_t dc = -r; dc <= r && dist == 99; ++dc) {
                        if (std::abs(dc) != r && std::abs(dr) != r) {
                            continue;
                        }
                        int32_t cc = col + dc;
                        if (cylSim) {
                            if (cc < 0)        { cc += width; }
                            if (cc >= width)   { cc -= width; }
                        } else if (cc < 0 || cc >= width) { continue; }
                        const TerrainType nt =
                            grid.terrain(rr * width + cc);
                        if (nt != TerrainType::Ocean
                            && nt != TerrainType::ShallowWater) {
                            dist = r;
                        }
                    }
                }
            }
            uint8_t zone = 4;
            if (dist <= 1)      { zone = 1; }
            else if (dist <= 3) { zone = 2; }
            else if (dist <= 5) { zone = 3; }
            if (hazard[static_cast<std::size_t>(i)] >= 3) { zone = 5; }
            marineD[static_cast<std::size_t>(i)] = zone;
        }
    }
    for (int32_t i = 0; i < totalT; ++i) {
        if (volcanism[static_cast<std::size_t>(i)] == 2
            && (grid.terrain(i) == TerrainType::ShallowWater
                || grid.terrain(i) == TerrainType::Ocean)) {
            bSub[static_cast<std::size_t>(i)] = 11;
        }
    }
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        if (lat < 0.40f || lat > 0.65f) { continue; }
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            if (grid.terrain(i) != TerrainType::ShallowWater) {
                continue;
            }
            if (upwelling[static_cast<std::size_t>(i)] == 1) {
                bSub[static_cast<std::size_t>(i)] = 12;
            }
        }
    }
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            if (grid.terrain(i) != TerrainType::ShallowWater) {
                continue;
            }
            bool riverMouth = false;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                if (grid.riverEdges(nIdx) != 0
                    && grid.terrain(nIdx) != TerrainType::ShallowWater
                    && grid.terrain(nIdx) != TerrainType::Ocean) {
                    riverMouth = true; break;
                }
            }
            if (riverMouth) {
                bSub[static_cast<std::size_t>(i)] = 13;
            }
        }
    }
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        if (lat > 0.30f) { continue; }
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            if (grid.terrain(i) != TerrainType::ShallowWater) {
                continue;
            }
            if (marineD[static_cast<std::size_t>(i)] == 1
                && bSub[static_cast<std::size_t>(i)] == 0) {
                bSub[static_cast<std::size_t>(i)] = 14;
            }
        }
    }

    // ---- WP4: WILDLIFE ----
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            const FeatureType f = grid.feature(i);
            uint8_t w = 0;
            if ((t == TerrainType::Plains
                 || t == TerrainType::Grassland)
                && lat < 0.40f
                && f == FeatureType::None) {
                w = 1;
            }
            if (f == FeatureType::Forest && lat < 0.55f) {
                w = 1;
            }
            if ((t == TerrainType::Tundra
                 || (f == FeatureType::Forest && lat > 0.55f))) {
                w = 2;
            }
            if ((t == TerrainType::Ocean
                 || t == TerrainType::ShallowWater)
                && lat > 0.55f) {
                w = 3;
            }
            if (lat > 0.40f
                && grid.riverEdges(i) != 0
                && (t == TerrainType::Plains
                    || t == TerrainType::Grassland)) {
                w = 4;
            }
            if ((f == FeatureType::Marsh
                 || f == FeatureType::Floodplains)
                && lat > 0.30f && lat < 0.60f) {
                w = 5;
            }
            wildlife[static_cast<std::size_t>(i)] = w;
        }
    }

    // ---- WP5: DISEASE ZONES ----
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            const FeatureType f = grid.feature(i);
            uint8_t d = 0;
            if (lat < 0.25f
                && (f == FeatureType::Marsh
                    || f == FeatureType::Floodplains
                    || f == FeatureType::Jungle)) {
                d |= 0x01;
            }
            if (lat < 0.20f && f == FeatureType::Jungle) {
                d |= 0x02;
            }
            if (lat < 0.30f && t == TerrainType::Plains) {
                d |= 0x04;
            }
            if (lat > 0.30f && lat < 0.60f
                && f == FeatureType::Hills) {
                d |= 0x08;
            }
            if (t == TerrainType::Plains
                && lat > 0.40f && lat < 0.55f) {
                d |= 0x10;
            }
            if (lat < 0.30f
                && f == FeatureType::Floodplains) {
                d |= 0x20;
            }
            disease[static_cast<std::size_t>(i)] = d;
        }
    }

    // ---- WP6: RENEWABLE ENERGY POTENTIALS ----
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            uint8_t wind = 30;
            if ((climateHazard[static_cast<std::size_t>(i)] & 0x08) != 0) {
                wind = 200;
            } else if ((climateHazard[static_cast<std::size_t>(i)] & 0x04) != 0) {
                wind = 170;
            } else if (lat > 0.40f && lat < 0.65f
                && (t == TerrainType::Plains
                    || t == TerrainType::Grassland)) {
                wind = 100;
            }
            windE[static_cast<std::size_t>(i)] = wind;
            float solarF = 1.0f - lat;
            if (i < static_cast<int32_t>(cloudCover.size())) {
                solarF *= (1.0f - cloudCover[static_cast<std::size_t>(i)]);
            }
            if (t == TerrainType::Desert) { solarF *= 1.4f; }
            solarE[static_cast<std::size_t>(i)] = static_cast<uint8_t>(
                std::clamp(solarF * 255.0f, 0.0f, 255.0f));
            uint8_t hydro = 0;
            if (grid.riverEdges(i) != 0) {
                const int8_t myE = grid.elevation(i);
                int8_t dropMax = 0;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nb(col, row, d, nIdx)) { continue; }
                    const int8_t nE = grid.elevation(nIdx);
                    const int8_t drop = static_cast<int8_t>(myE - nE);
                    if (drop > dropMax) { dropMax = drop; }
                }
                hydro = static_cast<uint8_t>(50 + dropMax * 50);
            }
            hydroE[static_cast<std::size_t>(i)] = hydro;
            uint8_t geo = 0;
            const uint8_t v = volcanism[static_cast<std::size_t>(i)];
            if (v == 5)      { geo = 220; }
            else if (v == 1
                  || v == 2
                  || v == 3
                  || v == 4) { geo = 150; }
            geoE[static_cast<std::size_t>(i)] = geo;
            const uint8_t oz = oceanZone[static_cast<std::size_t>(i)];
            const uint8_t tidalBin = oz & 0x03;
            tidalE[static_cast<std::size_t>(i)] =
                static_cast<uint8_t>(tidalBin * 80);
            uint8_t wave = 0;
            if ((t == TerrainType::Ocean
                 || t == TerrainType::ShallowWater)
                && (climateHazard[static_cast<std::size_t>(i)] & 0x04) != 0) {
                wave = 200;
            }
            waveE[static_cast<std::size_t>(i)] = wave;
        }
    }

    // ---- WP7: ATMOSPHERIC EXTRAS ----
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            uint8_t ax = 0;
            if (grid.feature(i) == FeatureType::Hills) {
                bool nearMtn = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nb(col, row, d, nIdx)) { continue; }
                    if (grid.terrain(nIdx) == TerrainType::Mountain) {
                        nearMtn = true; break;
                    }
                }
                if (nearMtn) { ax |= 0x01; }
            }
            if (grid.terrain(i) == TerrainType::Snow) {
                ax |= 0x02;
            }
            if (lat > 0.20f && lat < 0.35f
                && (grid.terrain(i) == TerrainType::Ocean
                    || grid.terrain(i) == TerrainType::ShallowWater)) {
                ax |= 0x04;
            }
            if (lat > 0.85f) { ax |= 0x08; }
            if (lat < 0.10f) { ax |= 0x10; }
            if (lat > 0.10f && lat < 0.40f
                && (grid.terrain(i) == TerrainType::Plains
                    || grid.terrain(i) == TerrainType::Grassland)) {
                ax |= 0x20;
            }
            atmExtras[static_cast<std::size_t>(i)] = ax;
        }
    }

    // ---- WP8: HYDROLOGICAL EXTRAS ----
    for (int32_t i = 0; i < totalT; ++i) {
        const TerrainType t = grid.terrain(i);
        const FeatureType f = grid.feature(i);
        uint8_t hx = 0;
        if (i < static_cast<int32_t>(sediment.size())
            && sediment[static_cast<std::size_t>(i)] > 0.04f) {
            hx |= 0x01;
        }
        const auto& rk2 = grid.rockType();
        if (!rk2.empty()
            && i < static_cast<int32_t>(rk2.size())
            && rk2[static_cast<std::size_t>(i)] == 5) {
            hx |= 0x02;
        }
        if (lakeFlag[static_cast<std::size_t>(i)] != 0) {
            const int32_t row = i / width;
            const int32_t col = i % width;
            bool volcAdjacent = false;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                if (volcanism[static_cast<std::size_t>(nIdx)] != 0) {
                    volcAdjacent = true; break;
                }
            }
            if (volcAdjacent) { hx |= 0x04; }
            const uint8_t oz = oceanZone[static_cast<std::size_t>(i)];
            const uint8_t salin = (oz >> 2) & 0x03;
            if (salin == 2)      { hx |= 0x40; }
            else                  { hx |= 0x80; }
        }
        if (lakeFlag[static_cast<std::size_t>(i)] != 0
            && permafrost[static_cast<std::size_t>(i)] != 0) {
            hx |= 0x10;
        }
        if (grid.riverEdges(i) != 0
            && (f == FeatureType::Marsh
                || f == FeatureType::Floodplains)
            && t != TerrainType::ShallowWater) {
            hx |= 0x20;
        }
        hydExtras[static_cast<std::size_t>(i)] = hx;
    }

    // ---- WP9: EVENT MARKERS ----
    {
        std::vector<int32_t> arcTiles;
        std::vector<int32_t> cratonTiles;
        for (int32_t i = 0; i < totalT; ++i) {
            if (volcanism[static_cast<std::size_t>(i)] == 1
                || volcanism[static_cast<std::size_t>(i)] == 3) {
                arcTiles.push_back(i);
            }
            const auto& ages = grid.crustAgeTile();
            if (i < static_cast<int32_t>(ages.size())
                && ages[static_cast<std::size_t>(i)] > 130.0f
                && grid.terrain(i) != TerrainType::Ocean) {
                cratonTiles.push_back(i);
            }
        }
        aoc::Random evRng(in.seed ^ 0xE7E7u);
        for (int32_t k = 0; k < 3 && !arcTiles.empty(); ++k) {
            const int32_t pick = evRng.nextInt(0,
                static_cast<int32_t>(arcTiles.size()) - 1);
            eventMrk[static_cast<std::size_t>(arcTiles[pick])] = 1;
            arcTiles.erase(arcTiles.begin()
                + static_cast<std::ptrdiff_t>(pick));
        }
        if (evRng.nextFloat(0.0f, 1.0f) < 0.30f) {
            for (std::size_t k = 0; k < eventMrk.size(); ++k) {
                if (eventMrk[k] == 1) {
                    eventMrk[k] = 3; break;
                }
            }
        }
        if (!cratonTiles.empty()) {
            const int32_t pick = evRng.nextInt(0,
                static_cast<int32_t>(cratonTiles.size()) - 1);
            eventMrk[static_cast<std::size_t>(cratonTiles[pick])] = 2;
        }
    }
}

} // namespace aoc::map::gen
