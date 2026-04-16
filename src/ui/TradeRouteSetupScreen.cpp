/**
 * @file TradeRouteSetupScreen.cpp
 * @brief Trade route setup screen: select Trader, pick destination, confirm.
 */

#include "aoc/ui/TradeRouteSetupScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Log.hpp"

#include <cstdio>
#include <string>

namespace aoc::ui {

void TradeRouteSetupScreen::setContext(aoc::game::GameState* gameState,
                                        aoc::map::HexGrid* grid,
                                        PlayerId humanPlayer,
                                        const aoc::sim::Market* market,
                                        aoc::sim::DiplomacyManager* diplomacy) {
    this->m_gameState = gameState;
    this->m_grid      = grid;
    this->m_player    = humanPlayer;
    this->m_market    = market;
    this->m_diplomacy = diplomacy;
}

void TradeRouteSetupScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }
    this->m_isOpen = true;
    this->m_selectedTrader = nullptr;
    this->m_selectedDest   = nullptr;

    WidgetId innerPanel = this->createScreenFrame(
        ui, "Trade Routes", 580.0f, 520.0f, this->m_screenW, this->m_screenH);

    // Status label
    this->m_statusLabel = ui.createLabel(
        innerPanel, {0.0f, 0.0f, 550.0f, 16.0f},
        LabelData{"Select a Trader unit:", {0.8f, 0.9f, 0.8f, 1.0f}, 13.0f});

    // Trader unit selection list
    this->m_traderList = ui.createScrollList(
        innerPanel, {0.0f, 0.0f, 550.0f, 140.0f});

    Widget* listWidget = ui.getWidget(this->m_traderList);
    if (listWidget != nullptr) {
        listWidget->padding = {4.0f, 4.0f, 4.0f, 4.0f};
        listWidget->childSpacing = 3.0f;
    }

    // Populate with the player's idle Trader units
    if (this->m_gameState != nullptr) {
        const aoc::game::Player* player = this->m_gameState->player(this->m_player);
        if (player != nullptr) {
            int32_t traderIndex = 0;
            for (const std::unique_ptr<aoc::game::Unit>& unitPtr : player->units()) {
                if (unitPtr->typeDef().unitClass != aoc::sim::UnitClass::Trader) {
                    continue;
                }

                // Check if this trader already has an active route
                bool isIdle = (unitPtr->trader().owner == INVALID_PLAYER);

                aoc::hex::AxialCoord pos = unitPtr->position();
                const aoc::sim::UnitTypeDef& typeDef = unitPtr->typeDef();

                // Build label: "Trader at (q,r) - Idle" or "Trader at (q,r) - Active route to ..."
                char posBuffer[64];
                std::snprintf(posBuffer, sizeof(posBuffer), "%.*s at (%d, %d)",
                              static_cast<int>(typeDef.name.size()),
                              typeDef.name.data(), pos.q, pos.r);

                std::string label;
                if (isIdle) {
                    label = std::string(posBuffer) + " - Idle";
                } else {
                    aoc::hex::AxialCoord dest = unitPtr->trader().destCityLocation;
                    char destBuf[32];
                    std::snprintf(destBuf, sizeof(destBuf), " - Route to (%d, %d)", dest.q, dest.r);
                    label = std::string(posBuffer) + destBuf;
                }

                // Idle traders are green, active are grey
                ButtonData btn;
                btn.label = std::move(label);
                btn.fontSize = 11.0f;
                if (isIdle) {
                    btn.normalColor  = {0.2f, 0.28f, 0.2f, 0.9f};
                    btn.hoverColor   = {0.3f, 0.38f, 0.3f, 0.9f};
                    btn.pressedColor = {0.15f, 0.2f, 0.15f, 0.9f};
                } else {
                    btn.normalColor  = {0.25f, 0.25f, 0.25f, 0.7f};
                    btn.hoverColor   = {0.3f, 0.3f, 0.3f, 0.7f};
                    btn.pressedColor = {0.2f, 0.2f, 0.2f, 0.7f};
                }
                btn.cornerRadius = 3.0f;

                aoc::game::Unit* unitRawPtr = unitPtr.get();
                const bool capturedIsIdle = isIdle;
                btn.onClick = [this, &ui, innerPanel, unitRawPtr, capturedIsIdle]() {
                    if (!capturedIsIdle) {
                        ui.setLabelText(this->m_statusLabel,
                                        "That Trader already has an active route.");
                        return;
                    }
                    this->m_selectedTrader = unitRawPtr;
                    this->m_selectedDest   = nullptr;
                    ui.setLabelText(this->m_statusLabel, "Select a destination city:");
                    this->buildDestinationPanel(ui, innerPanel);
                };

                (void)ui.createButton(this->m_traderList, {0.0f, 0.0f, 540.0f, 22.0f},
                                       std::move(btn));
                ++traderIndex;
            }

            if (traderIndex == 0) {
                (void)ui.createLabel(this->m_traderList, {0.0f, 0.0f, 540.0f, 16.0f},
                    LabelData{"No Trader units available. Build one in a city.",
                              {0.8f, 0.5f, 0.5f, 1.0f}, 11.0f});
            }
        }
    }

    ui.layout();
}

void TradeRouteSetupScreen::buildDestinationPanel(UIManager& ui, WidgetId innerPanel) {
    // Remove old destination and preview panels
    if (this->m_destPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_destPanel);
        this->m_destPanel = INVALID_WIDGET;
    }
    if (this->m_previewPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_previewPanel);
        this->m_previewPanel = INVALID_WIDGET;
    }

    this->m_destPanel = ui.createScrollList(
        innerPanel, {0.0f, 0.0f, 550.0f, 200.0f});

    Widget* destWidget = ui.getWidget(this->m_destPanel);
    if (destWidget != nullptr) {
        destWidget->padding = {4.0f, 4.0f, 4.0f, 4.0f};
        destWidget->childSpacing = 2.0f;
    }

    // Header: own cities first, then foreign
    (void)ui.createLabel(this->m_destPanel, {0.0f, 0.0f, 540.0f, 14.0f},
                          LabelData{"Your Cities:", {0.7f, 0.9f, 0.7f, 1.0f}, 11.0f});

    if (this->m_gameState == nullptr || this->m_selectedTrader == nullptr) {
        ui.layout();
        return;
    }

    // Own cities
    const aoc::game::Player* ownerPlayer = this->m_gameState->player(this->m_player);
    if (ownerPlayer != nullptr) {
        for (const std::unique_ptr<aoc::game::City>& city : ownerPlayer->cities()) {
            // Skip the origin city (closest city to trader — show it but grayed out
            // if it's the same tile as the trader)
            int32_t dist = aoc::hex::distance(this->m_selectedTrader->position(), city->location());

            char distBuf[32];
            std::snprintf(distBuf, sizeof(distBuf), " (%d tiles)", dist);
            std::string label = city->name() + distBuf;

            ButtonData btn;
            btn.label = std::move(label);
            btn.fontSize = 10.0f;
            btn.normalColor  = {0.2f, 0.25f, 0.2f, 0.9f};
            btn.hoverColor   = {0.3f, 0.35f, 0.3f, 0.9f};
            btn.pressedColor = {0.15f, 0.18f, 0.15f, 0.9f};
            btn.cornerRadius = 2.0f;

            aoc::game::City* cityRawPtr = city.get();
            btn.onClick = [this, &ui, innerPanel, cityRawPtr]() {
                this->m_selectedDest = cityRawPtr;
                this->buildRoutePreview(ui, innerPanel);
            };

            (void)ui.createButton(this->m_destPanel, {0.0f, 0.0f, 540.0f, 20.0f},
                                   std::move(btn));
        }
    }

    // Foreign cities
    (void)ui.createLabel(this->m_destPanel, {0.0f, 0.0f, 540.0f, 14.0f},
                          LabelData{"Foreign Cities:", {0.9f, 0.8f, 0.7f, 1.0f}, 11.0f});

    for (const std::unique_ptr<aoc::game::Player>& otherPlayer : this->m_gameState->players()) {
        if (otherPlayer->id() == this->m_player || otherPlayer->id() == BARBARIAN_PLAYER) {
            continue;
        }

        // Check diplomatic status
        bool isAtWar = false;
        bool hasTradeAgreement = false;
        if (this->m_diplomacy != nullptr) {
            const aoc::sim::PairwiseRelation& rel =
                this->m_diplomacy->relation(this->m_player, otherPlayer->id());
            isAtWar = rel.isAtWar;
            hasTradeAgreement = rel.hasOpenBorders || rel.totalScore() > -20;
        }

        const aoc::sim::CivilizationDef& civDefRef = aoc::sim::civDef(otherPlayer->civId());

        for (const std::unique_ptr<aoc::game::City>& city : otherPlayer->cities()) {
            int32_t dist = aoc::hex::distance(
                this->m_selectedTrader->position(), city->location());

            char distBuf[64];
            std::snprintf(distBuf, sizeof(distBuf), " (%d tiles) - %.*s",
                          dist, static_cast<int>(civDefRef.name.size()),
                          civDefRef.name.data());
            std::string label = city->name() + distBuf;

            ButtonData btn;
            btn.label = std::move(label);
            btn.fontSize = 10.0f;
            btn.cornerRadius = 2.0f;

            if (isAtWar) {
                // Cannot trade with enemies
                btn.normalColor  = {0.35f, 0.15f, 0.15f, 0.7f};
                btn.hoverColor   = {0.35f, 0.15f, 0.15f, 0.7f};
                btn.pressedColor = {0.35f, 0.15f, 0.15f, 0.7f};
            } else if (hasTradeAgreement) {
                btn.normalColor  = {0.2f, 0.2f, 0.28f, 0.9f};
                btn.hoverColor   = {0.3f, 0.3f, 0.38f, 0.9f};
                btn.pressedColor = {0.15f, 0.15f, 0.2f, 0.9f};
            } else {
                btn.normalColor  = {0.25f, 0.2f, 0.2f, 0.9f};
                btn.hoverColor   = {0.35f, 0.3f, 0.3f, 0.9f};
                btn.pressedColor = {0.18f, 0.15f, 0.15f, 0.9f};
            }

            aoc::game::City* cityRawPtr = city.get();
            const bool capturedIsAtWar = isAtWar;
            btn.onClick = [this, &ui, innerPanel, cityRawPtr, capturedIsAtWar]() {
                if (capturedIsAtWar) {
                    ui.setLabelText(this->m_statusLabel,
                                    "Cannot trade with a civilization you are at war with.");
                    return;
                }
                this->m_selectedDest = cityRawPtr;
                this->buildRoutePreview(ui, innerPanel);
            };

            (void)ui.createButton(this->m_destPanel, {0.0f, 0.0f, 540.0f, 20.0f},
                                   std::move(btn));
        }
    }

    ui.layout();
}

void TradeRouteSetupScreen::buildRoutePreview(UIManager& ui, WidgetId innerPanel) {
    // Remove old preview
    if (this->m_previewPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_previewPanel);
        this->m_previewPanel = INVALID_WIDGET;
    }

    if (this->m_selectedTrader == nullptr || this->m_selectedDest == nullptr
        || this->m_gameState == nullptr || this->m_grid == nullptr
        || this->m_market == nullptr) {
        return;
    }

    this->m_previewPanel = ui.createPanel(
        innerPanel, {0.0f, 0.0f, 550.0f, 120.0f},
        PanelData{{0.12f, 0.12f, 0.18f, 0.95f}, 4.0f});

    Widget* prevWidget = ui.getWidget(this->m_previewPanel);
    if (prevWidget != nullptr) {
        prevWidget->padding = {6.0f, 6.0f, 6.0f, 6.0f};
        prevWidget->childSpacing = 3.0f;
    }

    // Compute estimate
    aoc::sim::TradeRouteEstimate estimate = aoc::sim::estimateTradeRouteIncome(
        *this->m_gameState, *this->m_grid, *this->m_market,
        *this->m_selectedTrader, *this->m_selectedDest);

    // Route type name
    const char* routeTypeNames[] = {"Land", "Sea", "Air"};
    const char* routeTypeName = routeTypeNames[static_cast<int32_t>(estimate.routeType)];

    // Preview info
    char previewBuf[256];
    std::snprintf(previewBuf, sizeof(previewBuf),
                  "Route: %s | Distance: ~%d tiles | Round trip: ~%d turns",
                  routeTypeName, estimate.distanceTiles, estimate.roundTripTurns);
    (void)ui.createLabel(this->m_previewPanel, {0.0f, 0.0f, 540.0f, 14.0f},
                          LabelData{previewBuf, {0.8f, 0.85f, 0.9f, 1.0f}, 11.0f});

    char goldBuf[128];
    std::snprintf(goldBuf, sizeof(goldBuf),
                  "Est. gold per trip: ~%d | Destination: %s",
                  static_cast<int32_t>(estimate.estimatedGoldPerTrip),
                  this->m_selectedDest->name().c_str());
    (void)ui.createLabel(this->m_previewPanel, {0.0f, 0.0f, 540.0f, 14.0f},
                          LabelData{goldBuf, {0.9f, 0.9f, 0.6f, 1.0f}, 11.0f});

    // Cargo preview: show what goods would be loaded
    const aoc::game::Player* ownerPlayer = this->m_gameState->player(this->m_player);
    if (ownerPlayer != nullptr) {
        // Find origin city (closest to trader)
        const aoc::game::City* originCity = nullptr;
        int32_t bestDist = 9999;
        for (const std::unique_ptr<aoc::game::City>& c : ownerPlayer->cities()) {
            int32_t dist = aoc::hex::distance(this->m_selectedTrader->position(), c->location());
            if (dist < bestDist) {
                bestDist = dist;
                originCity = c.get();
            }
        }
        if (originCity != nullptr) {
            // Show top surplus goods from origin
            std::string cargoPreview = "Cargo from " + originCity->name() + ": ";
            int32_t shown = 0;
            for (const std::pair<const uint16_t, int32_t>& entry : originCity->stockpile().goods) {
                if (entry.second <= 0) { continue; }
                if (shown >= 4) {
                    cargoPreview += "...";
                    break;
                }
                if (shown > 0) { cargoPreview += ", "; }
                const aoc::sim::GoodDef& gDef = aoc::sim::goodDef(entry.first);
                cargoPreview += std::string(gDef.name) + " x" + std::to_string(entry.second / 2);
                ++shown;
            }
            if (shown == 0) {
                cargoPreview += "(no surplus goods)";
            }
            (void)ui.createLabel(this->m_previewPanel, {0.0f, 0.0f, 540.0f, 14.0f},
                                  LabelData{std::move(cargoPreview),
                                            {0.7f, 0.75f, 0.8f, 1.0f}, 10.0f});
        }
    }

    // "Establish Route" button
    ButtonData establishBtn;
    establishBtn.label = "Establish Trade Route";
    establishBtn.fontSize = 13.0f;
    establishBtn.normalColor  = {0.15f, 0.35f, 0.15f, 0.9f};
    establishBtn.hoverColor   = {0.20f, 0.50f, 0.20f, 0.9f};
    establishBtn.pressedColor = {0.10f, 0.25f, 0.10f, 0.9f};
    establishBtn.cornerRadius = 4.0f;

    establishBtn.onClick = [this, &ui]() {
        if (this->m_selectedTrader == nullptr || this->m_selectedDest == nullptr
            || this->m_gameState == nullptr || this->m_grid == nullptr
            || this->m_market == nullptr) {
            return;
        }

        aoc::ErrorCode result = aoc::sim::establishTradeRoute(
            *this->m_gameState, *this->m_grid, *this->m_market,
            this->m_diplomacy,
            *this->m_selectedTrader, *this->m_selectedDest);

        if (result == aoc::ErrorCode::Ok) {
            ui.setLabelText(this->m_statusLabel,
                            "Trade route established! Select another Trader or close.");
            LOG_INFO("Trade route established via setup screen");
        } else {
            ui.setLabelText(this->m_statusLabel,
                            "Failed to establish route (rejected or no path).");
            LOG_INFO("Trade route establishment failed (error code %d)",
                     static_cast<int32_t>(result));
        }

        // Clear selection so the player can set up another route
        this->m_selectedTrader = nullptr;
        this->m_selectedDest   = nullptr;

        // Remove destination and preview panels
        if (this->m_destPanel != INVALID_WIDGET) {
            ui.removeWidget(this->m_destPanel);
            this->m_destPanel = INVALID_WIDGET;
        }
        if (this->m_previewPanel != INVALID_WIDGET) {
            ui.removeWidget(this->m_previewPanel);
            this->m_previewPanel = INVALID_WIDGET;
        }
        ui.layout();
    };

    (void)ui.createButton(this->m_previewPanel, {0.0f, 0.0f, 200.0f, 28.0f},
                            std::move(establishBtn));

    ui.layout();
}

void TradeRouteSetupScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_statusLabel  = INVALID_WIDGET;
    this->m_traderList   = INVALID_WIDGET;
    this->m_destPanel    = INVALID_WIDGET;
    this->m_previewPanel = INVALID_WIDGET;
    this->m_selectedTrader = nullptr;
    this->m_selectedDest   = nullptr;
}

void TradeRouteSetupScreen::refresh(UIManager& /*ui*/) {
    // Static screen — no dynamic refresh needed while open.
    // Player re-opens the screen to see updated trader/city state.
}

} // namespace aoc::ui
