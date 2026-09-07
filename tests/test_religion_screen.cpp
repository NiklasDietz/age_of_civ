/**
 * @file test_religion_screen.cpp
 * @brief The Religion screen offers every free follower belief for the
 *        pantheon and founds with the clicked one; taken beliefs are shown but
 *        not offered; the world religions and own-city pressure rows appear;
 *        the screen rebuilds after the state moves. Civ VI plan Phase 2.7.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/ui/ReligionScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Widget.hpp"

#include <string>
#include <variant>

using aoc::PlayerId;
using aoc::ui::ButtonData;
using aoc::ui::LabelData;
using aoc::ui::ReligionScreen;
using aoc::ui::UIManager;
using aoc::ui::Widget;

namespace {

struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(2);
    UIManager ui;
    ReligionScreen screen;

    Fixture() {
        aoc::test::addCityAt(this->world, PlayerId{0}, 5, 5, "Home");
        this->world.gameState.player(PlayerId{0})->faith().faith = 500.0f;
        this->world.gameState.player(PlayerId{1})->faith().faith = 500.0f;
        REQUIRE(aoc::sim::requestFoundPantheon(this->world.gameState, PlayerId{1}, 5) == aoc::ErrorCode::Ok);
        this->ui.setScreenSize(1920.0f, 1200.0f);
        this->screen.setScreenSize(1920.0f, 1200.0f);
        this->screen.setContext(&this->world.gameState, &this->world.grid, PlayerId{0});
    }

    bool clickButton(const std::string& needle) {
        for (const Widget& w : this->ui.widgets()) {
            if (w.id == aoc::ui::INVALID_WIDGET) { continue; }
            const ButtonData* btn = std::get_if<ButtonData>(&w.data);
            if (btn != nullptr && btn->label.find(needle) != std::string::npos) {
                const aoc::ui::WidgetId id = w.id;
                return this->ui.clickWidget(id);
            }
        }
        return false;
    }

    [[nodiscard]] int32_t buttonsContaining(const std::string& needle) const {
        int32_t n = 0;
        for (const Widget& w : this->ui.widgets()) {
            if (w.id == aoc::ui::INVALID_WIDGET) { continue; }
            const ButtonData* btn = std::get_if<ButtonData>(&w.data);
            if (btn != nullptr && btn->label.find(needle) != std::string::npos) { ++n; }
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

TEST_CASE("the pantheon rows offer the free follower beliefs and found with the clicked one") {
    Fixture f;
    f.screen.open(f.ui);
    // One row per free follower belief, derived rather than hard-coded: the
    // belief table grew from sixteen entries to forty and a literal count here
    // failed for a reason that had nothing to do with the screen.
    int32_t followerBeliefs = 0;
    for (uint8_t i = 0; i < aoc::sim::BELIEF_COUNT; ++i) {
        if (aoc::sim::allBeliefs()[i].type == aoc::sim::BeliefType::Follower) {
            ++followerBeliefs;
        }
    }
    REQUIRE(followerBeliefs >= 2);
    CHECK(f.buttonsContaining("Found: ") == followerBeliefs - 1); // one taken by P1
    CHECK(f.buttonsContaining("Taken: ") == 1);
    const std::string sixth(aoc::sim::allBeliefs()[6].name);
    REQUIRE(f.clickButton("Found: " + sixth));
    CHECK(f.world.gameState.player(PlayerId{0})->faith().pantheonBelief == 6);
    f.screen.refresh(f.ui);   // rebuilt: now the religion section with the pick rows
    CHECK(f.buttonsContaining("Found Religion") == 1);
    CHECK(f.buttonsContaining("[x] ") == 3);       // one default pick per type
    CHECK(f.labelsContaining("World religions: 0") == 1);
    CHECK(f.labelsContaining("Home: no religion") == 1);
    f.screen.close(f.ui);
}

TEST_CASE("picking beliefs and founding lists the religion with its cities") {
    Fixture f;
    REQUIRE(aoc::sim::requestFoundPantheon(f.world.gameState, PlayerId{0}, 6) == aoc::ErrorCode::Ok);
    f.screen.open(f.ui);
    const std::string founderThree(aoc::sim::allBeliefs()[3].name);
    REQUIRE(f.clickButton("[ ] " + founderThree));
    f.screen.refresh(f.ui);
    CHECK(f.buttonsContaining("[x] " + founderThree) == 1);
    REQUIRE(f.clickButton("Found Religion"));
    const aoc::game::Player& p0 = *f.world.gameState.player(PlayerId{0});
    REQUIRE(p0.faith().foundedReligion != aoc::sim::NO_RELIGION);
    CHECK(f.world.gameState.religionTracker().religions[p0.faith().foundedReligion].founderBelief == 3);
    f.screen.refresh(f.ui);
    CHECK(f.labelsContaining("World religions: 1") == 1);
    CHECK(f.labelsContaining("[yours]") == 1);
    CHECK(f.labelsContaining("Home: ") == 1);
    f.screen.close(f.ui);
}
