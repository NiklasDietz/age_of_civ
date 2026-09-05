/**
 * @file test_diplomacy_screen.cpp
 * @brief The Diplomacy screen offers the 2.10a actions and a casus belli picker.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/diplomacy/DiplomacyActions.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/ui/DiplomacyScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Widget.hpp"

#include <string>
#include <variant>

using aoc::PlayerId;
using aoc::ui::ButtonData;
using aoc::ui::DiplomacyScreen;
using aoc::ui::LabelData;
using aoc::ui::UIManager;
using aoc::ui::Widget;

namespace {

struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(2);
    aoc::sim::DiplomacyManager d;
    UIManager ui;
    DiplomacyScreen screen;

    Fixture() {
        this->d.initialize(2);
        this->d.meetPlayers(PlayerId{0}, PlayerId{1}, 3);
        aoc::test::addCityAt(this->world, PlayerId{0}, 4, 4, "Alpha");
        aoc::test::addCityAt(this->world, PlayerId{1}, 14, 8, "Beta");
        this->world.gameState.players()[1]->setCivId(static_cast<aoc::sim::CivId>(1));
        this->ui.setScreenSize(1920.0f, 1200.0f);
        this->screen.setScreenSize(1920.0f, 1200.0f);
        this->screen.setContext(&this->world.gameState, PlayerId{0}, &this->d, &this->world.grid, nullptr,
                                nullptr);
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

    [[nodiscard]] int32_t buttonsLabelled(const std::string& text) const {
        int32_t n = 0;
        for (const Widget& w : this->ui.widgets()) {
            if (w.id == aoc::ui::INVALID_WIDGET) { continue; }
            const ButtonData* btn = std::get_if<ButtonData>(&w.data);
            if (btn != nullptr && btn->label == text) { ++n; }
        }
        return n;
    }

    bool clickButton(const std::string& text) {
        for (const Widget& w : this->ui.widgets()) {
            if (w.id == aoc::ui::INVALID_WIDGET) { continue; }
            const ButtonData* btn = std::get_if<ButtonData>(&w.data);
            if (btn != nullptr && btn->label == text) {
                const aoc::ui::WidgetId id = w.id;
                return this->ui.clickWidget(id);
            }
        }
        return false;
    }
};

} // namespace

TEST_CASE("at peace the card offers war, borders, denounce, friendship, delegation and embassy") {
    Fixture f;
    f.screen.open(f.ui);
    CHECK(f.buttonsLabelled("Declare War") == 1);
    CHECK(f.buttonsLabelled("Open Borders") == 1);
    CHECK(f.buttonsLabelled("Denounce") == 1);
    CHECK(f.buttonsLabelled("Friendship") == 1);
    CHECK(f.buttonsLabelled("Delegation (25 gold)") == 1);
    CHECK(f.buttonsLabelled("Embassy (50 gold)") == 1);
    CHECK(f.buttonsLabelled("Propose Peace") == 0);
}

TEST_CASE("Declare War opens a casus belli picker; the chosen casus belli starts the war") {
    Fixture f;
    f.screen.open(f.ui);
    REQUIRE(f.clickButton("Declare War"));
    CHECK(f.buttonsLabelled("Declare War") == 0);
    CHECK(f.buttonsLabelled("Surprise War") == 1);
    CHECK(f.buttonsLabelled("Formal War") == 0); // not denounced
    CHECK(f.buttonsLabelled("Cancel") == 1);
    REQUIRE(f.clickButton("Cancel"));
    CHECK(f.buttonsLabelled("Declare War") == 1);

    REQUIRE(f.clickButton("Declare War"));
    REQUIRE(f.clickButton("Surprise War"));
    CHECK(f.d.isAtWar(PlayerId{0}, PlayerId{1}));
    CHECK_FALSE(f.screen.isOpen());
    f.screen.open(f.ui);
    CHECK(f.buttonsLabelled("Propose Peace") == 1);
    CHECK(f.buttonsLabelled("Declare War") == 0);
    CHECK(f.buttonsLabelled("Denounce") == 0);
    CHECK(f.labelsContaining("[AT WAR]") == 1);
}

TEST_CASE("Denounce marks the relation and unlocks Formal War in the picker") {
    Fixture f;
    f.screen.open(f.ui);
    REQUIRE(f.clickButton("Denounce"));
    CHECK(f.d.relation(PlayerId{0}, PlayerId{1}).denouncedOnTurn == f.world.gameState.currentTurn());
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("[Denounced]") == 1);
    CHECK(f.buttonsLabelled("Denounce") == 0);
    REQUIRE(f.clickButton("Declare War"));
    CHECK(f.buttonsLabelled("Formal War") == 1);
}

TEST_CASE("a rejected action leaves the state alone; an accepted one shows its tag") {
    Fixture f;
    f.screen.open(f.ui);
    REQUIRE(f.clickButton("Delegation (25 gold)")); // 0 gold
    CHECK_FALSE(f.d.relation(PlayerId{0}, PlayerId{1}).hasDelegation);
    f.world.gameState.player(PlayerId{0})->addGold(30);
    f.screen.open(f.ui);
    REQUIRE(f.clickButton("Delegation (25 gold)"));
    CHECK(f.d.relation(PlayerId{0}, PlayerId{1}).hasDelegation);
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("[Delegation]") == 1);
    CHECK(f.buttonsLabelled("Delegation (25 gold)") == 0);
    CHECK(f.buttonsLabelled("Embassy (50 gold)") == 1);
}
