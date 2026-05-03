/**
 * @file Mappability.cpp
 * @brief Post-SESSION-4 analysis passes implementation.
 */

#include "aoc/map/gen/Mappability.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aoc::map::gen {

void runMountainPass(HexGrid& grid, bool cylindrical) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylindrical, col, row, dir, outIdx);
    };
    std::vector<uint8_t> passFlag(static_cast<std::size_t>(totalT), 0);
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater
                || t == TerrainType::Mountain) {
                continue;
            }
            int32_t mtnNb = 0;
            int32_t openNb = 0;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                const TerrainType nt = grid.terrain(nIdx);
                if (nt == TerrainType::Mountain) {
                    ++mtnNb;
                } else if (nt == TerrainType::Plains
                        || nt == TerrainType::Grassland
                        || nt == TerrainType::Desert) {
                    ++openNb;
                }
            }
            if (mtnNb >= 2 && openNb >= 2) {
                passFlag[static_cast<std::size_t>(i)] = 1;
            }
        }
    }
    grid.setMountainPass(std::move(passFlag));
}

void runDefensibility(HexGrid& grid, bool cylindrical) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylindrical, col, row, dir, outIdx);
    };
    std::vector<uint8_t> defScore(static_cast<std::size_t>(totalT), 0);
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                continue;
            }
            const int8_t myE = grid.elevation(i);
            int32_t score = 0;
            if (t == TerrainType::Mountain) { score += 80; }
            if (grid.feature(i) == FeatureType::Hills) {
                score += 40;
            }
            int32_t blockedFlanks = 0;
            int32_t totalFlanks = 0;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                ++totalFlanks;
                const TerrainType nt = grid.terrain(nIdx);
                const int8_t nE = grid.elevation(nIdx);
                if (nt == TerrainType::Ocean
                    || nt == TerrainType::ShallowWater
                    || nt == TerrainType::Mountain) {
                    ++blockedFlanks;
                }
                if (myE > nE) { score += 10; }
            }
            if (totalFlanks > 0) {
                score += static_cast<int32_t>(
                    50.0f * static_cast<float>(blockedFlanks)
                          / static_cast<float>(totalFlanks));
            }
            if (grid.riverEdges(i) != 0) { score += 20; }
            defScore[static_cast<std::size_t>(i)] =
                static_cast<uint8_t>(std::clamp(score, 0, 255));
        }
    }
    grid.setDefensibility(std::move(defScore));
}

void runDomesticable(HexGrid& grid) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    std::vector<uint8_t> dom(static_cast<std::size_t>(totalT), 0);
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row)
                       / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            const FeatureType f = grid.feature(i);
            uint8_t b = 0;
            if ((t == TerrainType::Plains || t == TerrainType::Grassland)
                && lat > 0.20f && lat < 0.55f) {
                b |= 0x01;
            }
            if (t == TerrainType::Plains
                && lat > 0.35f && lat < 0.55f) {
                b |= 0x02;
            }
            if (f == FeatureType::Hills
                && lat > 0.30f && lat < 0.55f) {
                b |= 0x04;
            }
            if (f == FeatureType::Hills
                && lat > 0.40f) {
                b |= 0x08;
            }
            if ((t == TerrainType::Mountain
                 || (t == TerrainType::Plains
                     && grid.elevation(i) >= 1))
                && lat > 0.10f && lat < 0.30f) {
                b |= 0x10;
            }
            if (t == TerrainType::Desert
                && lat > 0.10f && lat < 0.40f) {
                b |= 0x20;
            }
            if ((t == TerrainType::Tundra
                 || t == TerrainType::Snow)
                && lat > 0.55f) {
                b |= 0x40;
            }
            if (f == FeatureType::Forest
                && lat > 0.20f && lat < 0.55f) {
                b |= 0x80;
            }
            dom[static_cast<std::size_t>(i)] = b;
        }
    }
    grid.setDomesticable(std::move(dom));
}

void runTradeRoutePotential(HexGrid& grid,
                            const std::vector<uint8_t>& marineD) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    std::vector<uint8_t> tradePot(static_cast<std::size_t>(totalT), 0);
    for (int32_t i = 0; i < totalT; ++i) {
        const TerrainType t = grid.terrain(i);
        if (t == TerrainType::Mountain) {
            if (grid.mountainPass()[
                    static_cast<std::size_t>(i)] != 0) {
                tradePot[static_cast<std::size_t>(i)] = 200;
            }
            continue;
        }
        int32_t s = 30;
        if (t == TerrainType::Plains
            || t == TerrainType::Grassland) { s += 60; }
        if (t == TerrainType::Desert) { s -= 30; }
        if (t == TerrainType::Snow
            || t == TerrainType::Tundra) { s -= 20; }
        if (grid.riverEdges(i) != 0) { s += 60; }
        if (grid.feature(i) == FeatureType::Hills) {
            s -= 20;
        }
        if (grid.feature(i) == FeatureType::Forest) {
            s -= 10;
        }
        if (grid.feature(i) == FeatureType::Jungle) {
            s -= 30;
        }
        if (t == TerrainType::Ocean
            || t == TerrainType::ShallowWater) {
            s = 80;
            if (i < static_cast<int32_t>(marineD.size())
                && marineD[static_cast<std::size_t>(i)] == 1) {
                s = 130;
            }
        }
        tradePot[static_cast<std::size_t>(i)] =
            static_cast<uint8_t>(std::clamp(s, 0, 255));
    }
    grid.setTradeRoutePotential(std::move(tradePot));
}

void runHabitability(HexGrid& grid, bool cylindrical,
                     const std::vector<float>& soilFert,
                     const std::vector<uint16_t>& natHazard,
                     const std::vector<uint8_t>& disease,
                     const std::vector<uint8_t>& permafrost) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylindrical, col, row, dir, outIdx);
    };
    std::vector<uint8_t> habit(static_cast<std::size_t>(totalT), 0);
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater
                || t == TerrainType::Mountain) {
                continue;
            }
            float score = 0.0f;
            if (i < static_cast<int32_t>(soilFert.size())) {
                score += soilFert[static_cast<std::size_t>(i)] * 0.40f;
            }
            if (lat > 0.20f && lat < 0.55f) { score += 0.20f; }
            else if (lat < 0.20f)            { score += 0.10f; }
            if (grid.riverEdges(i) != 0) { score += 0.15f; }
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
            if (nearWater) { score += 0.10f; }
            if (i < static_cast<int32_t>(natHazard.size())) {
                const uint16_t h = natHazard[static_cast<std::size_t>(i)];
                const int32_t hCount = __builtin_popcount(h);
                score -= static_cast<float>(hCount) * 0.04f;
            }
            if (i < static_cast<int32_t>(disease.size())
                && disease[static_cast<std::size_t>(i)] != 0) {
                score -= 0.10f;
            }
            if (permafrost[static_cast<std::size_t>(i)] != 0) {
                score -= 0.20f;
            }
            habit[static_cast<std::size_t>(i)] =
                static_cast<uint8_t>(
                    std::clamp(score * 255.0f, 0.0f, 255.0f));
        }
    }
    grid.setHabitability(std::move(habit));
}

void runWetlandSubtype(HexGrid& grid) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    std::vector<uint8_t> wetSub(static_cast<std::size_t>(totalT), 0);
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const FeatureType f = grid.feature(i);
            if (f == FeatureType::Floodplains) {
                wetSub[static_cast<std::size_t>(i)] = 4;
                continue;
            }
            if (f != FeatureType::Marsh) { continue; }
            if (lat > 0.55f) {
                wetSub[static_cast<std::size_t>(i)] = 1;
            } else if (lat < 0.30f) {
                wetSub[static_cast<std::size_t>(i)] = 2;
            } else {
                wetSub[static_cast<std::size_t>(i)] = 3;
            }
        }
    }
    grid.setWetlandSubtype(std::move(wetSub));
}

void runCoralReef(HexGrid& grid,
                  const std::vector<uint8_t>& bSub) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    for (int32_t i = 0; i < totalT; ++i) {
        if (grid.terrain(i) != TerrainType::ShallowWater) {
            continue;
        }
        if (grid.feature(i) != FeatureType::None) { continue; }
        const int32_t row = i / width;
        const float ny = static_cast<float>(row)
                       / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        if (lat > 0.30f) { continue; }
        const std::size_t si = static_cast<std::size_t>(i);
        const uint8_t sub = bSub[si];
        const bool platform = (sub == 14 || sub == 13 || sub == 11);
        if (platform) {
            grid.setFeature(i, FeatureType::Reef);
        }
    }
}

} // namespace aoc::map::gen
