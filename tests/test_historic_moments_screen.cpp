/**
 * @file test_historic_moments_screen.cpp
 * @brief The Historic Moments screen shows the age, the era score against its
 *        thresholds, the lifetime score and the timeline newest first, rebuilds only
 *        when the timeline moves, and closes cleanly.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/tech/EraScore.hpp"
#include "aoc/ui/HistoricMomentsScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Widget.hpp"

#include <string>
#include <variant>

using aoc::PlayerId;
using aoc::ui::HistoricMomentsScreen;
using aoc::ui::LabelData;
using aoc::ui::UIManager;
using aoc::ui::Widget;

namespace {

struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(2);
    UIManager ui;
    HistoricMomentsScreen screen;

    Fixture() {
        this->ui.setScreenSize(1920.0f, 1200.0f);
        this->screen.setScreenSize(1920.0f, 1200.0f);
        this->screen.setContext(&this->world.gameState, &this->world.grid, PlayerId{0});
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

TEST_CASE("a fresh player sees a Normal age, zero scores and an empty timeline") {
    Fixture f;
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("Age: Normal   Era score: 0 (golden at ") == 1);
    CHECK(f.labelsContaining("Lifetime: 0") == 1);
    CHECK(f.labelsContaining("Nothing yet.") == 1);
    CHECK(f.labelsContaining("TIMELINE (newest first)") == 1);
}

TEST_CASE("moments are listed newest first with their turn and points; the age shows its timer") {
    Fixture f;
    aoc::game::Player& p = *f.world.gameState.player(PlayerId{0});
    aoc::sim::addEraScore(p, 12, 3, "Completed the Pyramids");
    aoc::sim::addEraScore(p, 15, 2, "Researched Mining");
    p.eraScore().currentAgeType = aoc::sim::AgeType::Golden;
    p.eraScore().turnsRemaining = 7;
    f.screen.open(f.ui);

    CHECK(f.labelsContaining("Age: Golden (7 turns left)   Era score: 5") == 1);
    CHECK(f.labelsContaining("Lifetime: 5") == 1);
    CHECK(f.labelsContaining("Turn 15   +2   Researched Mining") == 1);
    CHECK(f.labelsContaining("Turn 12   +3   Completed the Pyramids") == 1);
    CHECK(f.labelsContaining("Nothing yet.") == 0);

    // Newest first: the Mining row is created before the Pyramids row.
    int32_t miningIndex   = -1;
    int32_t pyramidsIndex = -1;
    int32_t index         = 0;
    for (const Widget& w : f.ui.widgets()) {
        if (w.id == aoc::ui::INVALID_WIDGET) {
            continue;
        }
        const LabelData* label = std::get_if<LabelData>(&w.data);
        if (label != nullptr && label->text.find("Researched Mining") != std::string::npos) {
            miningIndex = index;
        }
        if (label != nullptr && label->text.find("Completed the Pyramids") != std::string::npos) {
            pyramidsIndex = index;
        }
        ++index;
    }
    CHECK(miningIndex >= 0);
    CHECK(miningIndex < pyramidsIndex);
}

TEST_CASE("refresh rebuilds only when the timeline moves; close removes every widget") {
    Fixture f;
    aoc::game::Player& p = *f.world.gameState.player(PlayerId{0});
    const int32_t before = f.liveWidgets();
    f.screen.open(f.ui);
    const int32_t opened = f.liveWidgets();
    f.screen.refresh(f.ui);
    CHECK(f.liveWidgets() == opened);

    aoc::sim::addEraScore(p, 40, 3, "Captured Thebes");
    f.screen.refresh(f.ui);
    CHECK(f.labelsContaining("Turn 40   +3   Captured Thebes") == 1);

    f.screen.close(f.ui);
    CHECK_FALSE(f.screen.isOpen());
    CHECK(f.liveWidgets() == before);
}
