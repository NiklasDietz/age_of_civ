/**
 * @file test_unit_list_screen.cpp
 * @brief The Unit List screen shows one row per own unit with position, health,
 *        movement and state; Go and Select fire the callbacks with the unit's
 *        tile; refresh rebuilds when a unit moves. Civ VI plan Phase 2.3.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/UnitListScreen.hpp"
#include "aoc/ui/Widget.hpp"

#include <string>
#include <variant>
#include <vector>

using aoc::PlayerId;
using aoc::UnitTypeId;
using aoc::hex::AxialCoord;
using aoc::ui::ButtonData;
using aoc::ui::LabelData;
using aoc::ui::UIManager;
using aoc::ui::UnitListScreen;
using aoc::ui::Widget;

namespace {

struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(2);
    UIManager ui;
    UnitListScreen screen;
    std::vector<AxialCoord> jumps;
    std::vector<AxialCoord> selects;

    Fixture() {
        aoc::test::addUnitAt(this->world, PlayerId{0}, UnitTypeId{0}, 5, 5);   // Warrior
        aoc::test::addUnitAt(this->world, PlayerId{0}, UnitTypeId{5}, 6, 5).setState(aoc::sim::UnitState::Fortified);
        aoc::test::addUnitAt(this->world, PlayerId{1}, UnitTypeId{0}, 15, 5);
        this->ui.setScreenSize(1920.0f, 1200.0f);
        this->screen.setScreenSize(1920.0f, 1200.0f);
        this->screen.setContext(&this->world.gameState, &this->world.grid, PlayerId{0});
        this->screen.setCallbacks([this](AxialCoord at) { this->jumps.push_back(at); },
                                  [this](AxialCoord at) { this->selects.push_back(at); });
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

    bool clickNthButton(const std::string& text, int32_t n) {
        int32_t seen = 0;
        for (const Widget& w : this->ui.widgets()) {
            if (w.id == aoc::ui::INVALID_WIDGET) { continue; }
            const ButtonData* btn = std::get_if<ButtonData>(&w.data);
            if (btn != nullptr && btn->label == text) {
                if (seen == n) {
                    const aoc::ui::WidgetId id = w.id;
                    return this->ui.clickWidget(id);
                }
                ++seen;
            }
        }
        return false;
    }
};

} // namespace

TEST_CASE("one row per own unit with its tile, health, moves and state") {
    Fixture f;
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("2 units, 1 military") == 1);
    CHECK(f.labelsContaining("Warrior  at (5,5)") == 1);
    CHECK(f.labelsContaining("Builder  at (6,5)") == 1);
    CHECK(f.labelsContaining("fortified") == 1);
    CHECK(f.labelsContaining("at (15,5)") == 0);
    f.screen.close(f.ui);
}

TEST_CASE("Go and Select fire the callbacks with the row's unit; refresh follows a move") {
    Fixture f;
    f.screen.open(f.ui);
    REQUIRE(f.clickNthButton("Go", 0));
    REQUIRE(f.jumps.size() == 1);
    CHECK(f.jumps[0] == AxialCoord{5, 5});
    REQUIRE(f.clickNthButton("Select", 1));
    REQUIRE(f.selects.size() == 1);
    CHECK(f.selects[0] == AxialCoord{6, 5});

    aoc::game::Unit* warrior = f.world.gameState.player(PlayerId{0})->unitAt(AxialCoord{5, 5});
    REQUIRE(warrior != nullptr);
    warrior->setPosition(AxialCoord{7, 7});
    f.screen.refresh(f.ui);
    CHECK(f.labelsContaining("Warrior  at (7,7)") == 1);
    f.screen.close(f.ui);
}
