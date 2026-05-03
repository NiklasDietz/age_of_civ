/**
 * @file EarthSystem.cpp
 * @brief EARTH-SYSTEM POST-PASSES implementation.
 */

#include "aoc/map/gen/EarthSystem.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _OPENMP
#  define AOC_ES_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_ES_PARALLEL_FOR_ROWS
#endif

namespace aoc::map::gen {

void runEarthSystemPasses(HexGrid& grid, bool cylindrical,
                          const std::vector<float>& orogeny,
                          const std::vector<float>& sediment,
                          EarthSystemOutputs& out) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    const bool cylSim    = cylindrical;

    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylSim, col, row, dir, outIdx);
    };

    out.soilFert  .assign(static_cast<std::size_t>(totalT), 0.50f);
    out.volcanism .assign(static_cast<std::size_t>(totalT), 0);
    out.hazard    .assign(static_cast<std::size_t>(totalT), 0);
    out.permafrost.assign(static_cast<std::size_t>(totalT), 0);
    out.lakeFlag  .assign(static_cast<std::size_t>(totalT), 0);
    out.upwelling .assign(static_cast<std::size_t>(totalT), 0);

    std::vector<float>&   soilFert   = out.soilFert;
    std::vector<uint8_t>& volcanism  = out.volcanism;
    std::vector<uint8_t>& hazard     = out.hazard;
    std::vector<uint8_t>& permafrost = out.permafrost;
    std::vector<uint8_t>& lakeFlag   = out.lakeFlag;
    std::vector<uint8_t>& upwelling  = out.upwelling;

    const auto& mots = grid.plateMotions();
    const auto& cens = grid.plateCenters();
    const auto& lfrs = grid.plateLandFrac();

    // ---- LAKES (positive generation) ----
    for (int32_t i = 0; i < totalT; ++i) {
        const TerrainType t = grid.terrain(i);
        if (t == TerrainType::Ocean
            || t == TerrainType::ShallowWater
            || t == TerrainType::Mountain) {
            continue;
        }
        const float oro = orogeny[static_cast<std::size_t>(i)];
        if (oro < -0.06f) {
            grid.setTerrain(i, TerrainType::ShallowWater);
            grid.setElevation(i, -1);
            grid.setFeature(i, FeatureType::None);
            lakeFlag[static_cast<std::size_t>(i)] = 1;
        }
    }

    // ---- VOLCANISM markers ----
    const auto& hsList = grid.hotspots();
    for (const auto& h : hsList) {
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const float wx = (static_cast<float>(col) + 0.5f)
                    / static_cast<float>(width);
                const float wy = (static_cast<float>(row) + 0.5f)
                    / static_cast<float>(height);
                float dx = wx - h.first;
                float dy = wy - h.second;
                if (cylSim) {
                    if (dx >  0.5f) { dx -= 1.0f; }
                    if (dx < -0.5f) { dx += 1.0f; }
                }
                if (dx * dx + dy * dy < 0.0025f * 0.0025f * 100.0f) {
                    const int32_t i = row * width + col;
                    volcanism[static_cast<std::size_t>(i)] = 2;
                }
            }
        }
    }
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            if (grid.terrain(idx) == TerrainType::Ocean
                || grid.terrain(idx) == TerrainType::ShallowWater) {
                continue;
            }
            const uint8_t myPid = grid.plateId(idx);
            if (myPid == 0xFFu || myPid >= mots.size()) { continue; }
            if (myPid >= lfrs.size() || lfrs[myPid] < 0.40f) { continue; }
            bool isArc = false;
            for (int32_t d = 0; d < 6 && !isArc; ++d) {
                int32_t cur = idx;
                int32_t cc = col, rr = row;
                for (int32_t step = 0; step < 4 && !isArc; ++step) {
                    int32_t nIdx;
                    if (!nb(cc, rr, d, nIdx)) { break; }
                    cur = nIdx;
                    cc = nIdx % width;
                    rr = nIdx / width;
                    const uint8_t nPid = grid.plateId(cur);
                    if (nPid == 0xFFu || nPid == myPid) { continue; }
                    if (nPid >= mots.size()
                        || nPid >= lfrs.size()) { continue; }
                    if (lfrs[nPid] >= 0.40f) { continue; }
                    float bnx = cens[nPid].first  - cens[myPid].first;
                    float bny = cens[nPid].second - cens[myPid].second;
                    if (cylSim) {
                        if (bnx >  0.5f) { bnx -= 1.0f; }
                        if (bnx < -0.5f) { bnx += 1.0f; }
                    }
                    const float bnLen = std::sqrt(bnx * bnx + bny * bny);
                    if (bnLen < 1e-4f) { break; }
                    bnx /= bnLen; bny /= bnLen;
                    const float relVx = mots[myPid].first - mots[nPid].first;
                    const float relVy = mots[myPid].second - mots[nPid].second;
                    const float closing = relVx * bnx + relVy * bny;
                    if (closing > 0.04f) { isArc = true; }
                    break;
                }
            }
            if (isArc) {
                if (volcanism[static_cast<std::size_t>(idx)] == 0) {
                    volcanism[static_cast<std::size_t>(idx)] = 1;
                }
            }
        }
    }

    // ---- SEISMIC HAZARD ----
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            const uint8_t myPid = grid.plateId(idx);
            if (myPid == 0xFFu || myPid >= mots.size()) { continue; }
            uint8_t worst = 0;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                const uint8_t nPid = grid.plateId(nIdx);
                if (nPid == 0xFFu || nPid == myPid) { continue; }
                if (nPid >= mots.size()) { continue; }
                float bnx = cens[nPid].first  - cens[myPid].first;
                float bny = cens[nPid].second - cens[myPid].second;
                if (cylSim) {
                    if (bnx >  0.5f) { bnx -= 1.0f; }
                    if (bnx < -0.5f) { bnx += 1.0f; }
                }
                const float bnLen = std::sqrt(bnx * bnx + bny * bny);
                if (bnLen < 1e-4f) { continue; }
                bnx /= bnLen; bny /= bnLen;
                const float relVx = mots[myPid].first - mots[nPid].first;
                const float relVy = mots[myPid].second - mots[nPid].second;
                const float normProj = relVx * bnx + relVy * bny;
                const float tangProj = -relVx * bny + relVy * bnx;
                const float aN = std::abs(normProj);
                const float aT = std::abs(tangProj);
                uint8_t cur = 1;
                if (aN > aT && aN > 0.04f) {
                    cur = (normProj > 0.0f) ? 3 : 2;
                } else if (aT > 0.04f) {
                    cur = 2;
                }
                if (cur > worst) { worst = cur; }
            }
            hazard[static_cast<std::size_t>(idx)] = worst;
        }
    }

    // ---- PERMAFROST ----
    for (int32_t i = 0; i < totalT; ++i) {
        const TerrainType t = grid.terrain(i);
        if (t == TerrainType::Tundra
            || t == TerrainType::Snow) {
            permafrost[static_cast<std::size_t>(i)] = 1;
        }
    }

    // ---- MOUNTAIN GLACIERS ----
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        if (lat < 0.55f) { continue; }
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            if (grid.terrain(idx) == TerrainType::Mountain
                && grid.feature(idx) == FeatureType::None) {
                grid.setFeature(idx, FeatureType::Ice);
            }
        }
    }

    // ---- COASTAL UPWELLING ----
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        if (lat < 0.10f || lat > 0.60f) { continue; }
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            if (grid.terrain(idx) != TerrainType::ShallowWater) {
                continue;
            }
            if (lakeFlag[static_cast<std::size_t>(idx)] != 0) { continue; }
            int32_t ec = col + 1;
            if (cylSim) {
                if (ec >= width) { ec -= width; }
            } else if (ec >= width) { continue; }
            const int32_t eIdx = row * width + ec;
            const TerrainType te = grid.terrain(eIdx);
            if (te != TerrainType::Ocean
                && te != TerrainType::ShallowWater) {
                upwelling[static_cast<std::size_t>(idx)] = 1;
                if (!grid.resource(idx).isValid()) {
                    grid.setResource(idx,
                        ResourceId{aoc::sim::goods::FISH});
                }
            }
        }
    }

    // ---- RIVER DELTAS ----
    for (int32_t i = 0; i < totalT; ++i) {
        if (grid.riverEdges(i) == 0) { continue; }
        const TerrainType t = grid.terrain(i);
        if (t != TerrainType::Plains
            && t != TerrainType::Grassland) { continue; }
        const int32_t row = i / width;
        const int32_t col = i % width;
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
        if (nearWater
            && grid.feature(i) == FeatureType::None) {
            grid.setFeature(i, FeatureType::Floodplains);
        }
    }

    // ---- SALT FLATS / PLAYAS ----
    for (int32_t i = 0; i < totalT; ++i) {
        if (grid.terrain(i) != TerrainType::Desert) { continue; }
        if (grid.resource(i).isValid()) { continue; }
        const int32_t row = i / width;
        const int32_t col = i % width;
        bool nearLake = false;
        for (int32_t d = 0; d < 6; ++d) {
            int32_t nIdx;
            if (!nb(col, row, d, nIdx)) { continue; }
            if (lakeFlag[static_cast<std::size_t>(nIdx)] != 0) {
                nearLake = true; break;
            }
        }
        if (nearLake) {
            grid.setResource(i, ResourceId{aoc::sim::goods::SALT});
        }
    }

    // ---- SOIL FERTILITY ----
    for (int32_t i = 0; i < totalT; ++i) {
        const TerrainType t = grid.terrain(i);
        const FeatureType f = grid.feature(i);
        float fert = 0.50f;
        switch (t) {
            case TerrainType::Grassland: fert = 0.65f; break;
            case TerrainType::Plains:    fert = 0.55f; break;
            case TerrainType::Desert:    fert = 0.20f; break;
            case TerrainType::Tundra:    fert = 0.20f; break;
            case TerrainType::Snow:      fert = 0.05f; break;
            case TerrainType::Mountain:  fert = 0.30f; break;
            default: break;
        }
        if (volcanism[static_cast<std::size_t>(i)] != 0) {
            fert += 0.30f;
        }
        if (f == FeatureType::Floodplains) {
            fert += 0.30f;
        }
        if (f == FeatureType::Jungle) {
            fert -= 0.20f;
        }
        if (f == FeatureType::Marsh) {
            fert += 0.10f;
        }
        const auto& ages = grid.crustAgeTile();
        if (i < static_cast<int32_t>(ages.size())) {
            const float a = ages[static_cast<std::size_t>(i)];
            if (t == TerrainType::Grassland && a > 80.0f) {
                fert += 0.20f;
            }
        }
        const int32_t row = i / width;
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        if (lat > 0.40f && lat < 0.65f
            && (t == TerrainType::Plains
                || t == TerrainType::Grassland)) {
            fert += 0.15f;
        }
        if (permafrost[static_cast<std::size_t>(i)] != 0) {
            fert *= 0.4f;
        }
        soilFert[static_cast<std::size_t>(i)] =
            std::clamp(fert, 0.0f, 1.0f);
    }

    // ---- HOT SPRINGS / GEOTHERMAL ----
    for (int32_t i = 0; i < totalT; ++i) {
        if (volcanism[static_cast<std::size_t>(i)] == 0) { continue; }
        const int32_t row = i / width;
        const int32_t col = i % width;
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
        if (nearWater
            && volcanism[static_cast<std::size_t>(i)] != 5) {
            volcanism[static_cast<std::size_t>(i)] = 5;
        }
    }

    // ---- KARST topography ----
    const auto& rockTypeNow = grid.rockType();
    if (!rockTypeNow.empty()) {
        std::vector<uint8_t> rockUpd(rockTypeNow.begin(),
                                     rockTypeNow.end());
        const auto& ages2 = grid.crustAgeTile();
        for (int32_t i = 0; i < totalT; ++i) {
            if (grid.feature(i) != FeatureType::Hills) {
                continue;
            }
            if (rockUpd[static_cast<std::size_t>(i)] != 0) { continue; }
            if (i < static_cast<int32_t>(ages2.size())
                && ages2[static_cast<std::size_t>(i)] > 50.0f) {
                const int32_t row = i / width;
                const float ny = static_cast<float>(row)
                               / static_cast<float>(height);
                const float lat = 2.0f * std::abs(ny - 0.5f);
                if (lat < 0.55f) {
                    rockUpd[static_cast<std::size_t>(i)] = 5;
                }
            }
        }
        grid.setRockType(std::move(rockUpd));
    }

    // ---- INSELBERGS ----
    for (int32_t i = 0; i < totalT; ++i) {
        if (grid.terrain(i) != TerrainType::Mountain) {
            continue;
        }
        const int32_t row = i / width;
        const int32_t col = i % width;
        int32_t flatNb = 0;
        int32_t totalNb = 0;
        for (int32_t d = 0; d < 6; ++d) {
            int32_t nIdx;
            if (!nb(col, row, d, nIdx)) { continue; }
            ++totalNb;
            const TerrainType nt = grid.terrain(nIdx);
            if (nt == TerrainType::Plains
                || nt == TerrainType::Desert
                || nt == TerrainType::Grassland) {
                ++flatNb;
            }
        }
        if (totalNb >= 5 && flatNb >= totalNb - 1) {
            volcanism[static_cast<std::size_t>(i)] = 6;
        }
    }

    // ---- SAND DUNES ----
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        if (lat < 0.10f || lat > 0.40f) { continue; }
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            if (grid.terrain(i) != TerrainType::Desert) {
                continue;
            }
            if (volcanism[static_cast<std::size_t>(i)] == 0) {
                volcanism[static_cast<std::size_t>(i)] = 7;
            }
        }
    }

    // ---- TSUNAMI ZONES ----
    AOC_ES_PARALLEL_FOR_ROWS
    for (int32_t i = 0; i < totalT; ++i) {
        const TerrainType t = grid.terrain(i);
        if (t == TerrainType::Ocean
            || t == TerrainType::ShallowWater) {
            continue;
        }
        const int32_t row = i / width;
        const int32_t col = i % width;
        bool tsunami = false;
        for (int32_t dr = -4; dr <= 4 && !tsunami; ++dr) {
            const int32_t rr = row + dr;
            if (rr < 0 || rr >= height) { continue; }
            for (int32_t dc = -4; dc <= 4 && !tsunami; ++dc) {
                int32_t cc = col + dc;
                if (cylSim) {
                    if (cc < 0)        { cc += width; }
                    if (cc >= width)   { cc -= width; }
                } else if (cc < 0 || cc >= width) { continue; }
                const int32_t nIdx = rr * width + cc;
                const TerrainType nt = grid.terrain(nIdx);
                if (nt != TerrainType::Ocean
                    && nt != TerrainType::ShallowWater) {
                    continue;
                }
                if (hazard[static_cast<std::size_t>(nIdx)] >= 3) {
                    tsunami = true;
                }
            }
        }
        if (tsunami) {
            hazard[static_cast<std::size_t>(i)] |= 0x08;
        }
    }

    // ---- SEA ICE ----
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        if (lat < 0.85f) { continue; }
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            if (t != TerrainType::Ocean
                && t != TerrainType::ShallowWater) {
                continue;
            }
            if (grid.feature(i) == FeatureType::None) {
                grid.setFeature(i, FeatureType::Ice);
            }
        }
    }

    // ---- FJORDS ----
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        if (lat < 0.55f) { continue; }
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            if (grid.terrain(i) != TerrainType::ShallowWater) {
                continue;
            }
            bool nearMtn = false;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                if (grid.terrain(nIdx) == TerrainType::Mountain) {
                    nearMtn = true; break;
                }
            }
            if (nearMtn) {
                upwelling[static_cast<std::size_t>(i)] = 2;
            }
        }
    }

    // ---- TREELINE ----
    for (int32_t i = 0; i < totalT; ++i) {
        const TerrainType t = grid.terrain(i);
        const FeatureType f = grid.feature(i);
        if (t != TerrainType::Mountain) { continue; }
        if (f == FeatureType::Forest
            || f == FeatureType::Jungle) {
            const int32_t row = i / width;
            const float ny = static_cast<float>(row)
                           / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            if (lat > 0.50f
                && grid.feature(i) != FeatureType::Ice) {
                grid.setFeature(i, FeatureType::Ice);
            } else {
                grid.setFeature(i, FeatureType::None);
            }
        }
    }

    // ---- WETLANDS ----
    for (int32_t i = 0; i < totalT; ++i) {
        if (grid.feature(i) != FeatureType::None) { continue; }
        const TerrainType t = grid.terrain(i);
        if (t != TerrainType::Plains
            && t != TerrainType::Grassland) { continue; }
        if (grid.riverEdges(i) == 0) { continue; }
        const int32_t row = i / width;
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        if (lat > 0.40f && lat < 0.70f) {
            if (i < static_cast<int32_t>(sediment.size())
                && sediment[static_cast<std::size_t>(i)] > 0.05f) {
                grid.setFeature(i, FeatureType::Marsh);
            }
        }
    }
}

} // namespace aoc::map::gen
