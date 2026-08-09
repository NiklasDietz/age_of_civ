/**
 * @file test_ui_game_setup_layout.cpp
 * @brief Layout guards for the Game Setup screen and pan clamping.
 *
 * The Game Setup screen was reported as (a) needing a scrollbar even though
 * the window was large, and (b) bleeding rows outside the panel while
 * scrolling. Both were layout/clipping bugs, so assert the structural
 * properties here rather than relying on a screenshot.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/ui/MainMenu.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Widget.hpp"

#include <vector>

using aoc::ui::GameSetupConfig;
using aoc::ui::GameSetupScreen;
using aoc::ui::LayoutDirection;
using aoc::ui::PanelData;
using aoc::ui::Rect;
using aoc::ui::ScrollListData;
using aoc::ui::UIManager;
using aoc::ui::Widget;
using aoc::ui::WidgetId;

namespace {

constexpr float VIEWPORT_W = 1920.0f;
constexpr float VIEWPORT_H = 1200.0f;

/// Build the Game Setup screen at a desktop viewport and lay it out.
struct SetupFixture {
    UIManager ui;
    GameSetupScreen screen;

    SetupFixture() {
        this->ui.setScreenSize(VIEWPORT_W, VIEWPORT_H);
        this->screen.build(
            this->ui, VIEWPORT_W, VIEWPORT_H, [](const GameSetupConfig&) {}, []() {});
        this->ui.layout();
    }
};

} // namespace

TEST_CASE("game setup uses most of the viewport") {
    SetupFixture f;
    // The old screen was a fixed 550px column, which is what forced scrolling.
    // Find the widest panel; it should be a large fraction of the viewport.
    float widest = 0.0f;
    for (const Widget& w : f.ui.widgets()) {
        if (w.id == aoc::ui::INVALID_WIDGET || !w.isVisible) {
            continue;
        }
        // Skip the full-screen backdrop.
        if (w.computedBounds.w >= VIEWPORT_W) {
            continue;
        }
        widest = std::max(widest, w.computedBounds.w);
    }
    CHECK(widest > 900.0f);
}

TEST_CASE("game setup body is two side-by-side scroll columns") {
    SetupFixture f;
    std::vector<const Widget*> scrollLists;
    for (const Widget& w : f.ui.widgets()) {
        if (w.id == aoc::ui::INVALID_WIDGET || !w.isVisible) {
            continue;
        }
        if (std::holds_alternative<ScrollListData>(w.data)) {
            scrollLists.push_back(&w);
        }
    }
    REQUIRE(scrollLists.size() >= 2);

    const Widget* a = scrollLists[0];
    const Widget* b = scrollLists[1];
    // Side by side, not stacked: distinct x, overlapping y band.
    CHECK(a->computedBounds.x != b->computedBounds.x);
    // Roughly equal widths — both columns carry flex = 1.
    CHECK(std::abs(a->computedBounds.w - b->computedBounds.w) < 2.0f);
    // Non-degenerate: a zero-width column means the flex pass did not run.
    CHECK(a->computedBounds.w > 100.0f);
    CHECK(b->computedBounds.w > 100.0f);
}

TEST_CASE("scroll lists clip their children") {
    // Regression guard for rows rendering outside the panel while scrolling.
    // The scissor machinery already existed but nothing set clipChildren.
    SetupFixture f;
    bool sawScrollList = false;
    for (const Widget& w : f.ui.widgets()) {
        if (w.id == aoc::ui::INVALID_WIDGET) {
            continue;
        }
        if (std::holds_alternative<ScrollListData>(w.data)) {
            sawScrollList = true;
            CHECK(w.clipChildren == true);
        }
    }
    CHECK(sawScrollList);
}

TEST_CASE("pan clamp permits travel only on an overflowing axis") {
    // This arithmetic is what silently disabled tech-tree horizontal scrolling:
    // when the canvas was sized to exactly fit the graph, panX clamped to 0.
    UIManager ui;
    ui.setScreenSize(VIEWPORT_W, VIEWPORT_H);
    const WidgetId canvas = ui.createPanel({0.0f, 0.0f, 800.0f, 600.0f}, PanelData{});
    Widget* c             = ui.getWidget(canvas);
    REQUIRE(c != nullptr);
    c->layoutDirection = LayoutDirection::None;
    c->canPan          = true;
    // Wider than the viewport, exactly as tall.
    c->panContentW = 2000.0f;
    c->panContentH = 600.0f;
    ui.layout();

    // Scrolling right (negative pan) is allowed up to viewport - content.
    ui.handleInput(400.0f, 300.0f, false, false, -5.0f, false, false, /*shiftHeld=*/true);
    CHECK(ui.getWidget(canvas)->panX < 0.0f);
    CHECK(ui.getWidget(canvas)->panX >= 800.0f - 2000.0f);

    // The non-overflowing axis must stay pinned at 0.
    ui.handleInput(400.0f, 300.0f, false, false, -5.0f, false, false, /*shiftHeld=*/false);
    CHECK(ui.getWidget(canvas)->panY == doctest::Approx(0.0f));
}
