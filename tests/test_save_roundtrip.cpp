/**
 * @file test_save_roundtrip.cpp
 * @brief Save/load/save characterization: the serializer must reproduce an
 *        identical byte stream after a load, and the loaded state must match
 *        the original on spot-checked fields.
 *
 * Byte-compare is only valid because Serializer writes unordered_map
 * sections in sorted key order (see sortedEntries in Serializer.cpp); the
 * maps here are deliberately populated in scrambled insertion order to keep
 * that guarantee pinned.
 *
 * With AOC_WRITE_CORPUS=<path> the test additionally copies the first save
 * to <path> -- used to (re)generate the known-good corpus under
 * tests/data/saves/ when the save format version changes.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/core/Random.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/save/Serializer.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/turn/TurnManager.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct World {
    aoc::game::GameState gameState;
    aoc::map::HexGrid grid;
    aoc::sim::TurnManager turnManager;
    aoc::sim::EconomySimulation economy;
    aoc::sim::DiplomacyManager diplomacy;
    aoc::map::FogOfWar fogOfWar;
    aoc::Random rng{12345u};
};

/// Populate a small but non-trivial state. Every unordered_map the
/// serializer touches gets entries, inserted in scrambled key order.
void buildWorld(World& w) {
    w.grid.initialize(24, 16);
    for (int32_t i = 0; i < w.grid.tileCount(); i += 3) {
        w.grid.setTerrain(i, aoc::map::TerrainType::Grassland);
    }

    w.gameState.initialize(3);
    w.diplomacy.initialize(3);
    w.economy.initialize();
    w.turnManager.setPlayerCount(0, 3);
    w.fogOfWar.initialize(w.grid.tileCount(), 3);

    aoc::game::Player& p0 = *w.gameState.players()[0];
    aoc::game::Player& p1 = *w.gameState.players()[1];

    // NOTE: Player::m_treasury (the setTreasury/treasury() account) is NOT
    // serialized -- only the monetary/economy component treasuries are; the
    // app resyncs the spending account after load. Pin the component field.
    p0.monetary().treasury = 1234;
    p1.monetary().treasury = 87;

    aoc::game::City& alpha = p0.addCity({5, 5}, "Alpha");
    alpha.stockpile().goods[42]  = 10;   // scrambled insertion order on
    alpha.stockpile().goods[7]   = 3;    // purpose -- pins the sorted-write
    alpha.stockpile().goods[199] = 25;   // guarantee.
    alpha.stockpile().goods[13]  = 1;
    alpha.stockpile().exportBuffer[9] = 4;
    alpha.stockpile().exportBuffer[2] = 6;
    alpha.productionExperience().recipeExperience[11] = 40;
    alpha.productionExperience().recipeExperience[3]  = 7;
    alpha.buildingLevels().levels[6] = 2;
    alpha.buildingLevels().levels[1] = 3;

    aoc::game::City& beta = p1.addCity({12, 9}, "Beta");
    beta.stockpile().goods[199] = 5;
    beta.stockpile().goods[42]  = 1;

    p0.warWeariness().turnsAtWar[1] = 5;
    p0.warWeariness().turnsAtWar[2] = 12;
    p1.warWeariness().turnsAtWar[0] = 5;

    p0.addUnit(aoc::UnitTypeId{0}, {6, 5});
    p1.addUnit(aoc::UnitTypeId{0}, {12, 10});
    // Player 2 needs at least one unit: loadGame derives the player count
    // from the highest player index that owns a city or unit, so a player
    // with neither is silently dropped on load (pre-existing behaviour).
    w.gameState.players()[2]->addUnit(aoc::UnitTypeId{0}, {2, 2});
}

[[nodiscard]] std::vector<char> readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return std::vector<char>(std::istreambuf_iterator<char>(in),
                             std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("save -> load -> save reproduces identical bytes") {
    const std::string fileA = "roundtrip_a.sav";
    const std::string fileB = "roundtrip_b.sav";

    World original;
    buildWorld(original);
    REQUIRE(aoc::save::saveGame(fileA, original.gameState, original.grid,
                                original.turnManager, original.economy,
                                original.diplomacy, original.fogOfWar,
                                original.rng) == aoc::ErrorCode::Ok);

    World loaded;
    REQUIRE(aoc::save::loadGame(fileA, loaded.gameState, loaded.grid,
                                loaded.turnManager, loaded.economy,
                                loaded.diplomacy, loaded.fogOfWar,
                                loaded.rng) == aoc::ErrorCode::Ok);

    // Spot-check the loaded state against the original.
    REQUIRE(loaded.gameState.players().size() == 3);
    const aoc::game::Player& lp0 = *loaded.gameState.players()[0];
    const aoc::game::Player& lp1 = *loaded.gameState.players()[1];
    CHECK(lp0.monetary().treasury == 1234);
    CHECK(lp1.monetary().treasury == 87);
    REQUIRE(lp0.cities().size() == 1);
    const aoc::game::City& lAlpha = *lp0.cities()[0];
    CHECK(lAlpha.name() == "Alpha");
    CHECK(lAlpha.location() == aoc::hex::AxialCoord{5, 5});
    CHECK(lAlpha.stockpile().goods.at(42) == 10);
    CHECK(lAlpha.stockpile().goods.at(199) == 25);
    CHECK(lAlpha.stockpile().exportBuffer.at(2) == 6);
    CHECK(lAlpha.productionExperience().recipeExperience.at(11) == 40);
    CHECK(lAlpha.buildingLevels().levels.at(1) == 3);
    CHECK(lp0.warWeariness().turnsAtWar.at(2) == 12);
    CHECK(loaded.grid.width() == 24);
    CHECK(loaded.grid.height() == 16);

    // Resave the loaded state: byte-identical to the first save.
    REQUIRE(aoc::save::saveGame(fileB, loaded.gameState, loaded.grid,
                                loaded.turnManager, loaded.economy,
                                loaded.diplomacy, loaded.fogOfWar,
                                loaded.rng) == aoc::ErrorCode::Ok);

    std::vector<char> bytesA = readAll(fileA);
    std::vector<char> bytesB = readAll(fileB);
    REQUIRE(!bytesA.empty());
    CHECK(bytesA == bytesB);

    // Optional corpus (re)generation hook.
    const char* corpusPath = std::getenv("AOC_WRITE_CORPUS");
    if (corpusPath != nullptr) {
        std::error_code ec;
        std::filesystem::copy_file(
            fileA, corpusPath,
            std::filesystem::copy_options::overwrite_existing, ec);
        CHECK(!ec);
        std::printf("corpus save written to %s\n", corpusPath);
    }
}

#ifdef AOC_TEST_CORPUS_DIR
TEST_CASE("known-good save corpus still loads") {
    // Guards the load path: hardening changes to Serializer must keep
    // accepting saves produced by earlier good builds of the same
    // SAVE_VERSION. Regenerate via AOC_WRITE_CORPUS when the format
    // version is deliberately bumped.
    int corpusFiles = 0;
    for (const std::filesystem::directory_entry& entry
         : std::filesystem::directory_iterator(AOC_TEST_CORPUS_DIR)) {
        if (entry.path().extension() != ".sav") { continue; }
        ++corpusFiles;
        World w;
        CHECK_MESSAGE(
            aoc::save::loadGame(entry.path().string(), w.gameState, w.grid,
                                w.turnManager, w.economy, w.diplomacy,
                                w.fogOfWar, w.rng) == aoc::ErrorCode::Ok,
            "corpus file rejected: ", entry.path().string());
    }
    // An empty corpus would make this test pass vacuously.
    CHECK(corpusFiles >= 1);
}
#endif
