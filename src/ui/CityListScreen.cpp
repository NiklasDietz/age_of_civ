/**
 * @file CityListScreen.cpp
 * @brief City list with jump-to and open (see CityListScreen.hpp).
 */

#include "aoc/ui/CityListScreen.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/CityActions.hpp"
#include "aoc/simulation/city/CityGrowth.hpp"
#include "aoc/simulation/city/ProductionSystem.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/ui/UIManager.hpp"

#include <cstdio>
#include <string>
#include <utility>

namespace aoc::ui {

namespace {

constexpr float PANEL_W = 760.0f;
constexpr float PANEL_H = 560.0f;
constexpr float LIST_W  = PANEL_W - 30.0f;
constexpr float LIST_H  = PANEL_H - 130.0f;
constexpr float ROW_W   = LIST_W - 10.0f;
constexpr float ROW_H   = 22.0f;

void mixHash(uint64_t& hash, uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
}

} // namespace

void CityListScreen::setContext(aoc::game::GameState* gameState, const aoc::map::HexGrid* grid,
                                PlayerId humanPlayer) {
    this->m_gameState = gameState;
    this->m_grid      = grid;
    this->m_player    = humanPlayer;
    this->setBaseContext(gameState, grid, humanPlayer);
}

void CityListScreen::setCallbacks(LocationCallback onJump, LocationCallback onOpen) {
    this->m_onJump = std::move(onJump);
    this->m_onOpen = std::move(onOpen);
}

void CityListScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }
    this->m_isOpen = true;

    const WidgetId innerPanel =
        this->createScreenFrame(ui, "Cities", PANEL_W, PANEL_H, this->m_screenW, this->m_screenH);
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

void CityListScreen::close(UIManager& ui) {
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

void CityListScreen::refresh(UIManager& ui) {
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

void CityListScreen::buildRows(UIManager& ui) {
    const aoc::game::Player* owner = this->m_gameState->player(this->m_player);
    if (owner == nullptr) {
        ui.setLabelText(this->m_summaryLabel, "No player");
        return;
    }
    int32_t population = 0;
    for (const std::unique_ptr<aoc::game::City>& city : owner->cities()) {
        if (city != nullptr && city->owner() == this->m_player) { population += city->population(); }
    }
    ui.setLabelText(this->m_summaryLabel, std::to_string(owner->ownedCityCount()) + " cities, "
                                              + std::to_string(population) + " citizens");

    for (const std::unique_ptr<aoc::game::City>& cityPtr : owner->cities()) {
        if (cityPtr == nullptr || cityPtr->owner() != this->m_player) {
            continue;
        }
        const aoc::game::City& city = *cityPtr;
        const float perTurn = (this->m_grid != nullptr)
            ? aoc::sim::cityProductionPerTurn(*owner, city, *this->m_grid, *this->m_gameState)
            : 0.0f;
        const int32_t housing = (this->m_grid != nullptr)
            ? aoc::sim::computeCityHousing(city, *this->m_grid) : 0;

        char buf[192];
        std::snprintf(buf, sizeof(buf), "%s  pop %d/%d  |  %s  |  loyalty %.0f  |  %.*s",
                      city.name().c_str(), city.population(), housing,
                      aoc::sim::cityProductionSummary(city, perTurn).c_str(),
                      static_cast<double>(city.loyalty().loyalty),
                      static_cast<int>(aoc::sim::amenityTierName(city.happiness().happiness).size()),
                      aoc::sim::amenityTierName(city.happiness().happiness).data());

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

        const aoc::hex::AxialCoord loc = city.location();
        const LocationCallback jump    = this->m_onJump;
        const LocationCallback openCb  = this->m_onOpen;
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

        ButtonData openBtn;
        openBtn.label        = "Open";
        openBtn.fontSize     = 10.0f;
        openBtn.cornerRadius = tokens::CORNER_BUTTON;
        openBtn.normalColor  = tokens::BRONZE_BASE;
        openBtn.hoverColor   = tokens::BRONZE_LIGHT;
        openBtn.pressedColor = tokens::STATE_PRESSED;
        openBtn.labelColor   = tokens::TEXT_GILT;
        openBtn.onClick      = [openCb, loc]() { if (openCb) { openCb(loc); } };
        (void)ui.createButton(row, {0.0f, 0.0f, 70.0f, 18.0f}, std::move(openBtn));
    }
    if (owner->ownedCityCount() == 0) {
        (void)ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, 16.0f},
                             LabelData{"No cities yet: found one with a Settler", tokens::TEXT_DISABLED, 11.0f});
    }
}

uint64_t CityListScreen::stateFingerprint() const {
    if (this->m_gameState == nullptr) {
        return 0;
    }
    const aoc::game::Player* owner = this->m_gameState->player(this->m_player);
    if (owner == nullptr) {
        return 0;
    }
    uint64_t h = 1469598103934665603ULL;
    mixHash(h, static_cast<uint64_t>(this->m_gameState->currentTurn()) + 1u);
    for (const std::unique_ptr<aoc::game::City>& city : owner->cities()) {
        if (city == nullptr) { continue; }
        mixHash(h, static_cast<uint64_t>(city->population()) + 3u);
        mixHash(h, static_cast<uint64_t>(city->production().queue.size()) + 5u);
        mixHash(h, static_cast<uint64_t>(city->loyalty().loyalty * 10.0f) + 7u);
        mixHash(h, static_cast<uint64_t>(static_cast<uint8_t>(city->owner())) + 11u);
    }
    return h;
}

} // namespace aoc::ui
