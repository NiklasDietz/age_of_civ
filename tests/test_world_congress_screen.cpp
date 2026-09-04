/**
 * @file test_world_congress_screen.cpp
 * @brief The read-only World Congress screen shows the session status, the
 *        current proposal with its vote weights and the human's own vote, the
 *        human's favor, active effects and passed resolutions, and rebuilds only
 *        when the congress state moves.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/simulation/diplomacy/DiplomaticFavor.hpp"
#include "aoc/simulation/diplomacy/WorldCongress.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Widget.hpp"
#include "aoc/ui/WorldCongressScreen.hpp"

#include <string>
#include <variant>

using aoc::PlayerId;
using aoc::sim::Resolution;
using aoc::ui::LabelData;
using aoc::ui::UIManager;
using aoc::ui::Widget;
using aoc::ui::WorldCongressScreen;

namespace {

struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(2);
    UIManager ui;
    WorldCongressScreen screen;

    Fixture() {
        for (uint8_t i = 0; i < 2; ++i) {
            this->world.gameState.players()[i]->setCivId(static_cast<aoc::sim::CivId>(i));
        }
        this->world.gameState.players()[0]->diplomaticFavor().favor        = 45;
        this->world.gameState.players()[0]->diplomaticFavor().favorPerTurn = 3;
        this->ui.setScreenSize(1920.0f, 1200.0f);
        this->screen.setScreenSize(1920.0f, 1200.0f);
        this->screen.setContext(&this->world.gameState, &this->world.grid, PlayerId{0});
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

TEST_CASE("a fresh congress shows the countdown, the favor, and empty sections") {
    Fixture f;
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("Congress not yet convened   Next session in 50 turns   Your favor: 45 (+3 per turn)") == 1);
    CHECK(f.labelsContaining("None on the table.") == 1);
    CHECK(f.labelsContaining("None.") == 1);
    CHECK(f.labelsContaining("None yet.") == 1);
    CHECK(f.labelsContaining("Votes are cast automatically for every civilization, including yours.") == 1);
}

TEST_CASE("a proposal shows proposer, target, tallied vote weights and the human's own vote") {
    Fixture f;
    aoc::sim::WorldCongressComponent& wc = f.world.gameState.worldCongress();
    wc.isActive = true;
    wc.proposeResolution(Resolution::GlobalSanctions, PlayerId{1}, PlayerId{0});
    wc.castVote(PlayerId{0}, -2);
    wc.castVote(PlayerId{1}, 3);
    f.screen.open(f.ui);

    const std::string name = aoc::sim::resolutionName(Resolution::GlobalSanctions);
    CHECK(f.labelsContaining("Congress in session") == 1);
    CHECK(f.labelsContaining(name + "  proposed by ") == 1);
    CHECK(f.labelsContaining("targeting ") == 1);
    CHECK(f.labelsContaining("(you)") == 1);
    CHECK(f.labelsContaining("Votes: yes 3  no 2  |  your vote: -2") == 1);
}

TEST_CASE("active effects and passed resolutions are listed; refresh rebuilds only on change") {
    Fixture f;
    aoc::sim::WorldCongressComponent& wc = f.world.gameState.worldCongress();
    wc.activeEffects.push_back({Resolution::GlobalSanctions, PlayerId{1}, 7});
    wc.passedResolutions.push_back(Resolution::ClimateAccord);
    f.screen.open(f.ui);
    const int32_t opened = f.liveWidgets();
    CHECK(f.labelsContaining(std::string(aoc::sim::resolutionName(Resolution::GlobalSanctions)) + "  on ") == 1);
    CHECK(f.labelsContaining("7 turns left") == 1);
    CHECK(f.labelsContaining(aoc::sim::resolutionName(Resolution::ClimateAccord)) == 1);

    f.screen.refresh(f.ui);
    CHECK(f.liveWidgets() == opened);
    wc.activeEffects.front().turnsRemaining = 6;
    f.screen.refresh(f.ui);
    CHECK(f.labelsContaining("6 turns left") == 1);
    CHECK(f.labelsContaining("7 turns left") == 0);
}

TEST_CASE("close removes every widget") {
    Fixture f;
    const int32_t before = f.liveWidgets();
    f.screen.open(f.ui);
    CHECK(f.liveWidgets() > before + 10);
    f.screen.close(f.ui);
    CHECK_FALSE(f.screen.isOpen());
    CHECK(f.liveWidgets() == before);
}
