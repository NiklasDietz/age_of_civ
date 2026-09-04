/**
 * @file test_espionage_screen.cpp
 * @brief The read-only EspionageScreen lists every spy of the human player with
 *        all 18 missions and their success chance, every rival city with its
 *        intelligence level, rebuilds only when spy state changes, and tears
 *        down cleanly. Built against a bare UIManager and a hand-made GameState.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/Espionage.hpp"
#include "aoc/ui/EspionageScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Widget.hpp"

#include <string>
#include <variant>

using aoc::ui::ButtonData;
using aoc::ui::EspionageScreen;
using aoc::ui::LabelData;
using aoc::ui::UIManager;
using aoc::ui::Widget;
using aoc::ui::WidgetId;

namespace {

constexpr aoc::UnitTypeId SPY{101};
constexpr float VIEWPORT_W = 1920.0f;
constexpr float VIEWPORT_H = 1200.0f;

/// Two civs: the human (0) with one spy standing in the rival's capital on a
/// three-turn Steal Technology run, and a rival (1) whose counter-spy guards it.
struct World {
    aoc::game::GameState gs;
    aoc::map::HexGrid grid;
    aoc::sim::DiplomacyManager diplomacy;
    UIManager ui;
    EspionageScreen screen;

    World() {
        this->grid.initialize(24, 16);
        for (int32_t i = 0; i < this->grid.tileCount(); ++i) {
            this->grid.setTerrain(i, aoc::map::TerrainType::Grassland);
        }
        this->gs.initialize(2);
        this->diplomacy.initialize(2);
        aoc::game::Player& human = *this->gs.players()[0];
        aoc::game::Player& rival = *this->gs.players()[1];
        human.addCity({3, 3}, "Home").setOriginalCapital(true);
        rival.addCity({12, 9}, "Thebes").setOriginalCapital(true);
        aoc::game::Unit& spy     = human.addUnit(SPY, {12, 9});
        spy.spy().currentMission = aoc::sim::SpyMission::StealTechnology;
        spy.spy().turnsRemaining = 3;
        aoc::game::Unit& guard     = rival.addUnit(SPY, {12, 9});
        guard.spy().currentMission = aoc::sim::SpyMission::CounterIntelligence;
        this->diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).intelLevel =
            static_cast<uint8_t>(aoc::sim::IntelligenceLevel::Economic);
        this->ui.setScreenSize(VIEWPORT_W, VIEWPORT_H);
        this->screen.setScreenSize(VIEWPORT_W, VIEWPORT_H);
        this->screen.setContext(&this->gs, &this->grid, aoc::PlayerId{0}, &this->diplomacy);
    }

    [[nodiscard]] int32_t liveWidgets() const {
        int32_t n = 0;
        for (const Widget& w : this->ui.widgets()) {
            if (w.id != aoc::ui::INVALID_WIDGET) {
                ++n;
            }
        }
        return n;
    }

    [[nodiscard]] int32_t buttonsContaining(const std::string& needle, bool disabled) const {
        int32_t n = 0;
        for (const Widget& w : this->ui.widgets()) {
            if (w.id == aoc::ui::INVALID_WIDGET) {
                continue;
            }
            const ButtonData* btn = std::get_if<ButtonData>(&w.data);
            if (btn != nullptr && btn->disabled == disabled &&
                btn->label.find(needle) != std::string::npos) {
                ++n;
            }
        }
        return n;
    }

    /// Click the first button whose label contains `needle`; false when none.
    bool clickButton(const std::string& needle) {
        for (const Widget& w : this->ui.widgets()) {
            if (w.id == aoc::ui::INVALID_WIDGET) {
                continue;
            }
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
            if (w.id == aoc::ui::INVALID_WIDGET) {
                continue;
            }
            const LabelData* label = std::get_if<LabelData>(&w.data);
            if (label != nullptr && label->text.find(needle) != std::string::npos) {
                ++n;
            }
        }
        return n;
    }
};

} // namespace

TEST_CASE("open lists the spy, its mission, and all 18 missions with a chance") {
    World w;
    w.screen.open(w.ui);
    REQUIRE(w.screen.isOpen());

    CHECK(w.labelsContaining("Spies: 1   Counter-intelligence: 0   Revealed: 0") == 1);
    CHECK(w.labelsContaining("Agent at Thebes (") == 1);
    CHECK(w.labelsContaining("Steal Technology, 3 turns left") == 1);
    CHECK(w.labelsContaining("(busy until this one resolves) (counter-spy level ") == 1);
    for (const aoc::sim::SpyMissionDef& def : aoc::sim::SPY_MISSION_DEFS) {
        CHECK(w.labelsContaining("      " + std::string(def.name) + "  ") >= 1);
    }
    CHECK(w.labelsContaining("%  ") == 18);
    CHECK(w.labelsContaining("(ongoing)") == 4);
}

TEST_CASE("rivals section shows the intelligence level and the stationed spy") {
    World w;
    w.screen.open(w.ui);

    CHECK(w.labelsContaining("|  Intel: Economic") == 1);
    CHECK(w.labelsContaining("Thebes (12,9)  |  your spies: 1  |  counter-spy level ") == 1);
    CHECK(w.labelsContaining("No missions resolved yet.") == 1);
}

TEST_CASE("without spies the screen says so instead of listing missions") {
    World w;
    aoc::game::Player& human = *w.gs.players()[0];
    human.removeUnit(human.units().front().get());
    w.screen.open(w.ui);

    CHECK(w.labelsContaining("Spies: 0") == 1);
    CHECK(w.labelsContaining("No spies.") == 1);
    CHECK(w.labelsContaining("%  ") == 0);
    CHECK(w.labelsContaining("|  Intel: Economic") == 1);
}

TEST_CASE("refresh rebuilds only when spy state changes; close removes everything") {
    World w;
    const int32_t before = w.liveWidgets();
    w.screen.open(w.ui);
    const int32_t opened = w.liveWidgets();
    CHECK(opened > before + 20);

    w.screen.refresh(w.ui);
    CHECK(w.liveWidgets() == opened);
    CHECK(w.labelsContaining("3 turns left") == 1);

    aoc::game::Unit& spy     = *w.gs.players()[0]->units().front();
    spy.spy().turnsRemaining = 1;
    w.screen.refresh(w.ui);
    CHECK(w.labelsContaining("1 turns left") == 1);
    CHECK(w.labelsContaining("3 turns left") == 0);

    w.screen.close(w.ui);
    CHECK_FALSE(w.screen.isOpen());
    CHECK(w.liveWidgets() == before);
}

TEST_CASE("an idle spy gets one button per mission; a click assigns through the action path") {
    World w;
    aoc::game::Unit& spy     = *w.gs.players()[0]->units().front();
    spy.spy().turnsRemaining = 0;
    w.screen.open(w.ui);

    CHECK(w.buttonsContaining("%  ", false) == 18);
    CHECK(w.labelsContaining("%  ") == 0);
    CHECK(w.labelsContaining("click one to assign") == 1);

    REQUIRE(w.clickButton("Steal Technology  "));
    CHECK(spy.spy().currentMission == aoc::sim::SpyMission::StealTechnology);
    CHECK(spy.spy().turnsRemaining > 0);
    CHECK(spy.spy().location == spy.position());

    w.screen.refresh(w.ui);
    CHECK(w.buttonsContaining("%  ", false) == 0);
    CHECK(w.labelsContaining("%  ") == 18);
    CHECK(w.labelsContaining("Steal Technology, ") == 1);
}

TEST_CASE("outside a rival city only Counter-Intelligence is enabled") {
    World w;
    aoc::game::Player& human = *w.gs.players()[0];
    aoc::game::Unit& homeSpy = human.addUnit(SPY, {3, 3});
    w.screen.open(w.ui);

    // The Thebes spy is busy (label rows); the home spy contributes the buttons.
    CHECK(w.buttonsContaining("%  ", true) == 17);
    CHECK(w.buttonsContaining("%  ", false) == 1);
    CHECK(w.buttonsContaining("Counter-Intelligence", false) == 1);

    // A disabled button never reaches the action.
    static_cast<void>(w.clickButton("Steal Technology  "));
    CHECK(homeSpy.spy().currentMission == aoc::sim::SpyMission::GatherIntelligence);
    REQUIRE(w.clickButton("Counter-Intelligence"));
    CHECK(homeSpy.spy().currentMission == aoc::sim::SpyMission::CounterIntelligence);
}

TEST_CASE("history lists own outcomes and enemy spies caught at home, never escaped ones") {
    World w;
    aoc::game::GameState::SpyMissionRecord own{};
    own.spyOwner    = aoc::PlayerId{0};
    own.targetOwner = aoc::PlayerId{1};
    own.location    = {12, 9};
    own.mission     = aoc::sim::SpyMission::StealTechnology;
    own.success     = true;
    w.gs.recordSpyMission(own);
    aoc::game::GameState::SpyMissionRecord caught{};
    caught.spyOwner    = aoc::PlayerId{1};
    caught.targetOwner = aoc::PlayerId{0};
    caught.location    = {3, 3};
    caught.mission     = aoc::sim::SpyMission::SiphonFunds;
    caught.success     = false;
    caught.outcome     = aoc::sim::SpyFailureOutcome::Captured;
    w.gs.recordSpyMission(caught);
    aoc::game::GameState::SpyMissionRecord unseen = caught;
    unseen.outcome = aoc::sim::SpyFailureOutcome::EscapedUndetected;
    w.gs.recordSpyMission(unseen);
    w.screen.open(w.ui);

    CHECK(w.labelsContaining("T0  Steal Technology at Thebes: success") == 1);
    CHECK(w.labelsContaining("spy in Home (Captured)") == 1);
    CHECK(w.labelsContaining("(Escaped)") == 0);
    CHECK(w.labelsContaining("No missions resolved yet.") == 0);
}
