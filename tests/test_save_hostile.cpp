/**
 * @file test_save_hostile.cpp
 * @brief Hostile save-file inputs must make loadGame return a non-Ok code
 *        without crashing, hanging, or allocating multi-GB buffers.
 *
 * Each case generates a valid save in-test via saveGame, corrupts specific
 * byte ranges, and feeds the result back through loadGame (2026-06-06 audit:
 * map-dimension validation, reserve() count caps, canReadRecords guard).
 */

#include "aoc/save/Serializer.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/turn/TurnManager.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

// assert() is compiled out under NDEBUG (release preset builds the tests),
// so checks must be explicit to run in every configuration.
void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "test_save_hostile FAIL: %s\n", what);
        ++g_failures;
    }
}

/// Full set of world objects loadGame/saveGame operate on.
struct World {
    aoc::game::GameState gameState;
    aoc::map::HexGrid grid;
    aoc::sim::TurnManager turnManager;
    aoc::sim::EconomySimulation economy;
    aoc::sim::DiplomacyManager diplomacy;
    aoc::map::FogOfWar fogOfWar;
    aoc::Random rng{12345};
};

void initWorld(World& w) {
    w.grid.initialize(16, 16);
    w.gameState.initialize(2);
    w.diplomacy.initialize(2);
    w.economy.initialize();
    w.turnManager.setPlayerCount(0, 2);
}

std::vector<uint8_t> makeValidSave(const std::filesystem::path& path) {
    World w;
    initWorld(w);
    const aoc::ErrorCode r = aoc::save::saveGame(
        path.string(), w.gameState, w.grid, w.turnManager, w.economy,
        w.diplomacy, w.fogOfWar, w.rng);
    check(r == aoc::ErrorCode::Ok, "saveGame produces the fixture save");

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    check(in.is_open(), "fixture save can be reopened");
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    check(in.good(), "fixture save bytes read back");
    return bytes;
}

/// Write `bytes` to `path` and run loadGame on a fresh world.
aoc::ErrorCode loadBytes(const std::filesystem::path& path,
                         const std::vector<uint8_t>& bytes) {
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    World w;
    initWorld(w);
    return aoc::save::loadGame(path.string(), w.gameState, w.grid,
                               w.turnManager, w.economy, w.diplomacy,
                               w.fogOfWar, w.rng);
}

/// Byte offset of the first data byte of the section with the given id,
/// or SIZE_MAX when absent. Layout: 16-byte header, then per section
/// sectionId(u16) + sectionSize(u32) + data, all little-endian.
std::size_t findSectionData(const std::vector<uint8_t>& bytes, aoc::save::SectionId id) {
    std::size_t off = 16;
    while (off + 6 <= bytes.size()) {
        const uint16_t sid = static_cast<uint16_t>(
            static_cast<uint32_t>(bytes[off])
            | (static_cast<uint32_t>(bytes[off + 1]) << 8));
        const uint32_t size = static_cast<uint32_t>(bytes[off + 2])
            | (static_cast<uint32_t>(bytes[off + 3]) << 8)
            | (static_cast<uint32_t>(bytes[off + 4]) << 16)
            | (static_cast<uint32_t>(bytes[off + 5]) << 24);
        if (sid == static_cast<uint16_t>(id)) {
            return off + 6;
        }
        off += 6 + static_cast<std::size_t>(size);
    }
    return SIZE_MAX;
}

void putU32(std::vector<uint8_t>& bytes, std::size_t off, uint32_t v) {
    bytes[off]     = static_cast<uint8_t>(v & 0xFF);
    bytes[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    bytes[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    bytes[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void test_validSaveLoadsOk(const std::filesystem::path& dir,
                           const std::vector<uint8_t>& valid) {
    // Bounds caps must not reject a legitimate save.
    check(loadBytes(dir / "valid_copy.sav", valid) == aoc::ErrorCode::Ok,
          "unmodified fixture save loads Ok");
}

void test_truncatedFile(const std::filesystem::path& dir,
                        const std::vector<uint8_t>& valid) {
    const std::vector<uint8_t> truncated(
        valid.begin(),
        valid.begin() + static_cast<std::ptrdiff_t>(valid.size() / 2));
    check(loadBytes(dir / "truncated.sav", truncated) != aoc::ErrorCode::Ok,
          "file truncated to half fails to load");
}

void test_garbageMagic(const std::filesystem::path& dir,
                       const std::vector<uint8_t>& valid) {
    std::vector<uint8_t> badMagic = valid;
    putU32(badMagic, 0, 0xDEADBEEFu);
    check(loadBytes(dir / "bad_magic.sav", badMagic) != aoc::ErrorCode::Ok,
          "garbage magic fails to load");
}

void test_hugeMapDimensions(const std::filesystem::path& dir,
                            const std::vector<uint8_t>& valid) {
    std::vector<uint8_t> hugeMap = valid;
    const std::size_t off = findSectionData(hugeMap, aoc::save::SectionId::MapGrid);
    check(off != SIZE_MAX, "MapGrid section present in fixture save");
    if (off == SIZE_MAX) {
        return;
    }
    putU32(hugeMap, off, 0x7FFFFFFFu);      // width
    putU32(hugeMap, off + 4, 0x7FFFFFFFu);  // height
    check(loadBytes(dir / "huge_map.sav", hugeMap) != aoc::ErrorCode::Ok,
          "width/height 0x7FFFFFFF fails to load");
}

void test_hugeRecordCount(const std::filesystem::path& dir,
                          const std::vector<uint8_t>& valid,
                          aoc::save::SectionId section, const char* what) {
    std::vector<uint8_t> hugeCount = valid;
    const std::size_t off = findSectionData(hugeCount, section);
    check(off != SIZE_MAX, "record-list section present in fixture save");
    if (off == SIZE_MAX) {
        return;
    }
    putU32(hugeCount, off, 0xFFFFFFFFu);
    check(loadBytes(dir / "huge_count.sav", hugeCount) != aoc::ErrorCode::Ok, what);
}

} // namespace

int main() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "aoc_test_save_hostile";
    std::filesystem::create_directories(dir);

    const std::vector<uint8_t> valid = makeValidSave(dir / "valid.sav");
    check(valid.size() > 16, "fixture save is non-trivial");
    if (valid.size() > 16) {
        test_validSaveLoadsOk(dir, valid);
        test_truncatedFile(dir, valid);
        test_garbageMagic(dir, valid);
        test_hugeMapDimensions(dir, valid);
        test_hugeRecordCount(dir, valid,
                             aoc::save::SectionId::ElectricityAgreementState,
                             "electricity agreement count 0xFFFFFFFF fails to load");
        test_hugeRecordCount(dir, valid, aoc::save::SectionId::HoardState,
                             "hoard count 0xFFFFFFFF fails to load");
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    if (g_failures != 0) {
        return 1;
    }
    std::printf("test_save_hostile: OK\n");
    return 0;
}
