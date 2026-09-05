/**
 * @file test_government_screen.cpp
 * @brief The Government screen slots and removes policy cards and adopts a
 *        government through the shared requests; every row is a real widget.
 *        Until 2026-09-05 the slots were labels (Civ VI plan Phase 2.1).
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/ui/GameScreens.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Widget.hpp"

#include <string>
#include <variant>

using aoc::PlayerId;
using aoc::sim::EMPTY_POLICY_SLOT;
using aoc::sim::GovernmentType;
using aoc::ui::ButtonData;
using aoc::ui::GovernmentScreen;
using aoc::ui::LabelData;
using aoc::ui::UIManager;
using aoc::ui::Widget;

namespace {

struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(1);
    UIManager ui;
    GovernmentScreen screen;

    Fixture() {
        aoc::sim::PlayerGovernmentComponent& gov = this->world.gameState.player(PlayerId{0})->government();
        gov.unlockPolicy(0);    // Discipline (Military)
        gov.unlockPolicy(6);    // Urban Planning (Economic)
        gov.unlockGovernment(GovernmentType::Oligarchy);
        gov.policySwapFree = true;
        this->ui.setScreenSize(1920.0f, 1200.0f);
        this->screen.setScreenSize(1920.0f, 1200.0f);
        this->screen.setContext(&this->world.gameState, PlayerId{0});
    }

    aoc::sim::PlayerGovernmentComponent& gov() {
        return this->world.gameState.player(PlayerId{0})->government();
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

TEST_CASE("slot rows show their type, Slot fills the matching slot, Remove clears it") {
    Fixture f;
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("[Military] empty") == 1);
    CHECK(f.labelsContaining("[Economic] empty") == 1);
    CHECK(f.labelsContaining("rearranging cards is free") == 1);

    REQUIRE(f.clickButton("Slot Discipline"));
    CHECK(f.gov().activePolicies[0] == 0);
    f.screen.refresh(f.ui);   // the fingerprint moved: rows rebuilt
    CHECK(f.labelsContaining("[Military] Discipline") == 1);
    CHECK(f.labelsContaining("[Economic] empty") == 1);

    REQUIRE(f.clickButton("Slot Urban Planning"));
    CHECK(f.gov().activePolicies[1] == 6);
    f.screen.refresh(f.ui);
    CHECK(f.labelsContaining("No unlocked card is waiting") == 1);

    REQUIRE(f.clickButton("Remove"));
    CHECK(f.gov().activePolicies[0] == EMPTY_POLICY_SLOT);
    f.screen.refresh(f.ui);
    CHECK(f.labelsContaining("[Military] empty") == 1);
    f.screen.close(f.ui);
}

TEST_CASE("adopting a government goes through the request and relabels the screen") {
    Fixture f;
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("Current: Chiefdom") == 1);
    CHECK(f.labelsContaining("Autocracy (locked)") == 1);
    REQUIRE(f.clickButton("Oligarchy"));
    CHECK(f.gov().government == GovernmentType::Oligarchy);
    CHECK_FALSE(f.gov().isInAnarchy());
    f.screen.refresh(f.ui);
    CHECK(f.labelsContaining("Current: Oligarchy") == 1);
    CHECK(f.labelsContaining("[Diplomatic] empty") == 1);   // Oligarchy: 1 M, 1 E, 1 D
    f.screen.close(f.ui);
}
