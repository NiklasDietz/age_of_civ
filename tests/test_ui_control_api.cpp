/**
 * @file test_ui_control_api.cpp
 * @brief Coverage for the remote-UI-control primitives: the extended
 *        `dumpTreeJson()` fields, `UIManager::clickWidget()` dispatch, and
 *        a regression guard for the use-after-free that `handleInput`'s
 *        `ButtonData` release branch used to have (reading `btn`/the
 *        widget pointer again after `onClick()` could run after the
 *        callback reallocated `m_widgets`).
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Widget.hpp"

using aoc::ui::ButtonData;
using aoc::ui::IconData;
using aoc::ui::INVALID_WIDGET;
using aoc::ui::LabelData;
using aoc::ui::ListRowData;
using aoc::ui::PanelData;
using aoc::ui::Rect;
using aoc::ui::UIManager;
using aoc::ui::Widget;
using aoc::ui::WidgetId;

TEST_CASE("dumpTreeJson: button includes text, disabled, and kind") {
    UIManager ui;
    const WidgetId root = ui.createPanel({0.0f, 0.0f, 200.0f, 200.0f}, PanelData{});
    ButtonData btn;
    btn.label                          = "Start Game";
    btn.disabled                       = false;
    [[maybe_unused]] const WidgetId id = ui.createButton(root, {0.0f, 0.0f, 100.0f, 30.0f}, btn);
    ui.layout();

    const std::string json = ui.dumpTreeJson();
    CHECK(json.find("\"text\":\"Start Game\"") != std::string::npos);
    CHECK(json.find("\"kind\":\"button\"") != std::string::npos);
    CHECK(json.find("\"disabled\":false") != std::string::npos);
}

TEST_CASE("dumpTreeJson: disabled button reports disabled true") {
    UIManager ui;
    const WidgetId root = ui.createPanel({0.0f, 0.0f, 200.0f, 200.0f}, PanelData{});
    ButtonData btn;
    btn.label                          = "Locked";
    btn.disabled                       = true;
    [[maybe_unused]] const WidgetId id = ui.createButton(root, {0.0f, 0.0f, 100.0f, 30.0f}, btn);
    ui.layout();

    CHECK(ui.dumpTreeJson().find("\"disabled\":true") != std::string::npos);
}

TEST_CASE("dumpTreeJson: list row is tagged listrow, not panel") {
    UIManager ui;
    const WidgetId root = ui.createPanel({0.0f, 0.0f, 200.0f, 200.0f}, PanelData{});
    ListRowData row;
    row.title                          = "Warrior";
    [[maybe_unused]] const WidgetId id = ui.createListRow(root, {0.0f, 0.0f, 100.0f, 30.0f}, row);
    ui.layout();

    const std::string json = ui.dumpTreeJson();
    CHECK(json.find("\"kind\":\"listrow\"") != std::string::npos);
    CHECK(json.find("\"text\":\"Warrior\"") != std::string::npos);
}

TEST_CASE("dumpTreeJson: label text is extracted") {
    UIManager ui;
    const WidgetId root = ui.createPanel({0.0f, 0.0f, 200.0f, 200.0f}, PanelData{});
    LabelData label;
    label.text                         = "Turn 12";
    [[maybe_unused]] const WidgetId id = ui.createLabel(root, {0.0f, 0.0f, 100.0f, 30.0f}, label);
    ui.layout();

    CHECK(ui.dumpTreeJson().find("\"text\":\"Turn 12\"") != std::string::npos);
}

TEST_CASE("clickWidget: fires onClick for a button") {
    UIManager ui;
    const WidgetId root = ui.createPanel({0.0f, 0.0f, 200.0f, 200.0f}, PanelData{});
    bool fired          = false;
    ButtonData btn;
    btn.label         = "Click Me";
    btn.onClick       = [&fired]() { fired = true; };
    const WidgetId id = ui.createButton(root, {0.0f, 0.0f, 100.0f, 30.0f}, btn);
    ui.layout();

    CHECK(ui.clickWidget(id) == true);
    CHECK(fired == true);
}

TEST_CASE("clickWidget: respects disabled") {
    UIManager ui;
    const WidgetId root = ui.createPanel({0.0f, 0.0f, 200.0f, 200.0f}, PanelData{});
    bool fired          = false;
    ButtonData btn;
    btn.label         = "Disabled";
    btn.disabled      = true;
    btn.onClick       = [&fired]() { fired = true; };
    const WidgetId id = ui.createButton(root, {0.0f, 0.0f, 100.0f, 30.0f}, btn);
    ui.layout();

    CHECK(ui.clickWidget(id) == false);
    CHECK(fired == false);
}

TEST_CASE("clickWidget: fires onClick for an icon and a list row") {
    UIManager ui;
    const WidgetId root = ui.createPanel({0.0f, 0.0f, 200.0f, 200.0f}, PanelData{});

    bool iconFired = false;
    IconData icon;
    icon.onClick          = [&iconFired]() { iconFired = true; };
    const WidgetId iconId = ui.createIcon(root, {0.0f, 0.0f, 32.0f, 32.0f}, icon);

    bool rowFired = false;
    ListRowData row;
    row.title            = "Archer";
    row.onClick          = [&rowFired]() { rowFired = true; };
    const WidgetId rowId = ui.createListRow(root, {0.0f, 40.0f, 100.0f, 30.0f}, row);

    ui.layout();

    CHECK(ui.clickWidget(iconId) == true);
    CHECK(iconFired == true);
    CHECK(ui.clickWidget(rowId) == true);
    CHECK(rowFired == true);
}

TEST_CASE("clickWidget: no-ops for a non-existent id and a non-interactive panel") {
    UIManager ui;
    const WidgetId root = ui.createPanel({0.0f, 0.0f, 200.0f, 200.0f}, PanelData{});
    ui.layout();

    CHECK(ui.clickWidget(static_cast<WidgetId>(99999)) == false);
    CHECK(ui.clickWidget(root) == false);
}

TEST_CASE("regression: onClick that reallocates m_widgets does not dangle "
          "(UAF fix for handleInput's ButtonData release branch)") {
    // Force a UIManager::m_widgets reallocation from inside onClick --
    // exactly what "Start Game" does (destroy Main Menu's ~11 widgets,
    // build Game Setup's ~58). Before the fix, handleInput read
    // btn->disabled/btn->clickSound/btn->onDoubleClick AFTER calling
    // btn->onClick(), so a reallocation here left those reads dangling.
    UIManager ui;
    const WidgetId root = ui.createPanel({0.0f, 0.0f, 800.0f, 600.0f}, PanelData{});

    ButtonData btn;
    btn.label      = "Start Game";
    btn.clickSound = 1;
    btn.onClick    = [&ui, root]() {
        for (int32_t i = 0; i < 2000; ++i) {
            [[maybe_unused]] const WidgetId spam =
                ui.createPanel(root, {0.0f, 0.0f, 1.0f, 1.0f}, PanelData{});
        }
    };
    const WidgetId id = ui.createButton(root, {10.0f, 10.0f, 100.0f, 30.0f}, btn);
    ui.layout();
    // computedBounds is only populated by layout() -- must read it after,
    // not before, or the click below lands at a stale (0,0)-ish position
    // that misses the button entirely and never exercises onClick.
    const Widget* buttonW = ui.getWidget(id);
    REQUIRE(buttonW != nullptr);
    REQUIRE(buttonW->computedBounds.w > 0.0f);
    const float cx = buttonW->computedBounds.x + 1.0f;
    const float cy = buttonW->computedBounds.y + 1.0f;

    // Real press-then-release, exactly what a mouse click (or the
    // remote-control /ui/click-at route) does.
    ui.handleInput(cx, cy, /*mousePressed=*/true, /*mouseReleased=*/false);
    ui.handleInput(cx, cy, /*mousePressed=*/false, /*mouseReleased=*/true);
    // No crash / no ASan failure reaching here is the actual assertion.
    CHECK(true);
}

TEST_CASE("activateFocused: respects disabled") {
    UIManager ui;
    const WidgetId root = ui.createPanel({0.0f, 0.0f, 200.0f, 200.0f}, PanelData{});
    bool fired          = false;
    ButtonData btn;
    btn.label         = "Disabled";
    btn.disabled      = true;
    btn.onClick       = [&fired]() { fired = true; };
    const WidgetId id = ui.createButton(root, {0.0f, 0.0f, 100.0f, 30.0f}, btn);
    ui.layout();
    // focusNext() only considers widgets with `focusable == true`, which
    // createButton() does not set by default.
    ui.getWidget(id)->focusable = true;

    REQUIRE(ui.focusNext() == id);
    ui.activateFocused();
    CHECK(fired == false);
}

TEST_CASE("activateShortcut: respects disabled and fires enabled matches") {
    UIManager ui;
    const WidgetId root = ui.createPanel({0.0f, 0.0f, 200.0f, 200.0f}, PanelData{});

    bool disabledFired = false;
    ButtonData disabledBtn;
    disabledBtn.label    = "Disabled";
    disabledBtn.shortcut = 'Q';
    disabledBtn.disabled = true;
    disabledBtn.onClick  = [&disabledFired]() { disabledFired = true; };
    [[maybe_unused]] const WidgetId disabledId =
        ui.createButton(root, {0.0f, 0.0f, 100.0f, 30.0f}, disabledBtn);

    bool enabledFired = false;
    ButtonData enabledBtn;
    enabledBtn.label    = "Enabled";
    enabledBtn.shortcut = 'Q';
    enabledBtn.onClick  = [&enabledFired]() { enabledFired = true; };
    [[maybe_unused]] const WidgetId enabledId =
        ui.createButton(root, {0.0f, 40.0f, 100.0f, 30.0f}, enabledBtn);

    ui.layout();
    CHECK(ui.activateShortcut('Q') == true);
    CHECK(disabledFired == false);
    CHECK(enabledFired == true);
}

TEST_CASE("regression: activateShortcut survives an onClick that reallocates m_widgets") {
    // Same class of bug as handleInput's ButtonData branch, but this
    // function also iterates `m_widgets` directly with a live Widget&
    // -- a reallocation during the loop would invalidate the loop's own
    // iterators, not just a captured pointer.
    UIManager ui;
    const WidgetId root = ui.createPanel({0.0f, 0.0f, 800.0f, 600.0f}, PanelData{});
    ButtonData btn;
    btn.label    = "Spawn";
    btn.shortcut = 'S';
    btn.onClick  = [&ui, root]() {
        for (int32_t i = 0; i < 2000; ++i) {
            [[maybe_unused]] const WidgetId spam =
                ui.createPanel(root, {0.0f, 0.0f, 1.0f, 1.0f}, PanelData{});
        }
    };
    [[maybe_unused]] const WidgetId id = ui.createButton(root, {0.0f, 0.0f, 100.0f, 30.0f}, btn);
    ui.layout();

    CHECK(ui.activateShortcut('S') == true);
    // No crash / no ASan failure reaching here is the actual assertion.
    CHECK(true);
}
