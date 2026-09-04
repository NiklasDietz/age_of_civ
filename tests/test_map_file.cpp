/**
 * @file test_map_file.cpp
 * @brief Pins the full-fidelity map file: every HexGrid layer round-trips, and
 *        hostile files are rejected without touching the target grid.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexGridLayers.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/save/MapFile.hpp"
#include "GridLayerCompare.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

std::string scratchPath(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

/// A small grid with values in layers of every element kind the file knows.
aoc::map::HexGrid sampleGrid() {
    aoc::map::HexGrid grid;
    grid.initialize(12, 8, aoc::map::MapTopology::Cylindrical);
    const std::size_t tiles = static_cast<std::size_t>(grid.tileCount());
    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        grid.setTerrain(i, (i % 3 == 0) ? aoc::map::TerrainType::Grassland
                                        : aoc::map::TerrainType::Ocean);
        grid.setElevation(i, static_cast<int8_t>(i % 7 - 3));
        grid.setRiverEdges(i, static_cast<uint8_t>(i & 0x3F));
        grid.setResource(i, aoc::ResourceId{static_cast<uint16_t>(i % 5)});
        grid.setReserves(i, static_cast<int16_t>(i * 3 - 10));
    }
    grid.setNaturalWonder(5, static_cast<aoc::map::NaturalWonderType>(1));
    grid.setRowLatitudes({60.0f, 40.0f, 20.0f, 5.0f, -5.0f, -20.0f, -40.0f, -60.0f});
    std::vector<float> fertility(tiles, 0.25f);
    fertility[7] = 0.9f;
    grid.setSoilFertility(std::move(fertility));
    grid.setCropSuitability(3, std::vector<uint8_t>(tiles, 7));
    grid.setGreenhouseCrop(4, 42);
    grid.setHotspots({{0.1f, 0.2f}, {0.3f, 0.4f}});
    grid.setPlateMergesAbsorbed({3, -1, 7});
    return grid;
}

} // namespace

TEST_CASE("every layer survives a save/load round trip") {
    const aoc::map::HexGrid original = sampleGrid();
    const std::string path           = scratchPath("aoc_test_map_roundtrip.aocmap");
    aoc::save::MapFileInfo info;
    info.generatorSeed = 0xDEADBEEFu;
    REQUIRE(aoc::save::saveMapFile(path, original, info) == aoc::ErrorCode::Ok);

    aoc::map::HexGrid loaded;
    aoc::save::MapFileInfo loadedInfo;
    REQUIRE(aoc::save::loadMapFile(path, loaded, &loadedInfo) == aoc::ErrorCode::Ok);

    CHECK(loaded.width() == 12);
    CHECK(loaded.height() == 8);
    CHECK(loaded.topology() == aoc::map::MapTopology::Cylindrical);
    CHECK(loadedInfo.generatorSeed == 0xDEADBEEFu);
    CHECK(loaded.terrain(0) == aoc::map::TerrainType::Grassland);
    CHECK(loaded.naturalWonder(5) == static_cast<aoc::map::NaturalWonderType>(1));

    aoc::test::LayerSnapshot snapshot;
    original.visitLayers(snapshot);
    aoc::test::LayerCompare compare{snapshot};
    loaded.visitLayers(compare);
    CHECK(compare.seen > 150);
    CHECK_MESSAGE(compare.mismatched == 0, "first differing layer: " << compare.firstMismatch);
    std::filesystem::remove(path);
}

TEST_CASE("a truncated file is rejected and the target grid is untouched") {
    const std::string path = scratchPath("aoc_test_map_truncated.aocmap");
    REQUIRE(aoc::save::saveMapFile(path, sampleGrid()) == aoc::ErrorCode::Ok);
    const std::uintmax_t full = std::filesystem::file_size(path);
    std::filesystem::resize_file(path, full / 2);

    aoc::map::HexGrid target;
    target.initialize(3, 3);
    CHECK(aoc::save::loadMapFile(path, target) == aoc::ErrorCode::SaveCorrupted);
    CHECK(target.width() == 3);
    std::filesystem::remove(path);
}

TEST_CASE("a file with the wrong magic is rejected") {
    const std::string path = scratchPath("aoc_test_map_foreign.aocmap");
    {
        std::ofstream out(path, std::ios::binary);
        out << "this is not a map file, just some bytes that happen to be long enough";
    }
    aoc::map::HexGrid target;
    CHECK(aoc::save::loadMapFile(path, target) == aoc::ErrorCode::SaveCorrupted);
    std::filesystem::remove(path);
}

TEST_CASE("a missing file reports LoadFailed") {
    aoc::map::HexGrid target;
    CHECK(aoc::save::loadMapFile(scratchPath("aoc_test_map_does_not_exist.aocmap"), target) ==
          aoc::ErrorCode::LoadFailed);
}
