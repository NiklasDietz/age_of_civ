/**
 * @file test_city_detail_screen.cpp
 * @brief The City Detail screen's new controls act through the shared city
 *        requests: project and removal buttons on the Production tab, focus
 *        buttons on the Citizens tab, and the housing / amenity readouts on the
 *        Overview tab. Civ VI plan Phase 2.2, 2026-09-05.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/CitySiege.hpp"
#include "aoc/ui/GameScreens.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Widget.hpp"

#include <string>
#include <variant>

using aoc::PlayerId;
using aoc::hex::AxialCoord;
using aoc::ui::ButtonData;
using aoc::ui::CityDetailScreen;
using aoc::ui::LabelData;
using aoc::ui::UIManager;
using aoc::ui::Widget;

namespace {

struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(1);
    UIManager ui;
    CityDetailScreen screen;
    const AxialCoord home{5, 5};

    Fixture() {
        aoc::game::City& city = aoc::test::addCityAt(this->world, PlayerId{0}, this->home.q, this->home.r, "Home");
        city.setPopulation(3);
        this->ui.setScreenSize(1920.0f, 1200.0f);
        this->screen.setScreenSize(1920.0f, 1200.0f);
        this->screen.setContext(&this->world.gameState, &this->world.grid, this->home, PlayerId{0});
    }

    aoc::game::City& city() { return *this->world.gameState.player(PlayerId{0})->cityAt(this->home); }

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

TEST_CASE("the overview shows housing, amenities and the governor seat") {
    Fixture f;
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("Housing: 3 /") == 1);
    CHECK(f.labelsContaining("Amenities:") == 1);
    CHECK(f.labelsContaining("Food stored:") == 1);
    CHECK(f.labelsContaining("Seated: nobody") == 1);
    f.screen.close(f.ui);
}

TEST_CASE("the production tab queues a project and drops it again") {
    Fixture f;
    f.screen.open(f.ui);
    f.screen.switchTab(f.ui, CityDetailScreen::TAB_PRODUCTION);
    CHECK(f.labelsContaining("Faith & Projects") == 1);
    REQUIRE(f.clickButton("Queue Bread and Circuses"));
    REQUIRE(f.city().production().queue.size() == 1);
    CHECK(f.city().production().queue.front().type == aoc::sim::ProductionItemType::Project);
    f.screen.switchTab(f.ui, CityDetailScreen::TAB_PRODUCTION);   // rebuild the rows
    REQUIRE(f.clickButton("Remove from queue"));
    CHECK(f.city().production().queue.empty());
    f.screen.close(f.ui);
}

TEST_CASE("the citizens tab sets the focus") {
    Fixture f;
    f.screen.open(f.ui);
    f.screen.switchTab(f.ui, CityDetailScreen::TAB_CITIZENS);
    CHECK(f.labelsContaining("Focus: Balanced") == 1);
    REQUIRE(f.clickButton("Focus Production"));
    CHECK(f.city().governor().focus == aoc::sim::CityFocus::Production);
    f.screen.switchTab(f.ui, CityDetailScreen::TAB_CITIZENS);
    CHECK(f.labelsContaining("Focus: Production") == 1);
    f.screen.close(f.ui);
}

TEST_CASE("the fate of a conquered city is offered only for a city someone else founded") {
    Fixture f;
    AxialCoord chosen{};
    uint8_t picked = 255;
    f.screen.setCityDispositionCallback([&chosen, &picked](AxialCoord at, uint8_t disposition) {
        chosen = at;
        picked = disposition;
    });

    SUBCASE("a city you founded offers nothing") {
        f.city().setOriginalOwner(PlayerId{0});
        f.screen.open(f.ui);
        CHECK(f.labelsContaining("Conquered city") == 0);
        CHECK_FALSE(f.clickButton("Raze this city"));
        CHECK_FALSE(f.clickButton("Liberate"));
        f.screen.close(f.ui);
    }

    SUBCASE("a conquered city offers both fates") {
        f.city().setOriginalOwner(PlayerId{1});
        f.city().setOriginalCapital(false);
        f.screen.open(f.ui);
        CHECK(f.labelsContaining("Conquered city") == 1);
        REQUIRE(f.clickButton("Raze this city"));
        CHECK(chosen == f.home);
        CHECK(picked == static_cast<uint8_t>(aoc::sim::CityDisposition::Raze));

        REQUIRE(f.clickButton("Liberate"));
        CHECK(picked == static_cast<uint8_t>(aoc::sim::CityDisposition::Liberate));
        f.screen.close(f.ui);
    }

    SUBCASE("a captured capital can be liberated but never burned") {
        f.city().setOriginalOwner(PlayerId{1});
        f.city().setOriginalCapital(true);
        f.screen.open(f.ui);
        CHECK(f.labelsContaining("Conquered city") == 1);
        CHECK_FALSE(f.clickButton("Raze this city")); // the button is absent, not refusing
        REQUIRE(f.clickButton("Liberate"));
        CHECK(picked == static_cast<uint8_t>(aoc::sim::CityDisposition::Liberate));
        f.screen.close(f.ui);
    }
}
