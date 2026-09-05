/**
 * @file test_city_states_screen.cpp
 * @brief The City-States screen lists met city-states, sends envoys and rebuilds on refresh.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/ui/CityStatesScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Widget.hpp"

#include <string>
#include <variant>

using aoc::PlayerId;
using aoc::sim::CityStateComponent;
using aoc::sim::CityStateType;
using aoc::ui::ButtonData;
using aoc::ui::CityStatesScreen;
using aoc::ui::LabelData;
using aoc::ui::UIManager;
using aoc::ui::Widget;

namespace {

struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(2);
    UIManager ui;
    CityStatesScreen screen;

    Fixture() {
        this->world.gameState.initializeCityStateSlots(2);
        this->addCityState(2, CityStateType::Scientific, 10, 8); // Geneva, met
        this->addCityState(6, CityStateType::Trade, 4, 12);      // Lisbon, unmet
        this->world.gameState.cityStates()[0].setMet(PlayerId{0});
        this->world.gameState.cityStates()[0].envoys[0] = 1;
        this->world.gameState.player(PlayerId{0})->envoys().grant(2);

        this->ui.setScreenSize(1920.0f, 1200.0f);
        this->screen.setScreenSize(1920.0f, 1200.0f);
        this->screen.setContext(&this->world.gameState, &this->world.grid, PlayerId{0});
    }

    void addCityState(uint8_t defId, CityStateType type, int32_t q, int32_t r) {
        const std::size_t index = this->world.gameState.cityStates().size();
        CityStateComponent cs{};
        cs.defId    = defId;
        cs.type     = type;
        cs.location = {q, r};
        cs.envoys.fill(0);
        cs.suzerain = aoc::INVALID_PLAYER;
        this->world.gameState.cityStates().push_back(cs);
        aoc::game::Player* seat = this->world.gameState.player(
            static_cast<PlayerId>(aoc::sim::CITY_STATE_PLAYER_BASE + index));
        REQUIRE(seat != nullptr);
        seat->addCity({q, r}, std::string(aoc::sim::CITY_STATE_DEFS[defId].name));
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

TEST_CASE("met city-states are listed with envoys, suzerain, quest and three actions") {
    Fixture f;
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("1 of 2 city-states met  |  envoys available 2 (lifetime 2)") == 1);
    CHECK(f.labelsContaining("Geneva  (Scientific)  |  your envoys 1  |  suzerain: none  |  levy: free") == 1);
    CHECK(f.labelsContaining("Lisbon") == 0);
    CHECK(f.labelsContaining("no quest for you") == 1);
    CHECK(f.buttonsLabelled("Send Envoy") == 1);
    CHECK(f.buttonsLabelled("Levy (200 gold)") == 1);
    CHECK(f.buttonsLabelled("Bully") == 1);
}

TEST_CASE("Send Envoy spends the pool, the third envoy seats you, and refresh shows it") {
    Fixture f;
    f.screen.open(f.ui);
    REQUIRE(f.clickButton("Send Envoy"));
    CHECK(f.world.gameState.cityStates()[0].envoys[0] == 2);
    CHECK(f.world.gameState.player(PlayerId{0})->envoys().available == 1);
    f.screen.refresh(f.ui);
    CHECK(f.labelsContaining("your envoys 2  |  suzerain: none") == 1);

    REQUIRE(f.clickButton("Send Envoy"));
    f.screen.refresh(f.ui);
    CHECK(f.labelsContaining("your envoys 3  |  suzerain: you") == 1);
    CHECK(f.labelsContaining("envoys available 0 (lifetime 2)") == 1);

    // Pool empty: the request is rejected and nothing changes.
    REQUIRE(f.clickButton("Send Envoy"));
    CHECK(f.world.gameState.cityStates()[0].envoys[0] == 3);
}

TEST_CASE("Levy without gold and Bully under a rival suzerain are rejected without side effects") {
    Fixture f;
    f.world.gameState.cityStates()[0].envoys[1] = 4;
    f.world.gameState.cityStates()[0].suzerain  = f.world.gameState.cityStates()[0].computeSuzerain();
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("suzerain: none") == 0);
    REQUIRE(f.clickButton("Levy (200 gold)"));
    CHECK(f.world.gameState.cityStates()[0].levyPlayer == aoc::INVALID_PLAYER);
    REQUIRE(f.clickButton("Bully"));
    CHECK(f.world.gameState.cityStates()[0].envoys[0] == 1);
    CHECK(f.world.gameState.player(PlayerId{0})->treasury() == 0);
}

TEST_CASE("an active quest for the player is shown; close frees every widget") {
    Fixture f;
    aoc::sim::generateCityStateQuest(f.world.gameState, 0, PlayerId{0});
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("quest: Research a technology, +2 envoys, 30 turns left") == 1);
    CHECK(f.liveWidgets() > 5);
    f.screen.close(f.ui);
    CHECK(f.liveWidgets() == 0);
}
