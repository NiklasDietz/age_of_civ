/**
 * @file UnitListScreen.cpp
 * @brief Unit list with jump-to and select (see UnitListScreen.hpp).
 */

#include "aoc/ui/UnitListScreen.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/ui/UIManager.hpp"

#include <cstdio>
#include <string>
#include <utility>

namespace aoc::ui {

namespace {

constexpr float PANEL_W = 720.0f;
constexpr float PANEL_H = 560.0f;
constexpr float LIST_W  = PANEL_W - 30.0f;
constexpr float LIST_H  = PANEL_H - 130.0f;
constexpr float ROW_W   = LIST_W - 10.0f;
constexpr float ROW_H   = 22.0f;

void mixHash(uint64_t& hash, uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
}

const char* stateName(aoc::sim::UnitState state) {
    switch (state) {
        case aoc::sim::UnitState::Fortified: return "fortified";
        case aoc::sim::UnitState::Embarked:  return "embarked";
        case aoc::sim::UnitState::Sleeping:  return "sleeping";
        default:                             return "idle";
    }
}

} // namespace

void UnitListScreen::setContext(aoc::game::GameState* gameState, const aoc::map::HexGrid* grid,
                                PlayerId humanPlayer) {
    this->m_gameState = gameState;
    this->m_grid      = grid;
    this->m_player    = humanPlayer;
    this->setBaseContext(gameState, grid, humanPlayer);
}

void UnitListScreen::setCallbacks(LocationCallback onJump, LocationCallback onSelect) {
    this->m_onJump   = std::move(onJump);
    this->m_onSelect = std::move(onSelect);
}

void UnitListScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }
    this->m_isOpen = true;

    const WidgetId innerPanel =
        this->createScreenFrame(ui, "Units", PANEL_W, PANEL_H, this->m_screenW, this->m_screenH);
    LabelData summary;
    summary.color    = tokens::TEXT_INK;
    summary.fontSize = 13.0f;
    this->m_summaryLabel =
        ui.createLabel(innerPanel, {0.0f, 0.0f, LIST_W, 20.0f}, std::move(summary));

    this->m_list = ui.createScrollList(innerPanel, {0.0f, 0.0f, LIST_W, LIST_H});
    if (Widget* list = ui.getWidget(this->m_list); list != nullptr) {
        list->padding      = {4.0f, 4.0f, 4.0f, 4.0f};
        list->childSpacing = 3.0f;
    }
    this->buildRows(ui);
    this->m_shownFingerprint = this->stateFingerprint();
}

void UnitListScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_summaryLabel = INVALID_WIDGET;
    this->m_list         = INVALID_WIDGET;
}

void UnitListScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }
    const uint64_t now = this->stateFingerprint();
    if (now == this->m_shownFingerprint) {
        return;
    }
    this->close(ui);
    this->open(ui);
}

void UnitListScreen::buildRows(UIManager& ui) {
    const aoc::game::Player* owner = this->m_gameState->player(this->m_player);
    if (owner == nullptr) {
        ui.setLabelText(this->m_summaryLabel, "No player");
        return;
    }
    int32_t military = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unit : owner->units()) {
        if (unit != nullptr && unit->isMilitary()) { ++military; }
    }
    ui.setLabelText(this->m_summaryLabel, std::to_string(owner->units().size()) + " units, "
                                              + std::to_string(military) + " military");

    for (const std::unique_ptr<aoc::game::Unit>& unitPtr : owner->units()) {
        if (unitPtr == nullptr) {
            continue;
        }
        const aoc::game::Unit& unit = *unitPtr;
        const aoc::sim::UnitTypeDef& def = unit.typeDef();
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%.*s  at (%d,%d)  |  hp %d/%d  |  moves %d/%d  |  %s",
                      static_cast<int>(def.name.size()), def.name.data(), unit.position().q,
                      unit.position().r, unit.hitPoints(), def.maxHitPoints,
                      unit.movementRemaining(), def.movementPoints, stateName(unit.state()));

        PanelData rowBg;
        rowBg.backgroundColor = tokens::SURFACE_PARCHMENT_DIM;
        rowBg.cornerRadius    = tokens::CORNER_BUTTON;
        const WidgetId row = ui.createPanel(this->m_list, {0.0f, 0.0f, ROW_W, ROW_H}, std::move(rowBg));
        if (Widget* rw = ui.getWidget(row); rw != nullptr) {
            rw->layoutDirection = LayoutDirection::Horizontal;
            rw->childSpacing    = 6.0f;
            rw->padding         = {2.0f, 4.0f, 2.0f, 4.0f};
        }
        (void)ui.createLabel(row, {0.0f, 0.0f, ROW_W - 150.0f, 18.0f},
                             LabelData{std::string(buf), tokens::TEXT_INK, 11.0f});

        const aoc::hex::AxialCoord loc = unit.position();
        const LocationCallback jump    = this->m_onJump;
        const LocationCallback select  = this->m_onSelect;
        ButtonData go;
        go.label        = "Go";
        go.fontSize     = 10.0f;
        go.cornerRadius = tokens::CORNER_BUTTON;
        go.normalColor  = tokens::BRONZE_BASE;
        go.hoverColor   = tokens::BRONZE_LIGHT;
        go.pressedColor = tokens::STATE_PRESSED;
        go.labelColor   = tokens::TEXT_GILT;
        go.onClick      = [jump, loc]() { if (jump) { jump(loc); } };
        (void)ui.createButton(row, {0.0f, 0.0f, 56.0f, 18.0f}, std::move(go));

        ButtonData sel;
        sel.label        = "Select";
        sel.fontSize     = 10.0f;
        sel.cornerRadius = tokens::CORNER_BUTTON;
        sel.normalColor  = tokens::BRONZE_BASE;
        sel.hoverColor   = tokens::BRONZE_LIGHT;
        sel.pressedColor = tokens::STATE_PRESSED;
        sel.labelColor   = tokens::TEXT_GILT;
        sel.onClick      = [select, loc]() { if (select) { select(loc); } };
        (void)ui.createButton(row, {0.0f, 0.0f, 70.0f, 18.0f}, std::move(sel));
    }
    if (owner->units().empty()) {
        (void)ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, 16.0f},
                             LabelData{"No units", tokens::TEXT_DISABLED, 11.0f});
    }
}

uint64_t UnitListScreen::stateFingerprint() const {
    if (this->m_gameState == nullptr) {
        return 0;
    }
    const aoc::game::Player* owner = this->m_gameState->player(this->m_player);
    if (owner == nullptr) {
        return 0;
    }
    uint64_t h = 1469598103934665603ULL;
    mixHash(h, static_cast<uint64_t>(owner->units().size()) + 1u);
    for (const std::unique_ptr<aoc::game::Unit>& unit : owner->units()) {
        if (unit == nullptr) { continue; }
        mixHash(h, static_cast<uint64_t>(unit->position().q + 4096) * 8191u
                       + static_cast<uint64_t>(unit->position().r + 4096));
        mixHash(h, static_cast<uint64_t>(unit->hitPoints()) + 3u);
        mixHash(h, static_cast<uint64_t>(unit->movementRemaining()) + 5u);
        mixHash(h, static_cast<uint64_t>(unit->state()) + 7u);
    }
    return h;
}

} // namespace aoc::ui
