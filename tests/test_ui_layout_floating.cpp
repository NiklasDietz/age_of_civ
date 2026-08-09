/**
 * @file test_ui_layout_floating.cpp
 * @brief Layout semantics for `Widget::floating` inside flow containers.
 *
 * Guards two properties that a modal's pinned Close button depends on:
 *   1. A floating child keeps its authored bounds instead of being appended
 *      to the parent's vertical stack (where it landed past the panel edge).
 *   2. Flowing siblings are unaffected — a floating child consumes no cursor
 *      advance, no spacing, and no flex share.
 *
 * Property 2 is the important one: `floating` was first prototyped as
 * `anchor == Anchor::None`, which is the DEFAULT for every widget, so every
 * child in every flow container collapsed onto the same point.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Widget.hpp"

using aoc::ui::LayoutDirection;
using aoc::ui::PanelData;
using aoc::ui::UIManager;
using aoc::ui::Widget;
using aoc::ui::WidgetId;

namespace {

/// Vertical container at a known origin with no padding and no spacing, so
/// expected child positions are plain sums.
struct Fixture {
    UIManager ui;
    WidgetId parent = 0;

    Fixture() {
        this->ui.setScreenSize(800.0f, 600.0f);
        this->parent = this->ui.createPanel({100.0f, 50.0f, 400.0f, 300.0f}, PanelData{});
        Widget* p    = this->ui.getWidget(this->parent);
        REQUIRE(p != nullptr);
        p->layoutDirection = LayoutDirection::Vertical;
        p->padding         = {0.0f, 0.0f, 0.0f, 0.0f};
        p->childSpacing    = 0.0f;
    }

    WidgetId addChild(float x, float y, float w, float h) {
        return this->ui.createPanel(this->parent, {x, y, w, h}, PanelData{});
    }
};

} // namespace

TEST_CASE("flowing children stack vertically from the parent content origin") {
    Fixture f;
    const WidgetId a = f.addChild(0.0f, 0.0f, 200.0f, 30.0f);
    const WidgetId b = f.addChild(0.0f, 0.0f, 200.0f, 40.0f);
    f.ui.layout();

    // Baseline: no floating children, so this is ordinary stacking.
    CHECK(f.ui.getWidget(a)->computedBounds.y == doctest::Approx(50.0f));
    CHECK(f.ui.getWidget(b)->computedBounds.y == doctest::Approx(80.0f));
}

TEST_CASE("floating child keeps its authored offset instead of being stacked") {
    Fixture f;
    const WidgetId flowing = f.addChild(0.0f, 0.0f, 200.0f, 30.0f);
    // Pinned near the parent's bottom-right, the way createScreenFrame pins
    // its Close button: {width - 124, height - 50}.
    const WidgetId pinned = f.addChild(276.0f, 250.0f, 100.0f, 28.0f);
    Widget* pw            = f.ui.getWidget(pinned);
    REQUIRE(pw != nullptr);
    pw->floating = true;

    f.ui.layout();

    // Authored offset preserved, relative to the parent's content origin.
    CHECK(f.ui.getWidget(pinned)->computedBounds.x == doctest::Approx(376.0f));
    CHECK(f.ui.getWidget(pinned)->computedBounds.y == doctest::Approx(300.0f));
    // And it stays inside the parent rather than past its bottom edge.
    const Widget* parent = f.ui.getWidget(f.parent);
    CHECK(f.ui.getWidget(pinned)->computedBounds.y + 28.0f <=
          parent->computedBounds.y + parent->computedBounds.h);

    // The flowing sibling is untouched by the floating one.
    CHECK(f.ui.getWidget(flowing)->computedBounds.y == doctest::Approx(50.0f));
}

TEST_CASE("floating child consumes no space in the flow") {
    Fixture f;
    // Floating child declared FIRST: if it advanced the cursor, the two
    // flowing siblings below would both be pushed down.
    const WidgetId pinned = f.addChild(10.0f, 200.0f, 100.0f, 28.0f);
    Widget* pw            = f.ui.getWidget(pinned);
    REQUIRE(pw != nullptr);
    pw->floating = true;

    const WidgetId a = f.addChild(0.0f, 0.0f, 200.0f, 30.0f);
    const WidgetId b = f.addChild(0.0f, 0.0f, 200.0f, 40.0f);
    f.ui.layout();

    // Identical to the no-floating baseline above.
    CHECK(f.ui.getWidget(a)->computedBounds.y == doctest::Approx(50.0f));
    CHECK(f.ui.getWidget(b)->computedBounds.y == doctest::Approx(80.0f));
}

TEST_CASE("default-constructed children are NOT floating") {
    // Regression guard: `floating` must not be defaulted on, and must not be
    // aliased to `anchor == Anchor::None` (the default anchor). If it were,
    // both children would collapse onto the parent's content origin.
    Fixture f;
    const WidgetId a = f.addChild(0.0f, 0.0f, 200.0f, 30.0f);
    const WidgetId b = f.addChild(0.0f, 0.0f, 200.0f, 30.0f);
    f.ui.layout();

    CHECK(f.ui.getWidget(a)->floating == false);
    CHECK(f.ui.getWidget(a)->computedBounds.y != f.ui.getWidget(b)->computedBounds.y);
}
