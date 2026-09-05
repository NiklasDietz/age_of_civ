/**
 * @file test_city_list_screen.cpp
 * @brief The City List screen shows one row per own city with population,
 *        production and loyalty, its Go and Open buttons fire the callbacks
 *        with the city's location, and refresh rebuilds only when a city
 *        changed. Civ VI plan Phase 2.3, 2026-09-05.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/CityActions.hpp"
#include "aoc/ui/CityListScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Widget.hpp"

#include <string>
#include <variant>
#include <vector>

using aoc::PlayerId;
using aoc::hex::AxialCoord;
using aoc::ui::ButtonData;
using aoc::ui::CityListScreen;
using aoc::ui::LabelData;
using aoc::ui::UIManager;
using aoc::ui::Widget;

namespace {

struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(2);
    UIManager ui;
    CityListScreen screen;
    std::vector<AxialCoord> jumps;
    std::vector<AxialCoord> opens;

    Fixture() {
        aoc::test::addCityAt(this->world, PlayerId{0}, 5, 5, "Alpha").setPopulation(4);
        aoc::test::addCityAt(this->world, PlayerId{0}, 12, 5, "Beta").setPopulation(2);
        aoc::test::addCityAt(this->world, PlayerId{1}, 18, 9, "Theirs").setPopulation(9);
        this->ui.setScreenSize(1920.0f, 1200.0f);
        this->screen.setScreenSize(1920.0f, 1200.0f);
        this->screen.setContext(&this->world.gameState, &this->world.grid, PlayerId{0});
        this->screen.setCallbacks([this](AxialCoord at) { this->jumps.push_back(at); },
                                  [this](AxialCoord at) { this->opens.push_back(at); });
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

    /// Click the n-th button (0-based) whose label equals `text`.
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

    [[nodiscard]] int32_t liveWidgets() const {
        int32_t n = 0;
        for (const Widget& w : this->ui.widgets()) {
            if (w.id != aoc::ui::INVALID_WIDGET) { ++n; }
        }
        return n;
    }
};

} // namespace

TEST_CASE("one row per own city with population, production and loyalty; foreign cities stay out") {
    Fixture f;
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("2 cities, 6 citizens") == 1);
    CHECK(f.labelsContaining("Alpha  pop 4/") == 1);
    CHECK(f.labelsContaining("Beta  pop 2/") == 1);
    CHECK(f.labelsContaining("Theirs") == 0);
    CHECK(f.labelsContaining("|  idle  |") == 2);
    CHECK(f.labelsContaining("loyalty 100") == 2);
    f.screen.close(f.ui);
    CHECK(f.liveWidgets() == 0);
}

TEST_CASE("Go and Open fire the callbacks with the row's city") {
    Fixture f;
    f.screen.open(f.ui);
    REQUIRE(f.clickNthButton("Go", 1));
    REQUIRE(f.jumps.size() == 1);
    CHECK(f.jumps[0] == AxialCoord{12, 5});
    REQUIRE(f.clickNthButton("Open", 0));
    REQUIRE(f.opens.size() == 1);
    CHECK(f.opens[0] == AxialCoord{5, 5});
    f.screen.close(f.ui);
}

TEST_CASE("refresh rebuilds when a city grows and the banner summary names the production") {
    Fixture f;
    f.screen.open(f.ui);
    const int32_t before = f.liveWidgets();
    f.screen.refresh(f.ui);
    CHECK(f.liveWidgets() == before);
    aoc::game::City& alpha = *f.world.gameState.player(PlayerId{0})->cityAt(AxialCoord{5, 5});
    alpha.setPopulation(5);
    aoc::sim::ProductionQueueItem item{};
    item.type      = aoc::sim::ProductionItemType::Building;
    item.itemId    = 16;
    item.name      = "Monument";
    item.totalCost = 60.0f;
    item.progress  = 30.0f;
    alpha.production().queue.push_back(item);
    f.screen.refresh(f.ui);
    CHECK(f.labelsContaining("Alpha  pop 5/") == 1);
    CHECK(f.labelsContaining("Monument") == 1);
    CHECK(aoc::sim::cityBannerSummary(alpha, 10.0f) == "Pop 5 | Monument 3t");
    CHECK(aoc::sim::cityBannerSummary(alpha, 0.0f) == "Pop 5 | Monument");
    CHECK(aoc::sim::cityGrowthFraction(alpha) >= 0.0f);
    f.screen.close(f.ui);
}
