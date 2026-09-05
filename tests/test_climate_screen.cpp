/**
 * @file test_climate_screen.cpp
 * @brief The Climate screen shows temperature, CO2, sea level and recent disasters.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/climate/NaturalDisasters.hpp"
#include "aoc/ui/ClimateScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Widget.hpp"

#include <string>
#include <variant>

using aoc::PlayerId;
using aoc::ui::ClimateScreen;
using aoc::ui::LabelData;
using aoc::ui::UIManager;
using aoc::ui::Widget;

namespace {

struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(2);
    UIManager ui;
    ClimateScreen screen;

    Fixture() {
        this->world.gameState.players()[1]->setCivId(static_cast<aoc::sim::CivId>(1));
        this->world.gameState.climate().globalTemperature = 1.25f;
        this->world.gameState.climate().co2Level          = 3500.0f;
        this->world.gameState.climate().seaLevelRise      = 2;
        aoc::sim::recordDisaster(this->world.gameState, aoc::sim::DisasterType::VolcanicEruption, 40, {7, 3}, 3,
                                 PlayerId{0});
        aoc::sim::recordDisaster(this->world.gameState, aoc::sim::DisasterType::Hurricane, 52, {12, 9}, 2, PlayerId{1});
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

    [[nodiscard]] int32_t liveWidgets() const {
        int32_t n = 0;
        for (const Widget& w : this->ui.widgets()) {
            if (w.id != aoc::ui::INVALID_WIDGET) { ++n; }
        }
        return n;
    }
};

} // namespace

TEST_CASE("the climate rows show the numbers and the disasters newest first") {
    Fixture f;
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("Temperature +1.25 C  |  CO2 3500  |  2 coast tiles flooded") == 1);
    CHECK(f.labelsContaining("Global temperature: +1.25 C") == 1);
    CHECK(f.labelsContaining("CO2: 3500 (natural sink 0.5 per turn; food penalty starts at 3000)") == 1);
    CHECK(f.labelsContaining("Sea level: 2 coast tiles flooded (flood emergency at 5)") == 1);
    CHECK(f.labelsContaining("RECENT DISASTERS") == 1);
    CHECK(f.labelsContaining("Turn 40  Volcanic Eruption at (7,3), severity 3, Rome") == 1);
    CHECK(f.labelsContaining("Turn 52  Hurricane at (12,9), severity 2, Egypt") == 1);
    CHECK(f.labelsContaining("None so far") == 0);
}

TEST_CASE("a new disaster rebuilds the rows on refresh and close frees every widget") {
    Fixture f;
    f.screen.open(f.ui);
    aoc::sim::recordDisaster(f.world.gameState, aoc::sim::DisasterType::Drought, 60, {2, 2}, 1, aoc::INVALID_PLAYER);
    f.screen.refresh(f.ui);
    CHECK(f.labelsContaining("Turn 60  Drought at (2,2), severity 1, unclaimed land") == 1);
    f.screen.close(f.ui);
    CHECK(f.liveWidgets() == 0);
}
