/**
 * @file Biogeography.cpp
 * @brief Biogeography passes implementation.
 */

#include "aoc/map/gen/Biogeography.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aoc::map::gen {

void runBiogeographicRealms(HexGrid& grid) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    const auto& mergesAbsorbed = grid.plateMergesAbsorbed();
    const auto& crustAge       = grid.plateCrustAge();
    const auto& landFrac       = grid.plateLandFrac();
    std::vector<uint8_t> isoRealm(static_cast<std::size_t>(totalT), 0);
    for (int32_t i = 0; i < totalT; ++i) {
        const TerrainType t = grid.terrain(i);
        if (t == TerrainType::Ocean
            || t == TerrainType::ShallowWater) {
            continue;
        }
        const uint8_t pid = grid.plateId(i);
        if (pid == 0xFFu || pid >= mergesAbsorbed.size()) { continue; }
        if (pid >= crustAge.size() || pid >= landFrac.size()) { continue; }
        if (mergesAbsorbed[pid] == 0
            && crustAge[pid] > 60.0f
            && landFrac[pid] > 0.40f) {
            isoRealm[static_cast<std::size_t>(i)] = 1;
        }
    }
    grid.setIsolatedRealm(std::move(isoRealm));
}

void runLandBridges(HexGrid& grid, bool cylindrical,
                    const std::vector<uint8_t>& lakeFlag) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylindrical, col, row, dir, outIdx);
    };
    std::vector<uint8_t> bridges(static_cast<std::size_t>(totalT), 0);
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            if (grid.terrain(i) != TerrainType::ShallowWater) {
                continue;
            }
            if (lakeFlag[static_cast<std::size_t>(i)] != 0) { continue; }
            std::array<uint8_t, 6> nbPids{};
            std::array<bool, 6>    nbLand{};
            int32_t landCount = 0;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                nbPids[static_cast<std::size_t>(d)] = 0xFFu;
                if (!nb(col, row, d, nIdx)) { continue; }
                const TerrainType nt = grid.terrain(nIdx);
                if (nt != TerrainType::Ocean
                    && nt != TerrainType::ShallowWater) {
                    nbLand[static_cast<std::size_t>(d)] = true;
                    nbPids[static_cast<std::size_t>(d)] =
                        grid.plateId(nIdx);
                    ++landCount;
                }
            }
            if (landCount < 2) { continue; }
            bool different = false;
            uint8_t firstPid = 0xFFu;
            for (int32_t d = 0; d < 6; ++d) {
                if (!nbLand[static_cast<std::size_t>(d)]) { continue; }
                const uint8_t pid = nbPids[static_cast<std::size_t>(d)];
                if (pid == 0xFFu) { continue; }
                if (firstPid == 0xFFu) { firstPid = pid; }
                else if (pid != firstPid) { different = true; break; }
            }
            if (different) {
                bridges[static_cast<std::size_t>(i)] = 1;
            }
        }
    }
    grid.setLandBridge(std::move(bridges));
}

void runRefugia(HexGrid& grid, bool cylindrical,
                std::vector<float>& soilFert) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylindrical, col, row, dir, outIdx);
    };
    std::vector<uint8_t> refugia(static_cast<std::size_t>(totalT), 0);
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row)
                       / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        if (lat < 0.30f || lat > 0.60f) { continue; }
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            if (t != TerrainType::Plains
                && t != TerrainType::Grassland) { continue; }
            if (i >= static_cast<int32_t>(soilFert.size())) { continue; }
            if (soilFert[static_cast<std::size_t>(i)] < 0.55f) { continue; }
            bool nearMtn = false;
            for (int32_t d = 0; d < 6 && !nearMtn; ++d) {
                int32_t cc = col, rr = row;
                for (int32_t step = 0; step < 4 && !nearMtn; ++step) {
                    int32_t nIdx;
                    if (!nb(cc, rr, d, nIdx)) { break; }
                    cc = nIdx % width;
                    rr = nIdx / width;
                    if (grid.terrain(nIdx) == TerrainType::Mountain) {
                        nearMtn = true;
                    }
                }
            }
            if (nearMtn) {
                refugia[static_cast<std::size_t>(i)] = 1;
                soilFert[static_cast<std::size_t>(i)] = std::min(
                    1.0f,
                    soilFert[static_cast<std::size_t>(i)] + 0.10f);
            }
        }
    }
    grid.setRefugium(std::move(refugia));
}

void runMetamorphicCoreComplex(HexGrid& grid, bool cylindrical) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylindrical, col, row, dir, outIdx);
    };
    const auto& rockNow = grid.rockType();
    if (rockNow.empty()) { return; }
    std::vector<uint8_t> rockUpd2(rockNow.begin(), rockNow.end());
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            if (rockUpd2[static_cast<std::size_t>(i)] != 0) { continue; }
            bool nearOphiolite = false;
            for (int32_t d = 0; d < 6 && !nearOphiolite; ++d) {
                int32_t cc = col, rr = row;
                for (int32_t step = 0; step < 2 && !nearOphiolite; ++step) {
                    int32_t nIdx;
                    if (!nb(cc, rr, d, nIdx)) { break; }
                    cc = nIdx % width;
                    rr = nIdx / width;
                    if (rockUpd2[static_cast<std::size_t>(nIdx)] == 3) {
                        nearOphiolite = true;
                    }
                }
            }
            if (nearOphiolite) {
                rockUpd2[static_cast<std::size_t>(i)] = 2;
            }
        }
    }
    grid.setRockType(std::move(rockUpd2));
}

} // namespace aoc::map::gen
