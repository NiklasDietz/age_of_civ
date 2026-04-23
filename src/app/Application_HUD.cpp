/**
 * @file Application_HUD.cpp
 * @brief HUD build/update/rebuild methods split out of Application.cpp.
 *
 * These functions still belong to `aoc::app::Application` — C++ lets a
 * class's methods live in multiple translation units as long as the
 * class is declared in a common header. Splitting here pulled ~950
 * lines out of the 3800-line Application.cpp without semantic change.
 *
 * `turnToYear` used to live in an anonymous namespace at the top of
 * Application.cpp; it now lives in `ApplicationHelpers.hpp` so both
 * translation units can reuse it.
 */

#include "aoc/app/Application.hpp"
#include "ApplicationHelpers.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/UnitUpgrade.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/ProductionSystem.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/CityBombardment.hpp"
#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/city/BorderExpansion.hpp"
#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/save/Serializer.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Theme.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

namespace aoc::app {

using aoc::app::detail::turnToYear;

// ============================================================================
// HUD
// ============================================================================

void Application::buildHUD() {
    const std::pair<uint32_t, uint32_t> hudFbSize = this->m_window.framebufferSize();
    float screenW = static_cast<float>(hudFbSize.first);

    // ================================================================
    // Top bar: full width. Resources on left, buttons on right.
    // ================================================================
    this->m_topBar = this->m_uiManager.createPanel(
        {0.0f, 0.0f, screenW, 32.0f},
        aoc::ui::PanelData{{0.06f, 0.06f, 0.10f, 0.90f}, 0.0f});
    {
        aoc::ui::Widget* bar = this->m_uiManager.getWidget(this->m_topBar);
        bar->layoutDirection = aoc::ui::LayoutDirection::Horizontal;
        bar->padding = {4.0f, 6.0f, 4.0f, 6.0f};
        bar->childSpacing = 6.0f;
        bar->anchor = aoc::ui::Anchor::TopLeft;
    }

    // Helper for top bar buttons
    // auto required: lambda type is unnameable
    auto makeTopBtn = [this](aoc::ui::WidgetId parent, const std::string& label,
                              float width, std::function<void()> onClick) {
        aoc::ui::ButtonData btn;
        btn.label = label;
        btn.fontSize = 11.0f;
        btn.normalColor  = {0.18f, 0.18f, 0.22f, 0.9f};
        btn.hoverColor   = {0.28f, 0.28f, 0.35f, 0.9f};
        btn.pressedColor = {0.12f, 0.12f, 0.16f, 0.9f};
        btn.labelColor   = {0.9f, 0.9f, 0.9f, 1.0f};
        btn.cornerRadius = 3.0f;
        btn.onClick = std::move(onClick);
        return this->m_uiManager.createButton(
            parent, {0.0f, 0.0f, width, 22.0f}, std::move(btn));
    };

    // LEFT SIDE: Resource display
    this->m_resourceLabel = this->m_uiManager.createLabel(
        this->m_topBar, {0.0f, 0.0f, 700.0f, 22.0f},
        aoc::ui::LabelData{"Resources: ...", {0.75f, 0.80f, 0.65f, 1.0f}, 10.0f});

    // Civ-6-style diplomacy strip. One icon per known civ; unmet
    // players render as neutral `?`, met players get their player
    // colour. Click → open DiplomacyScreen. Rebuilt on every frame
    // inside `updateHUD` so newly-met civs light up live.
    this->m_diploStrip = this->m_uiManager.createPanel(
        this->m_topBar, {0.0f, 0.0f, 0.0f, 22.0f},
        aoc::ui::PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
    {
        aoc::ui::Widget* s = this->m_uiManager.getWidget(this->m_diploStrip);
        if (s != nullptr) {
            s->layoutDirection = aoc::ui::LayoutDirection::Horizontal;
            s->childSpacing = 4.0f;
            s->requestedBounds.w = 400.0f;
        }
    }

    // Spacer to push menu buttons to the right
    [[maybe_unused]] aoc::ui::WidgetId spacer = this->m_uiManager.createPanel(
        this->m_topBar, {0.0f, 0.0f, 1.0f, 22.0f},
        aoc::ui::PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

    // RIGHT SIDE: Game screen buttons
    makeTopBtn(this->m_topBar, "Tech", 50.0f, [this]() {
        if (!this->m_techScreen.isOpen()) {
            this->m_techScreen.setContext(&this->m_gameState, 0);
            this->m_techScreen.open(this->m_uiManager);
        } else {
            this->m_techScreen.close(this->m_uiManager);
        }
    });

    makeTopBtn(this->m_topBar, "Gov", 44.0f, [this]() {
        if (!this->m_governmentScreen.isOpen()) {
            this->m_governmentScreen.setContext(&this->m_gameState, 0);
            this->m_governmentScreen.open(this->m_uiManager);
        } else {
            this->m_governmentScreen.close(this->m_uiManager);
        }
    });

    makeTopBtn(this->m_topBar, "Econ", 50.0f, [this]() {
        if (!this->m_economyScreen.isOpen()) {
            this->m_economyScreen.setContext(&this->m_gameState, &this->m_hexGrid, 0, &this->m_economy.market());
            this->m_economyScreen.open(this->m_uiManager);
        } else {
            this->m_economyScreen.close(this->m_uiManager);
        }
    });

    makeTopBtn(this->m_topBar, "Trade", 50.0f, [this]() {
        if (!this->m_tradeScreen.isOpen()) {
            this->m_tradeScreen.setContext(&this->m_gameState, 0,
                                            &this->m_economy.market(),
                                            &this->m_diplomacy);
            this->m_tradeScreen.open(this->m_uiManager);
        } else {
            this->m_tradeScreen.close(this->m_uiManager);
        }
    });

    makeTopBtn(this->m_topBar, "Routes", 60.0f, [this]() {
        if (!this->m_tradeRouteSetupScreen.isOpen()) {
            this->m_tradeRouteSetupScreen.setContext(&this->m_gameState, &this->m_hexGrid, 0,
                                                      &this->m_economy.market(),
                                                      &this->m_diplomacy);
            this->m_tradeRouteSetupScreen.open(this->m_uiManager);
        } else {
            this->m_tradeRouteSetupScreen.close(this->m_uiManager);
        }
    });

    makeTopBtn(this->m_topBar, "Diplo", 50.0f, [this]() {
        if (!this->m_diplomacyScreen.isOpen()) {
            this->m_diplomacyScreen.setContext(&this->m_gameState, 0, &this->m_diplomacy,
                                                &this->m_hexGrid, &this->m_dealTracker);
            this->m_diplomacyScreen.open(this->m_uiManager);
        } else {
            this->m_diplomacyScreen.close(this->m_uiManager);
        }
    });

    // Separator
    [[maybe_unused]] aoc::ui::WidgetId sep = this->m_uiManager.createPanel(
        this->m_topBar, {0.0f, 0.0f, 2.0f, 22.0f},
        aoc::ui::PanelData{{0.3f, 0.3f, 0.35f, 0.5f}, 0.0f});

    // MENU button -- toggles a dropdown with Save/Load/Settings
    makeTopBtn(this->m_topBar, "Menu", 55.0f, [this]() {
        if (this->m_menuDropdown != aoc::ui::INVALID_WIDGET) {
            // Close dropdown
            this->m_uiManager.removeWidget(this->m_menuDropdown);
            this->m_menuDropdown = aoc::ui::INVALID_WIDGET;
        } else {
            // Open dropdown at top-right
            const std::pair<uint32_t, uint32_t> dropFbSize = this->m_window.framebufferSize();
            float dropX = static_cast<float>(dropFbSize.first) - 120.0f;
            float dropY = 34.0f;

            this->m_menuDropdown = this->m_uiManager.createPanel(
                {dropX, dropY, 110.0f, 150.0f},
                aoc::ui::PanelData{{0.10f, 0.10f, 0.14f, 0.95f}, 4.0f});
            {
                aoc::ui::Widget* dp = this->m_uiManager.getWidget(this->m_menuDropdown);
                dp->padding = {6.0f, 6.0f, 6.0f, 6.0f};
                dp->childSpacing = 4.0f;
            }

            // auto required: lambda type is unnameable
            auto makeDropBtn = [this](aoc::ui::WidgetId parent, const std::string& label,
                                       std::function<void()> onClick) {
                aoc::ui::ButtonData btn;
                btn.label = label;
                btn.fontSize = 12.0f;
                btn.normalColor  = {0.15f, 0.15f, 0.20f, 0.9f};
                btn.hoverColor   = {0.25f, 0.25f, 0.32f, 0.9f};
                btn.pressedColor = {0.10f, 0.10f, 0.14f, 0.9f};
                btn.labelColor   = {0.9f, 0.9f, 0.9f, 1.0f};
                btn.cornerRadius = 3.0f;
                btn.onClick = std::move(onClick);
                [[maybe_unused]] aoc::ui::WidgetId id = this->m_uiManager.createButton(
                    parent, {0.0f, 0.0f, 98.0f, 28.0f}, std::move(btn));
            };

            makeDropBtn(this->m_menuDropdown, "Save Game", [this]() {
                ErrorCode result = aoc::save::saveGame(
                    "quicksave.aoc", this->m_gameState, this->m_hexGrid,
                    this->m_turnManager, this->m_economy, this->m_diplomacy,
                    this->m_fogOfWar, this->m_gameRng);
                if (result == ErrorCode::Ok) { LOG_INFO("Game saved"); }
                else { LOG_ERROR("Save failed"); }
                this->m_uiManager.removeWidget(this->m_menuDropdown);
                this->m_menuDropdown = aoc::ui::INVALID_WIDGET;
            });

            makeDropBtn(this->m_menuDropdown, "Load Game", [this]() {
                ErrorCode result = aoc::save::loadGame(
                    "quicksave.aoc", this->m_gameState, this->m_hexGrid,
                    this->m_turnManager, this->m_economy, this->m_diplomacy,
                    this->m_fogOfWar, this->m_gameRng);
                if (result == ErrorCode::Ok) {
                    LOG_INFO("Game loaded");
                    this->m_fogOfWar.updateVisibility(this->m_gameState, this->m_hexGrid, 0);
                } else { LOG_ERROR("Load failed"); }
                this->m_uiManager.removeWidget(this->m_menuDropdown);
                this->m_menuDropdown = aoc::ui::INVALID_WIDGET;
            });

            makeDropBtn(this->m_menuDropdown, "Settings", [this]() {
                this->m_uiManager.removeWidget(this->m_menuDropdown);
                this->m_menuDropdown = aoc::ui::INVALID_WIDGET;
                const std::pair<uint32_t, uint32_t> settingsFbSize = this->m_window.framebufferSize();
                if (!this->m_settingsMenu.isBuilt()) {
                    this->m_settingsMenu.build(
                        this->m_uiManager,
                        static_cast<float>(settingsFbSize.first), static_cast<float>(settingsFbSize.second),
                        [this]() {
                            aoc::ui::saveSettings(this->m_settingsMenu.settings(), "settings.cfg");
                            this->m_settingsMenu.destroy(this->m_uiManager);
                            this->applySettings();
                        });
                }
            });

            makeDropBtn(this->m_menuDropdown, "Main Menu", [this]() {
                this->m_uiManager.removeWidget(this->m_menuDropdown);
                this->m_menuDropdown = aoc::ui::INVALID_WIDGET;
                this->showReturnToMenuConfirm();
            });

            makeDropBtn(this->m_menuDropdown, "Quit", [this]() {
                glfwSetWindowShouldClose(this->m_window.handle(), GLFW_TRUE);
            });
        }
    });

    // ================================================================
    // Info panel (below top bar)
    // ================================================================
    aoc::ui::WidgetId infoPanel = this->m_uiManager.createPanel(
        {10.0f, 42.0f, 250.0f, 170.0f},
        aoc::ui::PanelData{{0.08f, 0.08f, 0.12f, 0.85f}, 6.0f});
    {
        aoc::ui::Widget* panel = this->m_uiManager.getWidget(infoPanel);
        panel->padding = {8.0f, 10.0f, 8.0f, 10.0f};
        panel->childSpacing = 5.0f;
        panel->anchor = aoc::ui::Anchor::TopLeft;
    }

    this->m_turnLabel = this->m_uiManager.createLabel(
        infoPanel, {0.0f, 0.0f, 230.0f, 14.0f},
        aoc::ui::LabelData{"Turn 0", {1.0f, 0.9f, 0.6f, 1.0f}, 14.0f});

    this->m_economyLabel = this->m_uiManager.createLabel(
        infoPanel, {0.0f, 0.0f, 230.0f, 12.0f},
        aoc::ui::LabelData{"Barter  Gold:100", {0.6f, 0.85f, 0.6f, 1.0f}, 11.0f});

    this->m_selectionLabel = this->m_uiManager.createLabel(
        infoPanel, {0.0f, 0.0f, 230.0f, 12.0f},
        aoc::ui::LabelData{"No selection", {0.8f, 0.8f, 0.8f, 1.0f}, 11.0f});

    // Research progress label + bar
    this->m_researchLabel = this->m_uiManager.createLabel(
        infoPanel, {0.0f, 0.0f, 230.0f, 12.0f},
        aoc::ui::LabelData{"No research", {0.7f, 0.85f, 1.0f, 1.0f}, 10.0f});

    constexpr float PROGRESS_BAR_W = 220.0f;
    constexpr float PROGRESS_BAR_H = 6.0f;

    this->m_researchBar = this->m_uiManager.createPanel(
        infoPanel, {0.0f, 0.0f, PROGRESS_BAR_W, PROGRESS_BAR_H},
        aoc::ui::PanelData{{0.15f, 0.15f, 0.20f, 0.8f}, 2.0f});
    this->m_researchBarFill = this->m_uiManager.createPanel(
        this->m_researchBar, {0.0f, 0.0f, 0.0f, PROGRESS_BAR_H},
        aoc::ui::PanelData{{0.2f, 0.7f, 0.3f, 0.9f}, 2.0f});

    // Production progress label + bar (visible when city selected)
    this->m_productionLabel = this->m_uiManager.createLabel(
        infoPanel, {0.0f, 0.0f, 230.0f, 12.0f},
        aoc::ui::LabelData{"", {0.9f, 0.75f, 0.4f, 1.0f}, 10.0f});

    this->m_productionBar = this->m_uiManager.createPanel(
        infoPanel, {0.0f, 0.0f, PROGRESS_BAR_W, PROGRESS_BAR_H},
        aoc::ui::PanelData{{0.15f, 0.15f, 0.20f, 0.8f}, 2.0f});
    this->m_productionBarFill = this->m_uiManager.createPanel(
        this->m_productionBar, {0.0f, 0.0f, 0.0f, PROGRESS_BAR_H},
        aoc::ui::PanelData{{0.85f, 0.6f, 0.15f, 0.9f}, 2.0f});

    // Hide production bar initially
    this->m_uiManager.setVisible(this->m_productionLabel, false);
    this->m_uiManager.setVisible(this->m_productionBar, false);

    // Bottom-right end turn button (anchored to bottom-right, repositions on resize)
    this->m_endTurnButton = this->m_uiManager.createPanel(
        {0.0f, 0.0f, 130.0f, 40.0f});
    {
        aoc::ui::Widget* endPanel = this->m_uiManager.getWidget(this->m_endTurnButton);
        if (endPanel != nullptr) {
            endPanel->anchor = aoc::ui::Anchor::BottomRight;
            endPanel->marginRight  = 20.0f;
            endPanel->marginBottom = 20.0f;
        }
    }

    aoc::ui::ButtonData endTurnBtn;
    endTurnBtn.label       = "End Turn";
    endTurnBtn.fontSize    = 15.0f;
    endTurnBtn.normalColor = {0.15f, 0.35f, 0.15f, 0.9f};
    endTurnBtn.hoverColor  = {0.20f, 0.50f, 0.20f, 0.9f};
    endTurnBtn.pressedColor = {0.10f, 0.25f, 0.10f, 0.9f};
    endTurnBtn.cornerRadius = 5.0f;
    endTurnBtn.onClick = [this]() {
        this->handleEndTurn();
    };

    // The button is inside the panel container so it gets the panel background
    [[maybe_unused]] aoc::ui::WidgetId btnId = this->m_uiManager.createButton(
        this->m_endTurnButton,
        {0.0f, 0.0f, 130.0f, 40.0f},
        std::move(endTurnBtn));

    // "Waiting for you" banner above the end-turn button — visible when
    // the human player is the last one still acting this turn.
    this->m_lastPlayerBanner = this->m_uiManager.createPanel(
        {0.0f, 0.0f, 150.0f, 24.0f},
        aoc::ui::PanelData{{0.8f, 0.6f, 0.1f, 0.9f}, 4.0f});
    {
        aoc::ui::Widget* bannerPanel = this->m_uiManager.getWidget(this->m_lastPlayerBanner);
        if (bannerPanel != nullptr) {
            bannerPanel->anchor = aoc::ui::Anchor::BottomRight;
            bannerPanel->marginRight  = 10.0f;
            bannerPanel->marginBottom = 65.0f;
            bannerPanel->isVisible = false;  // Hidden by default
        }
    }
    this->m_uiManager.createLabel(
        this->m_lastPlayerBanner,
        {4.0f, 2.0f, 142.0f, 20.0f},
        aoc::ui::LabelData{"Waiting for you!", {1.0f, 1.0f, 1.0f, 1.0f}, 12.0f});

    // Victory announcement panel (hidden until game over, centered on screen)
    aoc::ui::WidgetId victoryPanel = this->m_uiManager.createPanel(
        {0.0f, 0.0f, 500.0f, 50.0f},
        aoc::ui::PanelData{{0.1f, 0.1f, 0.15f, 0.9f}, 6.0f});
    this->m_victoryLabel = this->m_uiManager.createLabel(
        victoryPanel,
        {10.0f, 10.0f, 480.0f, 30.0f},
        aoc::ui::LabelData{"", {1.0f, 0.85f, 0.2f, 1.0f}, 24.0f});
    {
        aoc::ui::Widget* vPanel = this->m_uiManager.getWidget(victoryPanel);
        if (vPanel != nullptr) {
            vPanel->isVisible = false;
            vPanel->anchor = aoc::ui::Anchor::Center;
        }
    }
}

void Application::updateDiploStrip() {
    if (this->m_diploStrip == aoc::ui::INVALID_WIDGET) { return; }

    // Drop existing children; we'll rebuild from current state.
    aoc::ui::Widget* strip = this->m_uiManager.getWidget(this->m_diploStrip);
    if (strip == nullptr) { return; }
    const std::vector<aoc::ui::WidgetId> oldChildren = strip->children;
    for (aoc::ui::WidgetId c : oldChildren) {
        this->m_uiManager.removeWidget(c);
    }

    constexpr float ICON_SIZE = 22.0f;
    const PlayerId human = 0;

    for (const std::unique_ptr<aoc::game::Player>& p : this->m_gameState.players()) {
        const PlayerId pid = p->id();
        if (pid == human) { continue; }
        const bool met = (this->m_diplomacy.haveMet(human, pid));
        const bool atWar = met && this->m_diplomacy.relation(human, pid).isAtWar;

        aoc::ui::IconData icon;
        if (met) {
            icon.tint = aoc::ui::theme().playerColor(static_cast<uint8_t>(pid));
            icon.fallbackColor = icon.tint;
            // Click → open DiplomacyScreen focused on this civ.
            icon.onClick = [this]() {
                if (!this->m_diplomacyScreen.isOpen()) {
                    this->m_diplomacyScreen.setContext(
                        &this->m_gameState, PlayerId{0}, &this->m_diplomacy);
                    this->m_diplomacyScreen.open(this->m_uiManager);
                }
            };
        } else {
            icon.tint = {1.0f, 1.0f, 1.0f, 1.0f};
            icon.fallbackColor = {0.35f, 0.35f, 0.42f, 1.0f};
        }

        aoc::ui::WidgetId iconId = this->m_uiManager.createIcon(
            this->m_diploStrip, {0.0f, 0.0f, ICON_SIZE, ICON_SIZE}, icon);

        // Hover tooltip: civ name + stance or "Unknown civilization".
        std::string tooltip;
        if (met) {
            const aoc::sim::CivilizationDef& cdef = aoc::sim::civDef(p->civId());
            tooltip = std::string(cdef.name) + " (" +
                      std::string(cdef.leaderName) + ")";
            tooltip += "\nStance: ";
            tooltip += std::string(
                aoc::sim::stanceName(this->m_diplomacy.relation(human, pid).stance()));
            if (atWar) { tooltip += "\nAT WAR"; }
        } else {
            tooltip = "Unknown civilization\nMake contact to reveal";
        }
        this->m_uiManager.setWidgetTooltip(iconId, std::move(tooltip));

        // Clickable → open diplomacy screen focused on this civ.
        aoc::ui::Widget* iw = this->m_uiManager.getWidget(iconId);
        if (iw != nullptr) {
            // Flash red border if at war — rely on `flash` animation.
            if (atWar) {
                this->m_uiManager.flash(iconId,
                    {0.8f, 0.2f, 0.2f, 0.4f}, 1.0f);
            }
        }
    }
    this->m_uiManager.layout();
}

void Application::updateHUD() {
    // Fire any declarative bindings first: screens register label /
    // button / visibility suppliers via `bindLabel`, and this pass
    // drains them into the widget tree before the imperative per-frame
    // updates below.
    this->m_uiManager.updateBindings();

    // Refresh the top-bar diplo strip. Rebuilt every frame since the
    // widget list is tiny (≤16 major players) and meet-state changes
    // are rare but need to surface immediately.
    this->updateDiploStrip();

    // Update resource reveal state for map rendering (tech-gated resources)
    {
        std::vector<bool> revealed(aoc::sim::goodCount(), true);  // Default: all visible
        const aoc::sim::PlayerTechComponent* playerTech = (this->m_gameState.player(0) != nullptr) ? &this->m_gameState.player(0)->tech() : nullptr;
        for (uint16_t gid = 0; gid < aoc::sim::goodCount(); ++gid) {
            TechId revealTech = aoc::sim::resourceRevealTech(gid);
            if (revealTech.isValid()) {
                revealed[gid] = (playerTech != nullptr && playerTech->hasResearched(revealTech));
            }
        }
        this->m_gameRenderer.mapRenderer().setRevealedResources(revealed);
    }

    // Update turn label with year display
    const TurnNumber currentTurn = this->m_turnManager.currentTurn();
    std::string turnText = "Turn " + std::to_string(currentTurn)
                         + " (" + turnToYear(currentTurn) + ")";
    this->m_uiManager.setLabelText(this->m_turnLabel, std::move(turnText));

    // Update economy label
    std::string econText;
    {
        const aoc::game::Player* econPlayer = this->m_gameState.player(0);
        if (econPlayer != nullptr) {
            const aoc::sim::MonetaryStateComponent& ms = econPlayer->monetary();
            econText = std::string(aoc::sim::monetarySystemName(ms.system));
            econText += "  T:" + std::to_string(ms.treasury);
            econText += "  " + std::string(aoc::sim::coinTierName(ms.effectiveCoinTier));
            if (ms.system != aoc::sim::MonetarySystemType::Barter) {
                econText += "  M:" + std::to_string(ms.moneySupply);
                int inflPct = static_cast<int>(ms.inflationRate * 100.0f);
                econText += "  Infl:" + std::to_string(inflPct) + "%";
            }
        } else {
            econText = "No economy";
        }
    }
    this->m_uiManager.setLabelText(this->m_economyLabel, std::move(econText));

    // Selection-change detection: rebuild the unit action panel whenever
    // the selected unit or city pointer changes. Without this, clicking
    // a unit on the map sets `m_selectedUnit` but the action panel stays
    // in its null-selection "End Turn only" form — left-click appears to
    // do nothing even though right-click-to-move still works because
    // `handleContextAction` reads `m_selectedUnit` directly.
    if (this->m_selectedUnit != this->m_prevSelectedUnit
        || this->m_selectedCity != this->m_prevSelectedCity) {
        this->rebuildUnitActionPanel();
        this->m_prevSelectedUnit = this->m_selectedUnit;
        this->m_prevSelectedCity = this->m_selectedCity;
    }

    // Update selection label
    std::string selText;
    if (this->m_selectedUnit != nullptr) {
        const aoc::sim::UnitTypeDef& def = this->m_selectedUnit->typeDef();
        selText = std::string(def.name)
                + " HP:" + std::to_string(this->m_selectedUnit->hitPoints())
                + " MP:" + std::to_string(this->m_selectedUnit->movementRemaining());
    } else if (this->m_selectedCity != nullptr) {
        selText = this->m_selectedCity->name()
                + " Pop:" + std::to_string(this->m_selectedCity->population());
    } else {
        selText = "No selection";
    }
    this->m_uiManager.setLabelText(this->m_selectionLabel, std::move(selText));

    // Update screen size for anchor-based repositioning
    const std::pair<uint32_t, uint32_t> hudUpdateFbSize = this->m_window.framebufferSize();
    const uint32_t fbWidth = hudUpdateFbSize.first;
    const uint32_t fbHeight = hudUpdateFbSize.second;
    this->m_uiManager.setScreenSize(static_cast<float>(fbWidth),
                                     static_cast<float>(fbHeight));

    // Keep game screen dimensions in sync so open() uses correct values
    const float hudScreenW = static_cast<float>(fbWidth);
    const float hudScreenH = static_cast<float>(fbHeight);
    this->m_productionScreen.setScreenSize(hudScreenW, hudScreenH);
    this->m_techScreen.setScreenSize(hudScreenW, hudScreenH);
    this->m_governmentScreen.setScreenSize(hudScreenW, hudScreenH);
    this->m_economyScreen.setScreenSize(hudScreenW, hudScreenH);
    this->m_tradeScreen.setScreenSize(hudScreenW, hudScreenH);
    this->m_diplomacyScreen.setScreenSize(hudScreenW, hudScreenH);
    this->m_religionScreen.setScreenSize(hudScreenW, hudScreenH);
    this->m_scoreScreen.setScreenSize(hudScreenW, hudScreenH);
    this->m_cityDetailScreen.setScreenSize(hudScreenW, hudScreenH);

    // Update top bar width to match screen (stretches across full width)
    aoc::ui::Widget* topBar = this->m_uiManager.getWidget(this->m_topBar);
    if (topBar != nullptr) {
        topBar->requestedBounds.w = static_cast<float>(fbWidth);
    }

    // Update resource display in top bar
    if (this->m_resourceLabel != aoc::ui::INVALID_WIDGET) {
        std::string resText;

        // Gold, science, culture, faith -- sourced from GameState object model.
        // The ECS still has the canonical values (economy simulation writes there),
        // but we read from GameState to exercise the new path during migration.
        const aoc::game::Player* humanHud = this->m_gameState.humanPlayer();
        if (humanHud != nullptr) {
            // Gold (total + per turn income/loss)
            CurrencyAmount goldTreasury = humanHud->treasury();
            CurrencyAmount goldIncome   = humanHud->incomePerTurn();
            resText = "Gold:" + std::to_string(goldTreasury);
            if (goldIncome >= 0) {
                resText += "(+" + std::to_string(goldIncome) + "/turn)";
            } else {
                resText += "(" + std::to_string(goldIncome) + "/turn)";
            }

            // Science (+X/turn)
            float totalScience = humanHud->sciencePerTurn(this->m_hexGrid);
            int32_t sciInt = static_cast<int32_t>(totalScience);
            resText += "  Sci:(+" + std::to_string(sciInt) + "/turn)";

            // Culture (+X/turn)
            float totalCulture = humanHud->culturePerTurn(this->m_hexGrid);
            int32_t culInt = static_cast<int32_t>(totalCulture);
            resText += "  Cul:(+" + std::to_string(culInt) + "/turn)";

            // Faith (total)
            int32_t faithTotal = static_cast<int32_t>(humanHud->faith().faith);
            resText += "  Faith:" + std::to_string(faithTotal);
        } else {
            // GameState not yet populated — show zeroed values
            resText = "Gold:0(+0/turn)";

            float totalScience = aoc::sim::computePlayerScience(this->m_gameState, this->m_hexGrid, 0);
            int32_t sciInt = static_cast<int32_t>(totalScience);
            resText += "  Sci:(+" + std::to_string(sciInt) + "/turn)";

            float totalCulture = aoc::sim::computePlayerCulture(this->m_gameState, this->m_hexGrid, 0);
            int32_t culInt = static_cast<int32_t>(totalCulture);
            resText += "  Cul:(+" + std::to_string(culInt) + "/turn)";
            resText += "  Faith:0";
        }

        // Aggregate stockpile goods across all player 0 cities via GameState
        {
            const aoc::game::Player* stockPlayer = this->m_gameState.player(0);
            if (stockPlayer != nullptr) {
                std::unordered_map<uint16_t, int32_t> totals;
                for (const std::unique_ptr<aoc::game::City>& city : stockPlayer->cities()) {
                    for (const std::pair<const uint16_t, int32_t>& entry : city->stockpile().goods) {
                        totals[entry.first] += entry.second;
                    }
                }
                for (const std::pair<const uint16_t, int32_t>& entry : totals) {
                    if (entry.second > 0 && entry.first < aoc::sim::goodCount()) {
                        const aoc::sim::GoodDef& def = aoc::sim::goodDef(entry.first);
                        if (!resText.empty()) {
                            resText += "  ";
                        }
                        resText += std::string(def.name) + ":" + std::to_string(entry.second);
                    }
                }
            }
        }
        if (resText.empty()) {
            resText = "No resources";
        }
        this->m_uiManager.setLabelText(this->m_resourceLabel, std::move(resText));
    }

    // Update research progress bar
    {
        constexpr float RESEARCH_BAR_MAX_W = 220.0f;
        std::string researchText = "No research";
        float researchFraction = 0.0f;

        const aoc::game::Player* techPlayer = this->m_gameState.player(0);
        if (techPlayer != nullptr) {
            const aoc::sim::PlayerTechComponent& tech = techPlayer->tech();
            if (tech.currentResearch.isValid()) {
                const aoc::sim::TechDef& tdef = aoc::sim::techDef(tech.currentResearch);
                researchText = "Research: " + std::string(tdef.name) + " "
                             + std::to_string(static_cast<int>(tech.researchProgress))
                             + "/" + std::to_string(tdef.researchCost);
                if (tdef.researchCost > 0) {
                    researchFraction = tech.researchProgress / static_cast<float>(tdef.researchCost);
                    if (researchFraction > 1.0f) { researchFraction = 1.0f; }
                }
            }
        }
        this->m_uiManager.setLabelText(this->m_researchLabel, std::move(researchText));

        aoc::ui::Widget* fillWidget = this->m_uiManager.getWidget(this->m_researchBarFill);
        if (fillWidget != nullptr) {
            fillWidget->requestedBounds.w = researchFraction * RESEARCH_BAR_MAX_W;
        }
    }

    // Update production progress bar (visible when city selected)
    {
        constexpr float PROD_BAR_MAX_W = 220.0f;
        bool showProd = false;
        std::string prodText;
        float prodFraction = 0.0f;

        if (this->m_selectedCity != nullptr) {
            const aoc::sim::ProductionQueueComponent* queue =
                &this->m_selectedCity->production();
            if (queue != nullptr) {
                const aoc::sim::ProductionQueueItem* current = queue->currentItem();
                if (current != nullptr) {
                    showProd = true;
                    prodText = "Production: " + current->name + " "
                             + std::to_string(static_cast<int>(current->progress))
                             + "/" + std::to_string(static_cast<int>(current->totalCost));
                    if (current->totalCost > 0.0f) {
                        prodFraction = current->progress / current->totalCost;
                        if (prodFraction > 1.0f) { prodFraction = 1.0f; }
                    }
                }
            }
        }

        this->m_uiManager.setVisible(this->m_productionLabel, showProd);
        this->m_uiManager.setVisible(this->m_productionBar, showProd);
        if (showProd) {
            this->m_uiManager.setLabelText(this->m_productionLabel, std::move(prodText));
            aoc::ui::Widget* fillWidget = this->m_uiManager.getWidget(this->m_productionBarFill);
            if (fillWidget != nullptr) {
                fillWidget->requestedBounds.w = prodFraction * PROD_BAR_MAX_W;
            }
        }
    }

    // Rebuild unit action panel when selection changes
    this->rebuildUnitActionPanel();

    // Victory announcement
    if (this->m_gameOver && this->m_victoryLabel != aoc::ui::INVALID_WIDGET) {
        // Show the parent panel (which contains the label)
        aoc::ui::Widget* vLabel = this->m_uiManager.getWidget(this->m_victoryLabel);
        if (vLabel != nullptr && vLabel->parent != aoc::ui::INVALID_WIDGET) {
            aoc::ui::Widget* vPanel = this->m_uiManager.getWidget(vLabel->parent);
            if (vPanel != nullptr) {
                vPanel->isVisible = true;
                vPanel->requestedBounds.x = static_cast<float>(fbWidth) * 0.5f - 250.0f;
                vPanel->requestedBounds.y = static_cast<float>(fbHeight) * 0.5f - 25.0f;
            }
        }

        const char* victoryName =
            this->m_victoryResult.type == aoc::sim::VictoryType::Science    ? "Science" :
            this->m_victoryResult.type == aoc::sim::VictoryType::Domination ? "Domination" :
            this->m_victoryResult.type == aoc::sim::VictoryType::Culture    ? "Culture" :
            this->m_victoryResult.type == aoc::sim::VictoryType::Score      ? "Score" : "Unknown";

        std::string victoryText = "Player " +
            std::to_string(static_cast<unsigned>(this->m_victoryResult.winner)) +
            " wins by " + victoryName + " Victory!";
        this->m_uiManager.setLabelText(this->m_victoryLabel, std::move(victoryText));
    }
}

// ============================================================================
// Unit action panel
// ============================================================================

void Application::rebuildUnitActionPanel() {
    // Skip only when the panel already exists, is built for the current
    // selection AND matches the current city-detail-open state (the
    // panel's bottom-right margin shifts when the city overlay
    // opens/closes so the HUD stays visible).
    if (this->m_unitActionPanel != aoc::ui::INVALID_WIDGET
        && this->m_actionPanelUnit == this->m_selectedUnit
        && this->m_actionPanelCityOpen == this->m_cityDetailScreen.isOpen()) {
        return;
    }

    // Destroy old panel
    if (this->m_unitActionPanel != aoc::ui::INVALID_WIDGET) {
        this->m_uiManager.removeWidget(this->m_unitActionPanel);
        this->m_unitActionPanel = aoc::ui::INVALID_WIDGET;
    }
    this->m_actionPanelUnit = this->m_selectedUnit;
    this->m_actionPanelCityOpen = this->m_cityDetailScreen.isOpen();

    // When the city detail panel is open (right-side, ~350px wide),
    // shift the bottom-right HUD past its left edge so buttons aren't
    // hidden underneath. Fallback margin is 10px when no overlay.
    const float hudRightMargin =
        this->m_cityDetailScreen.isOpen() ? 360.0f : 10.0f;

    // If no unit selected, show minimal End Turn panel
    if (this->m_selectedUnit == nullptr) {
        constexpr float MIN_W = 150.0f;
        constexpr float MIN_H = 50.0f;
        this->m_unitActionPanel = this->m_uiManager.createPanel(
            {0.0f, 0.0f, MIN_W, MIN_H},
            aoc::ui::PanelData{{0.08f, 0.08f, 0.12f, 0.90f}, 6.0f});
        // Fade-in animation: start at alpha 0 and tween toward 1.
        // `tickAnimations` integrates each frame; PanelData render
        // multiplies widget alpha into its own colour alpha.
        if (aoc::ui::Widget* p0 = this->m_uiManager.getWidget(this->m_unitActionPanel)) {
            p0->alpha = 0.0f;
        }
        this->m_uiManager.tweenAlpha(this->m_unitActionPanel, 1.0f, 0.12f);
        {
            aoc::ui::Widget* p = this->m_uiManager.getWidget(this->m_unitActionPanel);
            if (p != nullptr) {
                p->padding = {8.0f, 8.0f, 8.0f, 8.0f};
                p->anchor = aoc::ui::Anchor::BottomRight;
                p->marginRight  = hudRightMargin;
                p->marginBottom = 10.0f;
            }
        }
        aoc::ui::ButtonData endBtn;
        endBtn.label = "End Turn";
        endBtn.fontSize = 13.0f;
        endBtn.normalColor = {0.15f, 0.35f, 0.15f, 0.9f};
        endBtn.hoverColor = {0.20f, 0.50f, 0.20f, 0.9f};
        endBtn.pressedColor = {0.10f, 0.25f, 0.10f, 0.9f};
        endBtn.labelColor = {1.0f, 1.0f, 1.0f, 1.0f};
        endBtn.cornerRadius = 4.0f;
        endBtn.onClick = [this]() { this->handleEndTurn(); };
        (void)this->m_uiManager.createButton(
            this->m_unitActionPanel,
            {0.0f, 0.0f, MIN_W - 16.0f, 34.0f}, std::move(endBtn));
        this->m_uiManager.layout();
        return;
    }

    const aoc::game::Unit& unit = *this->m_selectedUnit;
    const aoc::sim::UnitTypeDef& def = unit.typeDef();

    // Count buttons to size the panel
    int32_t buttonCount = 2;  // Skip + Sleep always
    if (aoc::sim::isMilitary(def.unitClass)) {
        ++buttonCount;  // Fortify
    }
    if (def.unitClass == aoc::sim::UnitClass::Scout) {
        ++buttonCount;  // Auto-Explore
    }
    if (def.unitClass == aoc::sim::UnitClass::Settler) {
        ++buttonCount;  // Found City
    }
    if (def.unitClass == aoc::sim::UnitClass::Civilian) {
        buttonCount += 2;  // Improve + Auto-Improve
    }

    const std::vector<aoc::sim::UnitUpgradeDef> upgrades =
        aoc::sim::getAvailableUpgrades(unit.typeId());
    if (!upgrades.empty()) {
        ++buttonCount;  // Upgrade
    }

    constexpr float BTN_W = 90.0f;
    constexpr float BTN_H = 24.0f;
    constexpr float BTN_SPACING = 3.0f;
    constexpr float PAD = 8.0f;
    // Bottom-right panel with unit info + action buttons + End Turn
    constexpr float PANEL_W = 280.0f;
    // Height: info header (50) + buttons rows + end turn button (40) + padding
    int32_t buttonRows = (buttonCount + 2) / 3;  // 3 buttons per row
    const float PANEL_H = 55.0f + static_cast<float>(buttonRows) * (BTN_H + BTN_SPACING) + 45.0f + PAD * 2.0f;

    this->m_unitActionPanel = this->m_uiManager.createPanel(
        {0.0f, 0.0f, PANEL_W, PANEL_H},
        aoc::ui::PanelData{{0.08f, 0.08f, 0.12f, 0.90f}, 6.0f});
    // Fade-in so the full-button panel doesn't pop instantly.
    if (aoc::ui::Widget* p1 = this->m_uiManager.getWidget(this->m_unitActionPanel)) {
        p1->alpha = 0.0f;
    }
    this->m_uiManager.tweenAlpha(this->m_unitActionPanel, 1.0f, 0.12f);
    {
        aoc::ui::Widget* panel = this->m_uiManager.getWidget(this->m_unitActionPanel);
        if (panel != nullptr) {
            panel->padding = {PAD, PAD, PAD, PAD};
            panel->childSpacing = 3.0f;
            panel->anchor = aoc::ui::Anchor::BottomRight;
            // Shift left past the city-detail panel when it's open so
            // the unit-action HUD never hides behind the overlay.
            panel->marginRight  = hudRightMargin;
            panel->marginBottom = 10.0f;
        }
    }

    // -- Unit info header --
    {
        char infoBuf[128];
        std::snprintf(infoBuf, sizeof(infoBuf), "%.*s   HP: %d/%d   MP: %d/%d",
                      static_cast<int>(def.name.size()), def.name.data(),
                      unit.hitPoints(), def.maxHitPoints,
                      unit.movementRemaining(), def.movementPoints);
        (void)this->m_uiManager.createLabel(
            this->m_unitActionPanel,
            {0.0f, 0.0f, PANEL_W - PAD * 2.0f, 16.0f},
            aoc::ui::LabelData{std::string(infoBuf),
                               {0.9f, 0.85f, 0.6f, 1.0f}, 11.0f});

        // Combat strength info for military units
        if (aoc::sim::isMilitary(def.unitClass)) {
            char combatBuf[96];
            if (def.rangedStrength > 0) {
                std::snprintf(combatBuf, sizeof(combatBuf),
                              "Melee: %d  Ranged: %d (range %d)",
                              def.combatStrength, def.rangedStrength, def.range);
            } else {
                std::snprintf(combatBuf, sizeof(combatBuf),
                              "Combat Strength: %d", def.combatStrength);
            }
            (void)this->m_uiManager.createLabel(
                this->m_unitActionPanel,
                {0.0f, 0.0f, PANEL_W - PAD * 2.0f, 14.0f},
                aoc::ui::LabelData{std::string(combatBuf),
                                   {0.75f, 0.75f, 0.80f, 0.9f}, 10.0f});
        }

        // Separator
        (void)this->m_uiManager.createPanel(
            this->m_unitActionPanel,
            {0.0f, 0.0f, PANEL_W - PAD * 2.0f, 1.0f},
            aoc::ui::PanelData{{0.3f, 0.3f, 0.4f, 0.4f}, 0.0f});
    }

    // Helper to create action buttons
    aoc::game::Unit* selectedUnitPtr = this->m_selectedUnit;
    aoc::game::GameState* gsPtr = &this->m_gameState;

    // auto required: lambda type is unnameable
    auto makeActionBtn = [this](const std::string& label,
                                 aoc::ui::Color normalColor,
                                 std::function<void()> onClick) {
        constexpr float ACTION_BTN_W2 = 125.0f;
        constexpr float ACTION_BTN_H2 = 24.0f;
        aoc::ui::ButtonData btn;
        btn.label = label;
        btn.fontSize = 10.0f;
        btn.normalColor = normalColor;
        btn.hoverColor = {normalColor.r + 0.10f, normalColor.g + 0.10f,
                          normalColor.b + 0.10f, 0.9f};
        btn.pressedColor = {normalColor.r - 0.05f, normalColor.g - 0.05f,
                            normalColor.b - 0.05f, 0.9f};
        btn.labelColor = {1.0f, 1.0f, 1.0f, 1.0f};
        btn.cornerRadius = 3.0f;
        btn.onClick = std::move(onClick);
        (void)this->m_uiManager.createButton(
            this->m_unitActionPanel,
            {0.0f, 0.0f, ACTION_BTN_W2, ACTION_BTN_H2}, std::move(btn));
    };

    // -- Skip button (all units) --
    makeActionBtn("Skip", {0.25f, 0.25f, 0.30f, 0.9f},
        [this, selectedUnitPtr]() {
            if (selectedUnitPtr == nullptr) { return; }
            selectedUnitPtr->setMovementRemaining(0);
            LOG_INFO("Unit skipped turn");
        });

    // -- Sleep button (all units) --
    makeActionBtn("Sleep", {0.25f, 0.25f, 0.30f, 0.9f},
        [this, selectedUnitPtr]() {
            if (selectedUnitPtr == nullptr) { return; }
            selectedUnitPtr->setState(aoc::sim::UnitState::Sleeping);
            LOG_INFO("Unit sleeping");
        });

    // -- Auto-Explore button (Scout units) --
    if (def.unitClass == aoc::sim::UnitClass::Scout) {
        makeActionBtn("Auto-Explore", {0.20f, 0.25f, 0.35f, 0.9f},
            [this, selectedUnitPtr]() {
                if (selectedUnitPtr == nullptr) { return; }
                selectedUnitPtr->autoExplore = !selectedUnitPtr->autoExplore;
                if (selectedUnitPtr->autoExplore) {
                    LOG_INFO("Auto-explore enabled for scout");
                } else {
                    LOG_INFO("Auto-explore disabled for scout");
                }
            });
    }

    // -- Fortify button (military units) --
    if (aoc::sim::isMilitary(def.unitClass)) {
        makeActionBtn("Fortify", {0.20f, 0.30f, 0.20f, 0.9f},
            [this, selectedUnitPtr]() {
                if (selectedUnitPtr == nullptr) { return; }
                selectedUnitPtr->setState(aoc::sim::UnitState::Fortified);
                LOG_INFO("Unit fortified (+25%% defense)");
            });
    }

    // -- Found City button (Settler) --
    if (def.unitClass == aoc::sim::UnitClass::Settler) {
        makeActionBtn("Found City", {0.30f, 0.25f, 0.15f, 0.9f},
            [this, selectedUnitPtr]() {
                if (selectedUnitPtr == nullptr) { return; }

                const PlayerId cityOwner = selectedUnitPtr->owner();
                const aoc::hex::AxialCoord cityPos = selectedUnitPtr->position();

                aoc::game::Player* gsFounder = this->m_gameState.player(cityOwner);
                if (gsFounder == nullptr) { return; }

                const std::string cityName = aoc::sim::getNextCityName(this->m_gameState, cityOwner);
                const bool isFirstCity = (gsFounder->cityCount() == 0);

                aoc::sim::claimInitialTerritory(this->m_hexGrid, cityPos, cityOwner);

                aoc::game::City& newGsCity = gsFounder->addCity(cityPos, cityName);
                if (isFirstCity) {
                    newGsCity.setOriginalCapital(true);
                    newGsCity.setOriginalOwner(cityOwner);
                }
                newGsCity.autoAssignWorkers(this->m_hexGrid, aoc::sim::WorkerFocus::Balanced, gsFounder);

                // Remove the settler from the owning player and clear selection
                gsFounder->removeUnit(selectedUnitPtr);
                this->m_selectedUnit = nullptr;
                this->m_actionPanelUnit = nullptr;
                LOG_INFO("City founded via action panel!");

                {
                    aoc::game::Player* eurekaP = this->m_gameState.player(cityOwner);
                    if (eurekaP != nullptr) {
                        aoc::sim::checkEurekaConditions(*eurekaP,
                                                        aoc::sim::EurekaCondition::FoundCity);
                    }
                }
            });
    }

    // -- Improve button (Builder / Civilian) --
    if (def.unitClass == aoc::sim::UnitClass::Civilian) {
        makeActionBtn("Improve", {0.20f, 0.28f, 0.20f, 0.9f},
            [this, selectedUnitPtr]() {
                if (selectedUnitPtr == nullptr) { return; }

                const int32_t tileIndex = this->m_hexGrid.toIndex(selectedUnitPtr->position());
                const aoc::map::ImprovementType bestImpr =
                    aoc::sim::bestImprovementForTile(this->m_hexGrid, tileIndex);

                if (bestImpr != aoc::map::ImprovementType::None &&
                    this->m_hexGrid.improvement(tileIndex) == aoc::map::ImprovementType::None) {
                    this->m_hexGrid.setImprovement(tileIndex, bestImpr);
                    selectedUnitPtr->useCharge();
                    LOG_INFO("Builder placed improvement via action panel");
                    if (!selectedUnitPtr->hasCharges()) {
                        const PlayerId ownerId = selectedUnitPtr->owner();
                        aoc::game::Player* owner = this->m_gameState.player(ownerId);
                        if (owner != nullptr) {
                            owner->removeUnit(selectedUnitPtr);
                        }
                        this->m_selectedUnit = nullptr;
                        this->m_actionPanelUnit = nullptr;
                        LOG_INFO("Builder exhausted all charges");
                    }
                }
            });

        // -- Mine Mountain button: build MountainMine on an adjacent metal-bearing
        // mountain tile. The builder stays on its current passable tile; the
        // improvement is applied to the neighbor.
        makeActionBtn("Mine Mountain", {0.28f, 0.20f, 0.30f, 0.9f},
            [this, selectedUnitPtr]() {
                if (selectedUnitPtr == nullptr) { return; }
                const PlayerId ownerId = selectedUnitPtr->owner();
                const int32_t currentIdx = this->m_hexGrid.toIndex(selectedUnitPtr->position());
                if (this->m_hexGrid.owner(currentIdx) != ownerId) { return; }
                if (this->m_hexGrid.movementCost(currentIdx) <= 0) { return; }

                const std::array<aoc::hex::AxialCoord, 6> nbrs =
                    aoc::hex::neighbors(selectedUnitPtr->position());
                for (const aoc::hex::AxialCoord& nbr : nbrs) {
                    if (!this->m_hexGrid.isValid(nbr)) { continue; }
                    const int32_t nbrIdx = this->m_hexGrid.toIndex(nbr);
                    if (this->m_hexGrid.terrain(nbrIdx) != aoc::map::TerrainType::Mountain) { continue; }
                    if (this->m_hexGrid.improvement(nbrIdx) != aoc::map::ImprovementType::None) { continue; }
                    if (!aoc::sim::canPlaceImprovement(this->m_hexGrid, nbrIdx,
                            aoc::map::ImprovementType::MountainMine)) {
                        continue;
                    }
                    this->m_hexGrid.setImprovement(nbrIdx, aoc::map::ImprovementType::MountainMine);
                    if (this->m_hexGrid.owner(nbrIdx) == INVALID_PLAYER) {
                        this->m_hexGrid.setOwner(nbrIdx, ownerId);
                    }
                    selectedUnitPtr->useCharge();
                    LOG_INFO("Builder placed MountainMine on adjacent mountain via action panel");
                    if (!selectedUnitPtr->hasCharges()) {
                        aoc::game::Player* owner = this->m_gameState.player(ownerId);
                        if (owner != nullptr) {
                            owner->removeUnit(selectedUnitPtr);
                        }
                        this->m_selectedUnit = nullptr;
                        this->m_actionPanelUnit = nullptr;
                    }
                    break;
                }
            });

        // -- Auto-Improve toggle (Civilian units) --
        makeActionBtn("Auto-Improve", {0.20f, 0.28f, 0.30f, 0.9f},
            [this, selectedUnitPtr]() {
                if (selectedUnitPtr == nullptr) { return; }
                selectedUnitPtr->autoImprove = !selectedUnitPtr->autoImprove;
                if (selectedUnitPtr->autoImprove) {
                    LOG_INFO("Auto-improve enabled for builder");
                } else {
                    LOG_INFO("Auto-improve disabled for builder");
                }
            });
    }

    // -- Upgrade button (if upgrade available) --
    if (!upgrades.empty()) {
        const aoc::sim::UnitUpgradeDef& upg = upgrades[0];
        const int32_t cost = aoc::sim::upgradeCost(unit.typeId(), upg.to);
        const std::string upgLabel = "Upgrade (" + std::to_string(cost) + "g)";
        const UnitTypeId upgTo = upg.to;
        const PlayerId owner = unit.owner();
        const aoc::hex::AxialCoord unitPos = unit.position();
        makeActionBtn(upgLabel, {0.30f, 0.20f, 0.30f, 0.9f},
            [this, gsPtr, selectedUnitPtr, upgTo, owner, unitPos]() {
                if (selectedUnitPtr == nullptr) { return; }
                aoc::game::Player* upgradePlayer = gsPtr->player(owner);
                aoc::game::Unit* gsUnit = (upgradePlayer != nullptr)
                    ? upgradePlayer->unitAt(unitPos) : nullptr;
                if (gsUnit == nullptr) { return; }
                bool success = aoc::sim::upgradeUnit(*gsPtr, *gsUnit, upgTo, owner);
                if (success) {
                    LOG_INFO("Unit upgraded via action panel!");
                }
            });
    }

    // Separator before End Turn
    (void)this->m_uiManager.createPanel(
        this->m_unitActionPanel,
        {0.0f, 0.0f, PANEL_W - PAD * 2.0f, 1.0f},
        aoc::ui::PanelData{{0.3f, 0.3f, 0.4f, 0.4f}, 0.0f});

    // End Turn button integrated into the unit panel
    {
        aoc::ui::ButtonData endBtn;
        endBtn.label = "End Turn";
        endBtn.fontSize = 13.0f;
        endBtn.normalColor = {0.15f, 0.35f, 0.15f, 0.9f};
        endBtn.hoverColor = {0.20f, 0.50f, 0.20f, 0.9f};
        endBtn.pressedColor = {0.10f, 0.25f, 0.10f, 0.9f};
        endBtn.labelColor = {1.0f, 1.0f, 1.0f, 1.0f};
        endBtn.cornerRadius = 4.0f;
        endBtn.onClick = [this]() { this->handleEndTurn(); };
        (void)this->m_uiManager.createButton(
            this->m_unitActionPanel,
            {0.0f, 0.0f, PANEL_W - PAD * 2.0f, 34.0f}, std::move(endBtn));
    }

    this->m_uiManager.layout();
}

} // namespace aoc::app
