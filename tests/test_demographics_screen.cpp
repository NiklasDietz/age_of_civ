/**
 * @file test_demographics_screen.cpp
 * @brief The Demographics screen ranks the human against MET civilizations only,
 *        reports best / average / worst per metric, rebuilds when the numbers
 *        move, and tears down cleanly.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/ui/DemographicsScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Widget.hpp"

#include <string>
#include <variant>

using aoc::PlayerId;
using aoc::ui::DemographicsScreen;
using aoc::ui::LabelData;
using aoc::ui::UIManager;
using aoc::ui::Widget;

namespace {

constexpr aoc::UnitTypeId WARRIOR{0};

/// Three civs: the human (pop 5, 1 city, 2 warriors) has met civ 1 (pop 9, 2
/// cities, 0 warriors) but not civ 2 (pop 50, which must stay invisible).
struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(3);
    aoc::sim::DiplomacyManager diplomacy;
    UIManager ui;
    DemographicsScreen screen;

    Fixture() {
        this->diplomacy.initialize(3);
        for (uint8_t i = 0; i < 3; ++i) {   // distinct civ names, so "hidden" is unambiguous
            this->world.gameState.players()[i]->setCivId(static_cast<aoc::sim::CivId>(i));
        }
        this->diplomacy.meetPlayers(PlayerId{0}, PlayerId{1}, 0);
        aoc::test::addCityAt(this->world, PlayerId{0}, 5, 5, "Home").setPopulation(5);
        aoc::test::addUnitAt(this->world, PlayerId{0}, WARRIOR, 6, 5);
        aoc::test::addUnitAt(this->world, PlayerId{0}, WARRIOR, 7, 5);
        aoc::test::addCityAt(this->world, PlayerId{1}, 12, 9, "Thebes").setPopulation(6);
        aoc::test::addCityAt(this->world, PlayerId{1}, 14, 9, "Memphis").setPopulation(3);
        aoc::test::addCityAt(this->world, PlayerId{2}, 18, 4, "Hidden").setPopulation(50);
        this->world.gameState.players()[0]->victoryTracker().score = 400;
        this->world.gameState.players()[1]->victoryTracker().score = 700;
        this->world.gameState.players()[2]->victoryTracker().score = 9000;
        this->world.gameState.players()[1]->setTreasury(120, aoc::sim::MoneyFlow::external());
        this->world.gameState.players()[1]->tourism().tourismPerTurn = 12.0f;

        this->ui.setScreenSize(1920.0f, 1200.0f);
        this->screen.setScreenSize(1920.0f, 1200.0f);
        this->screen.setContext(&this->world.gameState, &this->world.grid, PlayerId{0},
                                &this->diplomacy);
    }

    [[nodiscard]] int32_t liveWidgets() const {
        int32_t n = 0;
        for (const Widget& w : this->ui.widgets()) {
            if (w.id != aoc::ui::INVALID_WIDGET) { ++n; }
        }
        return n;
    }

    [[nodiscard]] int32_t labelsContaining(const std::string& needle) const {
        int32_t n = 0;
        for (const Widget& w : this->ui.widgets()) {
            if (w.id == aoc::ui::INVALID_WIDGET) { continue; }
            const LabelData* label = std::get_if<LabelData>(&w.data);
            if (label != nullptr && label->text.find(needle) != std::string::npos) { ++n; }
        }
        return n;
    }
};

} // namespace

TEST_CASE("only met civilizations are ranked and the unmet one stays hidden") {
    Fixture f;
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("Ranking 2 known civilizations (1 not yet met)") == 1);
    const std::string hidden = std::string(aoc::sim::civDef(f.world.gameState.players()[2]->civId()).name);
    CHECK(f.labelsContaining(hidden + "  score") == 0);
    CHECK(f.labelsContaining("score 9000") == 0);
    CHECK(f.labelsContaining("(you)") == 1);
}

TEST_CASE("each metric row gives rank, best, average and worst over the known civs") {
    Fixture f;
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("Population  yours 5  rank 2 of 2  |  best 9  average 7  worst 5") == 1);
    CHECK(f.labelsContaining("Cities  yours 1  rank 2 of 2  |  best 2  average 1  worst 1") == 1);
    CHECK(f.labelsContaining("Military units  yours 2  rank 1 of 2  |  best 2  average 1  worst 0") == 1);
    CHECK(f.labelsContaining("Treasury  yours 0  rank 2 of 2  |  best 120  average 60  worst 0") == 1);
    CHECK(f.labelsContaining("Tourism  yours 0  rank 2 of 2  |  best 12  average 6  worst 0") == 1);
    CHECK(f.labelsContaining("Score  yours 400  rank 2 of 2  |  best 700  average 550  worst 400") == 1);
}

TEST_CASE("meeting the third civ and changing numbers rebuilds the rows on refresh") {
    Fixture f;
    f.screen.open(f.ui);
    const int32_t opened = f.liveWidgets();
    f.screen.refresh(f.ui);
    CHECK(f.liveWidgets() == opened);

    f.diplomacy.meetPlayers(PlayerId{0}, PlayerId{2}, 3);
    f.screen.refresh(f.ui);
    CHECK(f.labelsContaining("Ranking 3 known civilizations (0 not yet met)") == 1);
    CHECK(f.labelsContaining("score 9000") == 1);
    CHECK(f.labelsContaining("Population  yours 5  rank 3 of 3  |  best 50") == 1);
}

TEST_CASE("without a diplomacy manager every civ is ranked; close removes everything") {
    Fixture f;
    f.screen.setContext(&f.world.gameState, &f.world.grid, PlayerId{0}, nullptr);
    const int32_t before = f.liveWidgets();
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("Ranking 3 known civilizations (0 not yet met)") == 1);
    f.screen.close(f.ui);
    CHECK_FALSE(f.screen.isOpen());
    CHECK(f.liveWidgets() == before);
}
