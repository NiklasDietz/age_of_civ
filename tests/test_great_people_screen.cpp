/**
 * @file test_great_people_screen.cpp
 * @brief The read-only Great People screen shows per-type progress with its
 *        sources, the recruited people with what activation actually does,
 *        rebuilds when the state moves, and tears down cleanly.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/greatpeople/GreatPeople.hpp"
#include "aoc/simulation/greatpeople/GreatPeopleExpanded.hpp"
#include "aoc/ui/GreatPeopleScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Widget.hpp"

#include <string>
#include <variant>

using aoc::PlayerId;
using aoc::sim::DistrictType;
using aoc::sim::GreatPersonType;
using aoc::ui::ButtonData;
using aoc::ui::GreatPeopleScreen;
using aoc::ui::LabelData;
using aoc::ui::UIManager;
using aoc::ui::Widget;
using aoc::ui::WidgetId;

namespace {

constexpr aoc::BuildingId LIBRARY{7};

/// One human civ with a capital holding a Campus (with a Library) and an empty
/// Industrial Zone: 2 Scientist + 1 Artist + 1 Engineer points per turn.
struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(1);
    UIManager ui;
    GreatPeopleScreen screen;

    Fixture() {
        aoc::game::City& capital = aoc::test::addCityAt(this->world, PlayerId{0}, 5, 5, "Home");
        aoc::sim::CityDistrictsComponent::PlacedDistrict campus{};
        campus.type     = DistrictType::Campus;
        campus.location = {6, 5};
        campus.buildings.push_back(LIBRARY);
        capital.districts().districts.push_back(campus);
        aoc::sim::CityDistrictsComponent::PlacedDistrict industrial{};
        industrial.type     = DistrictType::Industrial;
        industrial.location = {4, 5};
        capital.districts().districts.push_back(industrial);

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

    /// Click the first button whose label contains `needle`; false when none.
    bool clickButton(const std::string& needle) {
        for (const Widget& w : this->ui.widgets()) {
            if (w.id == aoc::ui::INVALID_WIDGET) { continue; }
            const ButtonData* btn = std::get_if<ButtonData>(&w.data);
            if (btn != nullptr && btn->label.find(needle) != std::string::npos) {
                const WidgetId id = w.id;
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

TEST_CASE("progress rows show points against the threshold and where they come from") {
    Fixture f;
    aoc::sim::accumulateGreatPeoplePoints(f.world.gameState, PlayerId{0});
    f.screen.open(f.ui);

    CHECK(f.labelsContaining("Recruited: 0   Waiting to act: 0   Types exhausted: 0") == 1);
    CHECK(f.labelsContaining("Scientist  2 / 60 points  |  recruited 0 of 12") == 1);
    CHECK(f.labelsContaining("Sources: 1 districts, 1 buildings") == 1);   // the Campus
    CHECK(f.labelsContaining("Engineer  1 / 60 points") == 1);
    CHECK(f.labelsContaining("Artist  1 / 60 points") == 1);
    CHECK(f.labelsContaining("Sources: 1 Libraries") == 1);
    CHECK(f.labelsContaining("Sources: 0 districts, 0 buildings  (none yet)") == 2);  // General, Merchant
    CHECK(f.labelsContaining("None yet.") == 1);
}

TEST_CASE("a recruited person appears with the effect activation really applies") {
    Fixture f;
    aoc::game::Player& p = *f.world.gameState.players()[0];
    p.greatPeople().points[static_cast<uint8_t>(GreatPersonType::Scientist)] =
        p.greatPeople().threshold(GreatPersonType::Scientist) + 1.0f;
    aoc::sim::checkGreatPeopleRecruitment(f.world.gameState, PlayerId{0});
    f.screen.open(f.ui);

    const aoc::sim::NamedGreatPersonDef& first =
        aoc::sim::namedGreatPersonForCategory(aoc::sim::GreatPersonCategory::Scientist, 0);
    CHECK(f.labelsContaining("Waiting to act: 1") == 1);
    CHECK(f.labelsContaining("Scientist " + std::string(first.name) + "  at (5,5)") == 1);
    CHECK(f.labelsContaining(std::string(first.abilityName) + "  |  does: ") == 1);
    CHECK(f.labelsContaining("recruited 1 of 12") == 1);
    CHECK(f.labelsContaining("None yet.") == 0);
}

TEST_CASE("an exhausted type is labelled and refresh rebuilds only on change") {
    Fixture f;
    f.screen.open(f.ui);
    const int32_t opened = f.liveWidgets();
    f.screen.refresh(f.ui);
    CHECK(f.liveWidgets() == opened);
    CHECK(f.labelsContaining("EXHAUSTED") == 0);

    aoc::game::Player& p = *f.world.gameState.players()[0];
    p.greatPeople().exhausted[static_cast<uint8_t>(GreatPersonType::General)] = true;
    f.screen.refresh(f.ui);
    CHECK(f.labelsContaining("General  EXHAUSTED") == 1);
    CHECK(f.labelsContaining("Types exhausted: 1") == 1);
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

TEST_CASE("the Activate button consumes the person through the shared request") {
    Fixture f;
    aoc::game::Player& p = *f.world.gameState.players()[0];
    p.greatPeople().points[static_cast<uint8_t>(GreatPersonType::Scientist)] =
        p.greatPeople().threshold(GreatPersonType::Scientist) + 1.0f;
    aoc::sim::checkGreatPeopleRecruitment(f.world.gameState, PlayerId{0});
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("Waiting to act: 1") == 1);

    REQUIRE(f.clickButton("Activate "));
    CHECK(p.unitCount() == 0);            // consumed
    f.screen.refresh(f.ui);               // fingerprint changed: rows rebuilt
    CHECK(f.labelsContaining("Waiting to act: 0") == 1);
    CHECK(f.labelsContaining("recruited 1 of 12") == 1);
    CHECK(f.labelsContaining("None yet.") == 1);
}
