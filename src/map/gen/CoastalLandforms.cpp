/**
 * @file Session12.cpp
 * @brief SESSION 12 implementation.
 */

#include "aoc/map/gen/CoastalLandforms.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _OPENMP
#  define AOC_S12_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_S12_PARALLEL_FOR_ROWS
#endif

namespace aoc::map::gen {

void runCoastalLandforms(HexGrid& grid, bool cylindrical,
                  const std::vector<uint8_t>& lakeFlag,
                  const std::vector<float>& orogeny) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    const bool cylSim    = cylindrical;

    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylSim, col, row, dir, outIdx);
    };

    std::vector<uint8_t> coastLF (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> rivReg  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> aridLF  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> tfType  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> lakeFX  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> drumDir (static_cast<std::size_t>(totalT), 0xFFu);
    std::vector<uint8_t> sutReact(static_cast<std::size_t>(totalT), 0);

    // ---- COASTAL LANDFORMS ----
    AOC_S12_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            int32_t waterNb = 0;
            int32_t landNb = 0;
            std::array<bool, 6> nbWater{};
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                const TerrainType nt = grid.terrain(nIdx);
                if (nt == TerrainType::Ocean
                    || nt == TerrainType::ShallowWater) {
                    ++waterNb;
                    nbWater[static_cast<std::size_t>(d)] = true;
                } else { ++landNb; }
            }
            if ((t != TerrainType::Ocean
                 && t != TerrainType::ShallowWater)
                && waterNb == 6) {
                coastLF[static_cast<std::size_t>(i)] = 1;
                continue;
            }
            if ((t != TerrainType::Ocean
                 && t != TerrainType::ShallowWater)
                && waterNb >= 4 && waterNb <= 5) {
                coastLF[static_cast<std::size_t>(i)] = 2;
                continue;
            }
            if ((t != TerrainType::Ocean
                 && t != TerrainType::ShallowWater)
                && waterNb == 3) {
                int32_t alternations = 0;
                for (int32_t d = 0; d < 6; ++d) {
                    const std::size_t a = static_cast<std::size_t>(d);
                    const std::size_t b = static_cast<std::size_t>((d + 1) % 6);
                    if (nbWater[a] != nbWater[b]) { ++alternations; }
                }
                if (alternations >= 4) {
                    coastLF[static_cast<std::size_t>(i)] = 7;
                }
            }
            if (t == TerrainType::ShallowWater) {
                if (lakeFlag[static_cast<std::size_t>(i)] != 0) {
                    continue;
                }
                if (landNb >= 4) {
                    coastLF[static_cast<std::size_t>(i)] = 5;
                    continue;
                }
                const auto& oz = grid.oceanZone();
                const std::size_t si = static_cast<std::size_t>(i);
                if (oz.size() > si && (oz[si] & 0x03) == 0
                    && landNb >= 1 && landNb <= 3) {
                    coastLF[static_cast<std::size_t>(i)] = 6;
                    continue;
                }
                if (landNb == 1 || landNb == 2) {
                    const auto& md = grid.marineDepth();
                    if (md.size() > si && md[si] == 1) {
                        coastLF[static_cast<std::size_t>(i)] = 3;
                    }
                }
                if (landNb == 2) {
                    for (int32_t d = 0; d < 3; ++d) {
                        const std::size_t a = static_cast<std::size_t>(d);
                        const std::size_t b = static_cast<std::size_t>(d + 3);
                        if (!nbWater[a] && !nbWater[b]) {
                            coastLF[static_cast<std::size_t>(i)] = 4;
                            break;
                        }
                    }
                }
            }
        }
    }

    // ---- RIVER REGIME ----
    AOC_S12_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            if (grid.riverEdges(i) == 0) { continue; }
            const TerrainType t = grid.terrain(i);
            bool nearGlacier = false;
            bool nearHighMtn = false;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                if (grid.terrain(nIdx) == TerrainType::Mountain) {
                    if (grid.feature(nIdx) == FeatureType::Ice) {
                        nearGlacier = true;
                    }
                    if (lat > 0.45f) { nearHighMtn = true; }
                }
            }
            uint8_t r = 1;
            if (nearGlacier) {
                r = 4;
            } else if (nearHighMtn) {
                r = 5;
            } else if (t == TerrainType::Desert) {
                r = 3;
            } else if (lat > 0.30f && lat < 0.45f) {
                r = 2;
            }
            rivReg[static_cast<std::size_t>(i)] = r;
        }
    }

    // ---- ARID EROSION LANDFORMS ----
    AOC_S12_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            const FeatureType f = grid.feature(i);
            if (t != TerrainType::Desert
                && t != TerrainType::Plains) { continue; }
            const int8_t myE = grid.elevation(i);
            int32_t lowerNb = 0;
            int32_t higherNb = 0;
            int32_t totalNb = 0;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                ++totalNb;
                const int8_t nE = grid.elevation(nIdx);
                if (nE < myE)      { ++lowerNb; }
                else if (nE > myE) { ++higherNb; }
            }
            if (myE >= 2 && lowerNb >= 5) {
                aridLF[static_cast<std::size_t>(i)] = 2;
            } else if (myE >= 2 && lowerNb >= 4 && lowerNb < 6) {
                aridLF[static_cast<std::size_t>(i)] = 1;
            } else if (myE >= 2 && lowerNb < 4 && totalNb >= 4) {
                aridLF[static_cast<std::size_t>(i)] = 3;
            } else if (t == TerrainType::Desert
                && f == FeatureType::None
                && myE == 1
                && lowerNb >= 2 && lowerNb <= 4) {
                aridLF[static_cast<std::size_t>(i)] = 4;
            } else if (t == TerrainType::Desert
                && f == FeatureType::Hills
                && myE >= 1) {
                aridLF[static_cast<std::size_t>(i)] = 5;
            } else if (t == TerrainType::Desert
                && myE == 0 && higherNb >= 3) {
                aridLF[static_cast<std::size_t>(i)] = 6;
            }
            if (t == TerrainType::Desert
                && grid.riverEdges(i) != 0
                && higherNb >= 4) {
                aridLF[static_cast<std::size_t>(i)] = 7;
            }
        }
    }

    // ---- TRANSFORM-FAULT SUBTYPES ----
    for (int32_t i = 0; i < totalT; ++i) {
        const auto& sh = grid.seismicHazard();
        if (sh.size() <= static_cast<std::size_t>(i)) { continue; }
        const float oro = orogeny[static_cast<std::size_t>(i)];
        if ((sh[static_cast<std::size_t>(i)] & 0x07) == 2) {
            if (oro < -0.04f) {
                tfType[static_cast<std::size_t>(i)] = 1;
            } else if (oro > 0.06f) {
                tfType[static_cast<std::size_t>(i)] = 2;
            } else {
                tfType[static_cast<std::size_t>(i)] = 3;
            }
        }
    }

    // ---- LAKE-EFFECT SNOW ----
    AOC_S12_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        if (lat < 0.45f) { continue; }
        const float windStep = (lat >= 0.60f) ? -1.0f : 1.0f;
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                continue;
            }
            bool foundLake = false;
            for (int32_t s = 1; s <= 3 && !foundLake; ++s) {
                int32_t cc = col - static_cast<int32_t>(windStep) * s;
                if (cylSim) {
                    if (cc < 0)        { cc += width; }
                    if (cc >= width)   { cc -= width; }
                } else if (cc < 0 || cc >= width) { continue; }
                const int32_t uIdx = row * width + cc;
                if (lakeFlag[static_cast<std::size_t>(uIdx)] != 0) {
                    foundLake = true;
                }
            }
            if (foundLake) {
                lakeFX[static_cast<std::size_t>(i)] = 1;
            }
        }
    }

    // ---- DRUMLIN ALIGNMENT ----
    for (int32_t i = 0; i < totalT; ++i) {
        const auto& gf = grid.glacialFeature();
        if (gf.size() <= static_cast<std::size_t>(i)) { continue; }
        if (gf[static_cast<std::size_t>(i)] != 4) { continue; }
        const int32_t row = i / width;
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        uint8_t flowDirH = (ny < 0.5f) ? 5 : 1;
        drumDir[static_cast<std::size_t>(i)] = flowDirH;
    }

    // ---- SUTURE REACTIVATION ----
    for (int32_t i = 0; i < totalT; ++i) {
        const auto& rk = grid.rockType();
        const auto& sh = grid.seismicHazard();
        const std::size_t si = static_cast<std::size_t>(i);
        if (rk.size() > si && rk[si] == 3
            && sh.size() > si && (sh[si] & 0x07) == 3) {
            sutReact[si] = 1;
        }
    }

    grid.setCoastalLandform(std::move(coastLF));
    grid.setRiverRegime(std::move(rivReg));
    grid.setAridLandform(std::move(aridLF));
    grid.setTransformFaultType(std::move(tfType));
    grid.setLakeEffectSnow(std::move(lakeFX));
    grid.setDrumlinDirection(std::move(drumDir));
    grid.setSutureReactivated(std::move(sutReact));
}

} // namespace aoc::map::gen
