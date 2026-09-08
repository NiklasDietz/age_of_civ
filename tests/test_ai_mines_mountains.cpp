/**
 * @file test_ai_mines_mountains.cpp
 * @brief The AI can act on a tile it cannot stand on.
 *
 *        AIBuilderController improves `currentIdx`, the tile the builder is
 *        standing on. A MountainMine goes on the MOUNTAIN, and mountains are
 *        impassable, so no builder can ever stand on one. The controller's
 *        Step 2a hunts specifically for land tiles adjacent to unmined
 *        metal-bearing mountains, with a -3 hex priority bias -- and on
 *        arrival built a farm on the approach tile and walked away. The seek
 *        existed without the placement.
 *
 *        The HUD's "Mine Mountain" button has always done it correctly, so the
 *        capability was reachable by a human and structurally unreachable by
 *        the AI. It went unseen because until crustal thickening conserved mass
 *        there were no mountains on any generated map to mine.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/ai/AIBuilderController.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/tech/TechTree.hpp"

using aoc::PlayerId;
using aoc::UnitTypeId;

namespace {

constexpr UnitTypeId BUILDER{5};

/// A builder standing on owned flat land with a metal-bearing mountain next
/// door -- exactly the position Step 2a walks it to.
struct Prospect {
    aoc::test::World world = aoc::test::makeWorld(1);
    aoc::hex::AxialCoord stand{6, 6};
    aoc::hex::AxialCoord mountain{};

    explicit Prospect(uint16_t ore = aoc::sim::goods::IRON_ORE) {
        aoc::test::addCityAt(this->world, PlayerId{0}, 5, 5, "Basecamp");

        const int32_t standIdx = this->world.grid.toIndex(this->stand);
        this->world.grid.setTerrain(standIdx, aoc::map::TerrainType::Grassland);
        this->world.grid.setOwner(standIdx, PlayerId{0});

        this->mountain            = aoc::hex::neighbors(this->stand)[0];
        const int32_t mIdx        = this->world.grid.toIndex(this->mountain);
        this->world.grid.setTerrain(mIdx, aoc::map::TerrainType::Mountain);
        this->world.grid.setOwner(mIdx, PlayerId{0});
        this->world.grid.setResource(mIdx, aoc::ResourceId{ore});

        // Mining (TechId{0}) gates the improvement. The AI path applies the
        // tech gate; the HUD's button does not pass a tech and so skips it,
        // which is why a human could always do this and the AI had a second
        // reason to fail.
        aoc::game::Player* p = this->world.gameState.player(PlayerId{0});
        p->tech().initialize();
        p->tech().completedTechs[0] = true;

        aoc::test::addUnitAt(this->world, PlayerId{0}, BUILDER, this->stand.q, this->stand.r);
    }

    void runAI() {
        aoc::sim::ai::AIBuilderController ai{PlayerId{0}, aoc::ui::AIDifficulty::Normal};
        ai.manageBuildersAndImprovements(this->world.gameState, this->world.grid);
    }

    [[nodiscard]] aoc::map::ImprovementType mountainImprovement() const {
        return this->world.grid.improvement(this->world.grid.toIndex(this->mountain));
    }
};

} // namespace

TEST_CASE("an AI builder mines the metal-bearing mountain beside it") {
    Prospect p;
    REQUIRE(p.mountainImprovement() == aoc::map::ImprovementType::None);

    p.runAI();

    CHECK(p.mountainImprovement() == aoc::map::ImprovementType::MountainMine);
}

TEST_CASE("a mountain with no metal is left alone") {
    // The improvement rule wants a metal deposit; bare rock is not a mine.
    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Basecamp");
    const aoc::hex::AxialCoord stand{6, 6};
    const int32_t standIdx = w.grid.toIndex(stand);
    w.grid.setTerrain(standIdx, aoc::map::TerrainType::Grassland);
    w.grid.setOwner(standIdx, PlayerId{0});

    const aoc::hex::AxialCoord mtn = aoc::hex::neighbors(stand)[0];
    const int32_t mIdx             = w.grid.toIndex(mtn);
    w.grid.setTerrain(mIdx, aoc::map::TerrainType::Mountain);
    w.grid.setOwner(mIdx, PlayerId{0});
    // no resource set

    aoc::game::Player* pl = w.gameState.player(PlayerId{0});
    pl->tech().initialize();
    pl->tech().completedTechs[0] = true;

    aoc::test::addUnitAt(w, PlayerId{0}, BUILDER, stand.q, stand.r);
    aoc::sim::ai::AIBuilderController ai{PlayerId{0}, aoc::ui::AIDifficulty::Normal};
    ai.manageBuildersAndImprovements(w.gameState, w.grid);

    CHECK(w.grid.improvement(mIdx) != aoc::map::ImprovementType::MountainMine);
}

TEST_CASE("mining a mountain spends a builder charge") {
    // It is a builder action like any other, and must cost what one costs --
    // otherwise a single builder mines every mountain on the continent.
    Prospect p;
    aoc::game::Unit* builder = p.world.gameState.player(PlayerId{0})->units()[0].get();
    REQUIRE(builder != nullptr);
    const int32_t chargesBefore = builder->chargesRemaining();
    REQUIRE(chargesBefore > 0);

    p.runAI();

    REQUIRE(p.mountainImprovement() == aoc::map::ImprovementType::MountainMine);
    // The builder may have been retired if that was its last charge; if it
    // survives, it must have paid.
    const auto& units = p.world.gameState.player(PlayerId{0})->units();
    if (!units.empty()) {
        CHECK(units[0]->chargesRemaining() < chargesBefore);
    }
}
