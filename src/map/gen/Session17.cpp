/**
 * @file Session17.cpp
 * @brief SESSION 17 implementation -- terminal Earth-system analytics.
 */

#include "aoc/map/gen/Session17.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _OPENMP
#  define AOC_S17_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_S17_PARALLEL_FOR_ROWS
#endif

namespace aoc::map::gen {

void runSession17(HexGrid& grid) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;

    std::vector<uint8_t> pet     (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> arid    (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> erosion (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> carbon  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> wild    (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> floodF  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> canopy  (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> ripaFor (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> magI    (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> gwDepth (static_cast<std::size_t>(totalT), 0);

    AOC_S17_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const std::size_t si = static_cast<std::size_t>(i);
            const TerrainType t = grid.terrain(i);
            const FeatureType f = grid.feature(i);

            // ---- PET (Hargreaves: insolation x temp proxy) ----
            int32_t petK = 0;
            const auto& iv = grid.solarInsolation();
            if (iv.size() > si) {
                const float Iv = static_cast<float>(iv[si]) / 255.0f;
                const float warmth = 1.0f - lat;
                petK = static_cast<int32_t>(Iv * warmth * 255.0f);
            }
            pet[si] = static_cast<uint8_t>(std::clamp(petK, 0, 255));

            // ---- ARIDITY (PET / precipitation proxy) ----
            int32_t precipProxy = 80;
            const auto& cc = grid.cloudCover();
            if (cc.size() > si) {
                precipProxy += static_cast<int32_t>(cc[si] * 100.0f);
            }
            if (f == FeatureType::Jungle) { precipProxy += 60; }
            if (t == TerrainType::Desert) { precipProxy = 20; }
            if (precipProxy > 0) {
                const float ratio = static_cast<float>(petK)
                    / static_cast<float>(precipProxy);
                arid[si] = static_cast<uint8_t>(
                    std::clamp(ratio * 80.0f, 0.0f, 255.0f));
            }

            // ---- EROSION POTENTIAL (RUSLE-like) ----
            if (t != TerrainType::Ocean && t != TerrainType::ShallowWater) {
                int32_t E = 30;
                const auto& sl = grid.slopeAngle();
                if (sl.size() > si) {
                    E += static_cast<int32_t>(sl[si]) / 2;
                }
                if (cc.size() > si) {
                    E += static_cast<int32_t>(cc[si] * 60.0f);
                }
                const auto& siltP = grid.soilSiltPct();
                if (siltP.size() > si) {
                    E += static_cast<int32_t>(siltP[si]) / 4;
                }
                const auto& vd = grid.vegetationDensity();
                if (vd.size() > si) {
                    E -= static_cast<int32_t>(vd[si]) / 3;
                }
                erosion[si] = static_cast<uint8_t>(
                    std::clamp(E, 0, 255));
            }

            // ---- CARBON STOCK ----
            int32_t C = 30;
            if (f == FeatureType::Jungle) { C = 220; }
            else if (f == FeatureType::Forest) {
                C = (lat < 0.20f) ? 200 : (lat > 0.55f ? 180 : 150);
            }
            else if (f == FeatureType::Marsh)       { C = 180; }
            else if (f == FeatureType::Floodplains) { C = 120; }
            else if (t == TerrainType::Grassland)   { C = 90; }
            else if (t == TerrainType::Plains)      { C = 60; }
            const auto& so = grid.soilOrder();
            if (so.size() > si && so[si] == 11) { C += 50; } // histosol peat
            if (so.size() > si && so[si] == 3)  { C += 30; } // mollisol
            carbon[si] = static_cast<uint8_t>(std::clamp(C, 0, 255));

            // ---- WILDERNESS ----
            const auto& nppV = grid.netPrimaryProductivity();
            const auto& spR  = grid.speciesRichness();
            if (nppV.size() > si && spR.size() > si
                && nppV[si] > 130 && spR[si] > 130) {
                wild[si] = 1;
            }

            // ---- FLOOD FREQUENCY ----
            if (f == FeatureType::Floodplains) {
                floodF[si] = 200;
            } else if (f == FeatureType::Marsh) {
                floodF[si] = 150;
            } else if (grid.riverEdges(i) != 0
                && grid.elevation(i) <= 0) {
                floodF[si] = 100;
            }

            // ---- CANOPY STRATIFICATION ----
            if (f == FeatureType::Jungle) {
                canopy[si] = 4;
            } else if (f == FeatureType::Forest) {
                if (lat < 0.30f)        { canopy[si] = 3; }
                else if (lat > 0.55f)   { canopy[si] = 2; }
                else                    { canopy[si] = 3; }
            } else if ((t == TerrainType::Plains
                 || t == TerrainType::Grassland)
                && lat < 0.30f
                && f == FeatureType::None) {
                canopy[si] = 1;
            }

            // ---- RIPARIAN FOREST ----
            if (grid.riverEdges(i) != 0) {
                int32_t r = 80;
                if (t == TerrainType::Desert) {
                    r = 200;
                } else if (f == FeatureType::Jungle
                        || f == FeatureType::Forest) {
                    r = 150;
                } else if (lat > 0.55f) {
                    r = 100;
                }
                ripaFor[si] = static_cast<uint8_t>(r);
            }

            // ---- MAGNETIC INTENSITY ----
            const float magI_K = 90.0f + lat * 165.0f;
            magI[si] = static_cast<uint8_t>(
                std::clamp(magI_K, 0.0f, 255.0f));

            // ---- GROUNDWATER DEPTH ----
            int32_t depth = 130;
            if (f == FeatureType::Marsh
                || f == FeatureType::Floodplains) {
                depth = 5;
            } else if (grid.riverEdges(i) != 0) {
                depth = 40;
            } else if (t == TerrainType::Desert) {
                depth = 230;
            } else if (t == TerrainType::Mountain) {
                depth = 200;
            } else if (t == TerrainType::Grassland) {
                depth = 80;
            }
            gwDepth[si] = static_cast<uint8_t>(
                std::clamp(depth, 0, 255));
        }
    }

    grid.setPetIndex(std::move(pet));
    grid.setAridityIndex(std::move(arid));
    grid.setErosionPotential(std::move(erosion));
    grid.setCarbonStock(std::move(carbon));
    grid.setWilderness(std::move(wild));
    grid.setFloodFrequency(std::move(floodF));
    grid.setCanopyStratification(std::move(canopy));
    grid.setRiparianForest(std::move(ripaFor));
    grid.setMagneticIntensity(std::move(magI));
    grid.setGroundwaterDepth(std::move(gwDepth));
}

} // namespace aoc::map::gen
