/**
 * @file test_great_works_screen.cpp
 * @brief The Great Works screen lists works by city, moves them and shows the tourism race.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/culture/GreatWorks.hpp"
#include "aoc/simulation/greatpeople/GreatPeopleExpanded.hpp"
#include "aoc/ui/GreatWorksScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Widget.hpp"

#include <string>
#include <variant>

using aoc::BuildingId;
using aoc::PlayerId;
using aoc::sim::DistrictType;
using aoc::sim::GreatWorkType;
using aoc::ui::ButtonData;
using aoc::ui::GreatWorksScreen;
using aoc::ui::LabelData;
using aoc::ui::UIManager;
using aoc::ui::Widget;

namespace {

constexpr BuildingId AMPHITHEATER{39}; // 2 slots
constexpr BuildingId ART_MUSEUM{40};   // 3 slots

void addTheatre(aoc::game::City& city, BuildingId building) {
    city.districts().districts.push_back(
        {DistrictType::Theatre, {city.location().q + 1, city.location().r}, {building}});
}

struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(2);
    UIManager ui;
    GreatWorksScreen screen;

    Fixture() {
        aoc::game::City& alpha = aoc::test::addCityAt(this->world, PlayerId{0}, 5, 5, "Alpha");
        aoc::game::City& beta  = aoc::test::addCityAt(this->world, PlayerId{0}, 12, 5, "Beta");
        aoc::game::City& theirs = aoc::test::addCityAt(this->world, PlayerId{1}, 18, 9, "Theirs");
        addTheatre(alpha, ART_MUSEUM);
        addTheatre(beta, AMPHITHEATER);
        addTheatre(theirs, AMPHITHEATER);
        REQUIRE(aoc::sim::placeGreatWork(alpha, {GreatWorkType::Art, PlayerId{0}, 2, 30}));
        REQUIRE(aoc::sim::placeGreatWork(theirs, {GreatWorkType::Art, PlayerId{1}, 3, 31}));
        aoc::game::Player& me = *this->world.gameState.player(PlayerId{0});
        me.tourism().tourismPerTurn   = 4.0f;
        me.tourism().foreignTourists  = 12;
        me.tourism().domesticTourists = 8;
        this->world.gameState.player(PlayerId{1})->tourism().domesticTourists = 30;

        this->ui.setScreenSize(1920.0f, 1200.0f);
        this->screen.setScreenSize(1920.0f, 1200.0f);
        this->screen.setContext(&this->world.gameState, &this->world.grid, PlayerId{0});
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

    [[nodiscard]] int32_t liveWidgets() const {
        int32_t n = 0;
        for (const Widget& w : this->ui.widgets()) {
            if (w.id != aoc::ui::INVALID_WIDGET) { ++n; }
        }
        return n;
    }
};

} // namespace

TEST_CASE("works are listed per own city with a move button per city that has a free slot") {
    Fixture f;
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("1 works in 5 slots  |  tourism +4 per turn  |  12 foreign tourists") == 1);
    CHECK(f.labelsContaining("Alpha  works 1/3") == 1);
    CHECK(f.labelsContaining("Beta  works 0/2") == 1);
    CHECK(f.labelsContaining("Theirs") == 0);
    const std::string creator(aoc::sim::namedGreatPersonDef(2).name);
    CHECK(f.labelsContaining("Art by " + creator + " (turn 30)") == 1);
    CHECK(f.buttonsLabelled("Move to Beta") == 1);
    CHECK(f.buttonsLabelled("Move to Alpha") == 0);
}

TEST_CASE("the move button moves the work and the screen rebuilds on refresh") {
    Fixture f;
    f.screen.open(f.ui);
    REQUIRE(f.clickButton("Move to Beta"));
    aoc::game::Player& me = *f.world.gameState.player(PlayerId{0});
    CHECK(me.cities()[0]->greatWorks().works.empty());
    REQUIRE(me.cities()[1]->greatWorks().works.size() == 1);

    f.screen.refresh(f.ui);
    CHECK(f.labelsContaining("Alpha  works 0/3") == 1);
    CHECK(f.labelsContaining("Beta  works 1/2") == 1);
    CHECK(f.buttonsLabelled("Move to Alpha") == 1);
    CHECK(f.buttonsLabelled("Move to Beta") == 0);
}

TEST_CASE("the tourism race compares foreign tourists with each rival's domestic tourists") {
    Fixture f;
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("foreign tourists 12  |  domestic tourists 8") == 1);
    CHECK(f.labelsContaining("their domestic 30  yours foreign 12  (behind)") == 1);

    f.world.gameState.player(PlayerId{1})->tourism().domesticTourists = 5;
    f.world.gameState.player(PlayerId{0})->tourism().foreignTourists = 13; // fingerprint changes
    f.screen.refresh(f.ui);
    CHECK(f.labelsContaining("their domestic 5  yours foreign 13  (ahead)") == 1);
}

TEST_CASE("close frees every widget") {
    Fixture f;
    f.screen.open(f.ui);
    CHECK(f.liveWidgets() > 5);
    f.screen.close(f.ui);
    CHECK(f.liveWidgets() == 0);
}
