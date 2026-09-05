/**
 * @file test_encyclopedia_screen.cpp
 * @brief The Civilopedia builds from the static content tables and had no way in
 *        until it was bound to F2 and the HUD menu (2026-09-04). These cases pin
 *        that it opens standalone (no GameState), lists entries for the categories
 *        it advertises, filters by search, and tears down cleanly.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/ui/Encyclopedia.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Widget.hpp"

#include <string>
#include <variant>

using aoc::ui::EncyclopediaScreen;
using aoc::ui::UIManager;
using aoc::ui::Widget;
using aoc::ui::WikiCategory;

namespace {

/// Live widgets in the manager, skipping free-list slots.
[[nodiscard]] int32_t liveWidgets(const UIManager& ui) {
    int32_t n = 0;
    for (const Widget& w : ui.widgets()) {
        if (w.id != aoc::ui::INVALID_WIDGET) { ++n; }
    }
    return n;
}

} // namespace

TEST_CASE("every advertised category has a name") {
    for (uint8_t i = 0; i < static_cast<uint8_t>(WikiCategory::Count); ++i) {
        const char* name = aoc::ui::wikiCategoryName(static_cast<WikiCategory>(i));
        REQUIRE(name != nullptr);
        CHECK(std::string(name).length() > 0);
    }
}

TEST_CASE("the screen opens with no GameState and closes cleanly") {
    UIManager ui;
    ui.setScreenSize(1920.0f, 1200.0f);
    EncyclopediaScreen screen;
    screen.setScreenSize(1920.0f, 1200.0f);
    const int32_t before = liveWidgets(ui);

    CHECK_FALSE(screen.isOpen());
    screen.open(ui);
    CHECK(screen.isOpen());
    CHECK(liveWidgets(ui) > before + 10);

    screen.close(ui);
    CHECK_FALSE(screen.isOpen());
    CHECK(liveWidgets(ui) == before);
}

TEST_CASE("switching category and searching rebuild the entry list") {
    UIManager ui;
    ui.setScreenSize(1920.0f, 1200.0f);
    EncyclopediaScreen screen;
    screen.setScreenSize(1920.0f, 1200.0f);
    screen.open(ui);

    screen.setCategory(WikiCategory::Units);
    screen.refresh(ui);
    const int32_t unitWidgets = liveWidgets(ui);
    CHECK(unitWidgets > 0);

    // A query that matches nothing must not leave the previous category's rows behind.
    screen.search("zzzznotacontententry");
    screen.refresh(ui);
    CHECK(liveWidgets(ui) < unitWidgets);

    screen.search("");
    screen.refresh(ui);
    CHECK(liveWidgets(ui) == unitWidgets);
    screen.close(ui);
}

namespace {

/// Click the first button labelled exactly `label`; false when none.
bool clickEntry(UIManager& ui, const std::string& label) {
    for (const Widget& w : ui.widgets()) {
        if (w.id == aoc::ui::INVALID_WIDGET) { continue; }
        const aoc::ui::ButtonData* btn = std::get_if<aoc::ui::ButtonData>(&w.data);
        if (btn != nullptr && btn->label == label) {
            const aoc::ui::WidgetId id = w.id;
            return ui.clickWidget(id);
        }
    }
    return false;
}

[[nodiscard]] bool anyLabelContains(const UIManager& ui, const std::string& needle) {
    for (const Widget& w : ui.widgets()) {
        if (w.id == aoc::ui::INVALID_WIDGET) { continue; }
        const aoc::ui::LabelData* label = std::get_if<aoc::ui::LabelData>(&w.data);
        if (label != nullptr && label->text.find(needle) != std::string::npos) { return true; }
    }
    return false;
}

} // namespace

TEST_CASE("building entries state their amenities and their civic gate") {
    UIManager ui;
    ui.setScreenSize(1920.0f, 1200.0f);
    EncyclopediaScreen screen;
    screen.setScreenSize(1920.0f, 1200.0f);
    screen.open(ui);
    screen.setCategory(WikiCategory::Buildings);

    screen.search("Entertainment Complex");
    screen.refresh(ui);
    REQUIRE(clickEntry(ui, "Entertainment Complex"));
    screen.refresh(ui);
    CHECK(anyLabelContains(ui, "+Amenities:2"));
    CHECK(anyLabelContains(ui, "Requires Civic: Games and Recreation"));

    screen.search("Water Park");
    screen.refresh(ui);
    REQUIRE(clickEntry(ui, "Water Park"));
    screen.refresh(ui);
    CHECK(anyLabelContains(ui, "District: Harbor"));
    CHECK(anyLabelContains(ui, "Requires Civic: Urbanization"));

    screen.search("Hospital");
    screen.refresh(ui);
    REQUIRE(clickEntry(ui, "Hospital"));
    screen.refresh(ui);
    CHECK(anyLabelContains(ui, "+Amenities:1"));
    CHECK_FALSE(anyLabelContains(ui, "Requires Civic"));
    screen.close(ui);
}
