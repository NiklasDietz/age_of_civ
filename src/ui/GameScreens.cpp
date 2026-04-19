/**
 * @file GameScreens.cpp
 * @brief Implementation of modal game screens (production, tech, government, economy, city detail).
 */

#include "aoc/ui/GameScreens.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/map/Pathfinding.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace aoc::ui {

// ============================================================================
// ScreenBase
// ============================================================================

void ScreenBase::toggle(UIManager& ui) {
    if (this->m_isOpen) {
        this->close(ui);
    } else {
        this->open(ui);
    }
}

WidgetId ScreenBase::createScreenFrame(UIManager& ui, const std::string& title,
                                        float width, float height,
                                        float screenW, float screenH) {
    // Dark semi-transparent full-screen overlay as root
    this->m_rootPanel = ui.createPanel(
        {0.0f, 0.0f, screenW, screenH},
        PanelData{{0.0f, 0.0f, 0.0f, 0.5f}, 0.0f});

    // Centered inner panel
    const float panelX = (screenW - width) * 0.5f;
    const float panelY = (screenH - height) * 0.5f;
    WidgetId innerPanel = ui.createPanel(
        this->m_rootPanel,
        {panelX, panelY, width, height},
        PanelData{{0.1f, 0.1f, 0.15f, 0.95f}, 6.0f});

    Widget* inner = ui.getWidget(innerPanel);
    if (inner != nullptr) {
        inner->padding = {10.0f, 12.0f, 10.0f, 12.0f};
        inner->childSpacing = 6.0f;
    }

    // Title label at top
    (void)ui.createLabel(innerPanel, {0.0f, 0.0f, width - 24.0f, 22.0f},
                   LabelData{title, {1.0f, 0.9f, 0.5f, 1.0f}, 18.0f});

    // "Close [ESC]" button at bottom-right
    ButtonData closeBtn;
    closeBtn.label = "Close [ESC]";
    closeBtn.fontSize = 12.0f;
    closeBtn.normalColor = {0.3f, 0.15f, 0.15f, 0.9f};
    closeBtn.hoverColor = {0.45f, 0.2f, 0.2f, 0.9f};
    closeBtn.pressedColor = {0.2f, 0.1f, 0.1f, 0.9f};
    closeBtn.cornerRadius = 4.0f;
    closeBtn.onClick = [this, &ui]() {
        this->close(ui);
    };

    // Position in absolute coords relative to inner panel (bottom-right area)
    (void)ui.createButton(innerPanel,
                    {width - 124.0f, height - 50.0f, 100.0f, 28.0f},
                    std::move(closeBtn));

    return innerPanel;
}

// ============================================================================
// ProductionScreen
// ============================================================================

// Forward declaration — defined after CityDetailScreen below.
static aoc::game::City* resolveCityByLocation(aoc::game::GameState*, PlayerId, aoc::hex::AxialCoord);

void ProductionScreen::setContext(aoc::game::GameState* gameState, aoc::map::HexGrid* grid,
                                   aoc::hex::AxialCoord cityLocation, PlayerId player) {
    this->m_gameState  = gameState;
    this->m_grid       = grid;
    this->m_cityLocation = cityLocation;
    this->m_player     = player;
}

void ProductionScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    assert(this->m_gameState != nullptr);
    this->m_isOpen = true;

    WidgetId innerPanel = this->createScreenFrame(
        ui, "Production", 450.0f, 500.0f, this->m_screenW, this->m_screenH);

    // Locate the player and city in the GameState object model
    aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    aoc::game::City* city = nullptr;
    if (owningPlayer != nullptr) {
        for (const std::unique_ptr<aoc::game::City>& c : owningPlayer->cities()) {
            if (c->location() == this->m_cityLocation) {
                city = c.get();
                break;
            }
        }
    }

    // Current queue label
    std::string queueText = "Queue: empty";
    if (city != nullptr) {
        const aoc::sim::ProductionQueueItem* current = city->production().currentItem();
        if (current != nullptr) {
            queueText = "Building: " + current->name + " ("
                      + std::to_string(static_cast<int>(current->progress)) + "/"
                      + std::to_string(static_cast<int>(current->totalCost)) + ")";
        }
    }
    this->m_queueLabel = ui.createLabel(
        innerPanel, {0.0f, 0.0f, 420.0f, 16.0f},
        LabelData{std::move(queueText), {0.8f, 0.9f, 0.8f, 1.0f}, 13.0f});

    // Separator label
    (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 420.0f, 14.0f},
                   LabelData{"-- Available Items --", {0.6f, 0.6f, 0.7f, 1.0f}, 12.0f});

    // Scroll list of buildable items
    this->m_itemList = ui.createScrollList(
        innerPanel, {0.0f, 0.0f, 420.0f, 350.0f});

    Widget* listWidget = ui.getWidget(this->m_itemList);
    if (listWidget != nullptr) {
        listWidget->padding = {4.0f, 4.0f, 4.0f, 4.0f};
        listWidget->childSpacing = 3.0f;
    }

    // Build the list of available items using tech gating
    const std::vector<aoc::sim::BuildableItem> buildableItems =
        aoc::sim::getBuildableItems(*this->m_gameState, this->m_player, *resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation));

    for (const aoc::sim::BuildableItem& buildable : buildableItems) {
        std::string itemLabel = std::string(buildable.name) + " ("
                              + std::to_string(static_cast<int>(buildable.cost)) + ")";

        // Tag wonders for clarity
        if (buildable.type == aoc::sim::ProductionItemType::Wonder) {
            itemLabel += " (Wonder)";
        }

        ButtonData btn;
        btn.label = std::move(itemLabel);
        btn.fontSize = 12.0f;
        btn.cornerRadius = 3.0f;

        // Color-code by type
        switch (buildable.type) {
            case aoc::sim::ProductionItemType::Unit:
                btn.normalColor  = {0.2f, 0.2f, 0.28f, 0.9f};
                btn.hoverColor   = {0.3f, 0.3f, 0.38f, 0.9f};
                btn.pressedColor = {0.15f, 0.15f, 0.2f, 0.9f};
                break;
            case aoc::sim::ProductionItemType::Building:
                btn.normalColor  = {0.2f, 0.25f, 0.2f, 0.9f};
                btn.hoverColor   = {0.3f, 0.35f, 0.3f, 0.9f};
                btn.pressedColor = {0.15f, 0.18f, 0.15f, 0.9f};
                break;
            case aoc::sim::ProductionItemType::Wonder:
                btn.normalColor  = {0.28f, 0.22f, 0.15f, 0.9f};
                btn.hoverColor   = {0.40f, 0.32f, 0.20f, 0.9f};
                btn.pressedColor = {0.20f, 0.15f, 0.10f, 0.9f};
                break;
            case aoc::sim::ProductionItemType::District:
                btn.normalColor  = {0.2f, 0.2f, 0.25f, 0.9f};
                btn.hoverColor   = {0.3f, 0.3f, 0.35f, 0.9f};
                btn.pressedColor = {0.15f, 0.15f, 0.18f, 0.9f};
                break;
        }

        const aoc::sim::ProductionItemType itemType = buildable.type;
        const uint16_t itemId = buildable.id;
        const float itemCost = buildable.cost;
        const std::string itemName(buildable.name);
        aoc::game::City* cityPtr = city;
        btn.onClick = [cityPtr, itemType, itemId, itemCost, itemName]() {
            if (cityPtr == nullptr) {
                return;
            }
            aoc::sim::ProductionQueueItem item{};
            item.type      = itemType;
            item.itemId    = itemId;
            item.name      = itemName;
            item.totalCost = itemCost;
            item.progress  = 0.0f;
            cityPtr->production().queue.push_back(std::move(item));
            LOG_INFO("Enqueued: %s", itemName.c_str());
        };

        (void)ui.createButton(this->m_itemList, {0.0f, 0.0f, 410.0f, 24.0f}, std::move(btn));
    }

    ui.layout();
}

void ProductionScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_queueLabel = INVALID_WIDGET;
    this->m_itemList = INVALID_WIDGET;
}

void ProductionScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }

    aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    aoc::game::City* city = nullptr;
    if (owningPlayer != nullptr) {
        for (const std::unique_ptr<aoc::game::City>& c : owningPlayer->cities()) {
            if (c->location() == this->m_cityLocation) {
                city = c.get();
                break;
            }
        }
    }

    std::string queueText = "Queue: empty";
    if (city != nullptr) {
        const aoc::sim::ProductionQueueItem* current = city->production().currentItem();
        if (current != nullptr) {
            queueText = "Building: " + current->name + " ("
                      + std::to_string(static_cast<int>(current->progress)) + "/"
                      + std::to_string(static_cast<int>(current->totalCost)) + ")";
        }
    }
    ui.setLabelText(this->m_queueLabel, std::move(queueText));
}

// ============================================================================
// TechScreen
// ============================================================================

void TechScreen::setContext(aoc::game::GameState* gameState, PlayerId player) {
    this->m_gameState = gameState;
    this->m_player    = player;
}

void TechScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    assert(this->m_gameState != nullptr);
    this->m_isOpen = true;

    WidgetId innerPanel = this->createScreenFrame(
        ui, "Technology", 500.0f, 550.0f, this->m_screenW, this->m_screenH);

    // Find player tech component through the object model
    const aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    const aoc::sim::PlayerTechComponent* playerTech =
        (owningPlayer != nullptr) ? &owningPlayer->tech() : nullptr;

    // Current research label
    std::string currentText = "No active research";
    if (playerTech != nullptr && playerTech->currentResearch.isValid()) {
        const aoc::sim::TechDef& def = aoc::sim::techDef(playerTech->currentResearch);
        currentText = "Researching: " + std::string(def.name)
                    + " (" + std::to_string(static_cast<int>(playerTech->researchProgress))
                    + "/" + std::to_string(def.researchCost) + ")";
    }
    this->m_currentLabel = ui.createLabel(
        innerPanel, {0.0f, 0.0f, 470.0f, 16.0f},
        LabelData{std::move(currentText), {0.7f, 0.85f, 1.0f, 1.0f}, 13.0f});

    // Separator
    (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 470.0f, 14.0f},
                   LabelData{"-- All Technologies --", {0.6f, 0.6f, 0.7f, 1.0f}, 12.0f});

    // Tech list
    this->m_techList = ui.createScrollList(
        innerPanel, {0.0f, 0.0f, 470.0f, 400.0f});

    Widget* listWidget = ui.getWidget(this->m_techList);
    if (listWidget != nullptr) {
        listWidget->padding = {4.0f, 4.0f, 4.0f, 4.0f};
        listWidget->childSpacing = 3.0f;
    }

    const std::vector<aoc::sim::TechDef>& techs = aoc::sim::allTechs();
    for (const aoc::sim::TechDef& tech : techs) {
        if (playerTech != nullptr && playerTech->hasResearched(tech.id)) {
            // Completed
            std::string label = "[OK] " + std::string(tech.name)
                              + " (Era " + std::to_string(tech.era.value) + ")";
            (void)ui.createLabel(this->m_techList, {0.0f, 0.0f, 460.0f, 18.0f},
                           LabelData{std::move(label), {0.4f, 0.8f, 0.4f, 1.0f}, 12.0f});
        } else if (playerTech != nullptr && playerTech->canResearch(tech.id)) {
            // Available for research
            std::string label = "> " + std::string(tech.name)
                              + " (" + std::to_string(tech.researchCost) + ")";

            ButtonData btn;
            btn.label = std::move(label);
            btn.fontSize = 12.0f;
            btn.normalColor = {0.15f, 0.25f, 0.35f, 0.9f};
            btn.hoverColor = {0.2f, 0.35f, 0.5f, 0.9f};
            btn.pressedColor = {0.1f, 0.18f, 0.25f, 0.9f};
            btn.cornerRadius = 3.0f;

            const uint16_t techValue = tech.id.value;
            aoc::game::GameState* gsPtr = this->m_gameState;
            const PlayerId player = this->m_player;
            btn.onClick = [gsPtr, player, techValue]() {
                aoc::game::Player* p = gsPtr->player(player);
                if (p == nullptr) { return; }
                p->tech().currentResearch = TechId{techValue};
                p->tech().researchProgress = 0.0f;
                const aoc::sim::TechDef& def = aoc::sim::techDef(TechId{techValue});
                LOG_INFO("Now researching: %.*s",
                         static_cast<int>(def.name.size()), def.name.data());
            };

            (void)ui.createButton(this->m_techList, {0.0f, 0.0f, 460.0f, 24.0f}, std::move(btn));
        } else {
            // Locked
            std::string label = "-- " + std::string(tech.name) + " (locked)";
            (void)ui.createLabel(this->m_techList, {0.0f, 0.0f, 460.0f, 18.0f},
                           LabelData{std::move(label), {0.5f, 0.5f, 0.5f, 0.7f}, 12.0f});
        }
    }

    ui.layout();
}

void TechScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_currentLabel = INVALID_WIDGET;
    this->m_techList = INVALID_WIDGET;
}

void TechScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }

    const aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    const aoc::sim::PlayerTechComponent* playerTech =
        (owningPlayer != nullptr) ? &owningPlayer->tech() : nullptr;

    std::string currentText = "No active research";
    if (playerTech != nullptr && playerTech->currentResearch.isValid()) {
        const aoc::sim::TechDef& def = aoc::sim::techDef(playerTech->currentResearch);
        currentText = "Researching: " + std::string(def.name)
                    + " (" + std::to_string(static_cast<int>(playerTech->researchProgress))
                    + "/" + std::to_string(def.researchCost) + ")";
    }
    ui.setLabelText(this->m_currentLabel, std::move(currentText));
}

// ============================================================================
// GovernmentScreen
// ============================================================================

void GovernmentScreen::setContext(aoc::game::GameState* gameState, PlayerId player) {
    this->m_gameState = gameState;
    this->m_player    = player;
}

void GovernmentScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    assert(this->m_gameState != nullptr);
    this->m_isOpen = true;

    WidgetId innerPanel = this->createScreenFrame(
        ui, "Government", 450.0f, 400.0f, this->m_screenW, this->m_screenH);

    // Find player government component through object model
    aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    aoc::sim::PlayerGovernmentComponent* playerGov =
        (owningPlayer != nullptr) ? &owningPlayer->government() : nullptr;

    // Current government label
    std::string currentText = "Current: Unknown";
    if (playerGov != nullptr) {
        const aoc::sim::GovernmentDef& def = aoc::sim::governmentDef(playerGov->government);
        currentText = "Current: " + std::string(def.name);
    }
    this->m_currentGovLabel = ui.createLabel(
        innerPanel, {0.0f, 0.0f, 420.0f, 16.0f},
        LabelData{std::move(currentText), {0.9f, 0.85f, 0.6f, 1.0f}, 14.0f});

    // Available governments section
    (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 420.0f, 14.0f},
                   LabelData{"-- Available Governments --", {0.6f, 0.6f, 0.7f, 1.0f}, 12.0f});

    this->m_govList = ui.createScrollList(
        innerPanel, {0.0f, 0.0f, 420.0f, 200.0f});

    Widget* listWidget = ui.getWidget(this->m_govList);
    if (listWidget != nullptr) {
        listWidget->padding = {4.0f, 4.0f, 4.0f, 4.0f};
        listWidget->childSpacing = 3.0f;
    }

    for (uint8_t i = 0; i < aoc::sim::GOVERNMENT_COUNT; ++i) {
        const aoc::sim::GovernmentType govType = static_cast<aoc::sim::GovernmentType>(i);
        const aoc::sim::GovernmentDef& govDef = aoc::sim::governmentDef(govType);

        if (playerGov != nullptr && playerGov->isGovernmentUnlocked(govType)) {
            ButtonData btn;
            btn.label = std::string(govDef.name);
            btn.fontSize = 12.0f;
            btn.normalColor = {0.2f, 0.2f, 0.28f, 0.9f};
            btn.hoverColor = {0.3f, 0.3f, 0.38f, 0.9f};
            btn.pressedColor = {0.15f, 0.15f, 0.2f, 0.9f};
            btn.cornerRadius = 3.0f;

            aoc::game::GameState* gsPtr = this->m_gameState;
            const PlayerId player = this->m_player;
            btn.onClick = [gsPtr, player, govType]() {
                aoc::game::Player* p = gsPtr->player(player);
                if (p == nullptr) { return; }
                p->government().government = govType;
                const aoc::sim::GovernmentDef& def = aoc::sim::governmentDef(govType);
                LOG_INFO("Switched government to: %.*s",
                         static_cast<int>(def.name.size()), def.name.data());
            };

            (void)ui.createButton(this->m_govList, {0.0f, 0.0f, 410.0f, 24.0f}, std::move(btn));
        } else {
            std::string label = std::string(govDef.name) + " (locked)";
            (void)ui.createLabel(this->m_govList, {0.0f, 0.0f, 410.0f, 18.0f},
                           LabelData{std::move(label), {0.5f, 0.5f, 0.5f, 0.7f}, 12.0f});
        }
    }

    // Active policies section
    (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 420.0f, 14.0f},
                   LabelData{"-- Active Policies --", {0.6f, 0.6f, 0.7f, 1.0f}, 12.0f});

    if (playerGov != nullptr) {
        for (uint8_t slot = 0; slot < aoc::sim::MAX_POLICY_SLOTS; ++slot) {
            std::string policyText;
            if (playerGov->activePolicies[slot] != aoc::sim::EMPTY_POLICY_SLOT) {
                uint8_t polId = static_cast<uint8_t>(playerGov->activePolicies[slot]);
                const aoc::sim::PolicyCardDef& polDef = aoc::sim::policyCardDef(polId);
                policyText = "Slot " + std::to_string(slot + 1) + ": "
                           + std::string(polDef.name);
            } else {
                policyText = "Slot " + std::to_string(slot + 1) + ": [Empty]";
            }
            (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 420.0f, 16.0f},
                           LabelData{std::move(policyText), {0.7f, 0.7f, 0.8f, 1.0f}, 12.0f});
        }
    }

    ui.layout();
}

void GovernmentScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_currentGovLabel = INVALID_WIDGET;
    this->m_govList = INVALID_WIDGET;
}

void GovernmentScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }

    const aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    const aoc::sim::PlayerGovernmentComponent* playerGov =
        (owningPlayer != nullptr) ? &owningPlayer->government() : nullptr;

    std::string currentText = "Current: Unknown";
    if (playerGov != nullptr) {
        const aoc::sim::GovernmentDef& def = aoc::sim::governmentDef(playerGov->government);
        currentText = "Current: " + std::string(def.name);
    }
    ui.setLabelText(this->m_currentGovLabel, std::move(currentText));
}

// ============================================================================
// EconomyScreen
// ============================================================================

void EconomyScreen::setContext(aoc::game::GameState* gameState, const aoc::map::HexGrid* grid,
                                PlayerId player, const aoc::sim::Market* market) {
    this->m_gameState = gameState;
    this->m_grid      = grid;
    this->m_player    = player;
    this->m_market    = market;
}

void EconomyScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    assert(this->m_gameState != nullptr);
    this->m_isOpen = true;

    WidgetId innerPanel = this->createScreenFrame(
        ui, "Economy", 500.0f, 500.0f, this->m_screenW, this->m_screenH);

    // Find monetary state through object model
    const aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    const aoc::sim::MonetaryStateComponent* monetary =
        (owningPlayer != nullptr) ? &owningPlayer->monetary() : nullptr;

    // Economy info label
    std::string infoText = "No economic data";
    if (monetary != nullptr) {
        infoText = "System: " + std::string(aoc::sim::monetarySystemName(monetary->system))
                 + "  Coins: " + std::string(aoc::sim::coinTierName(monetary->effectiveCoinTier))
                 + "  Treasury: " + std::to_string(monetary->treasury)
                 + "  Money: " + std::to_string(monetary->moneySupply)
                 + "  Inflation: " + std::to_string(static_cast<int>(monetary->inflationRate * 100.0f)) + "%";
    }
    this->m_infoLabel = ui.createLabel(
        innerPanel, {0.0f, 0.0f, 470.0f, 16.0f},
        LabelData{std::move(infoText), {0.6f, 0.85f, 0.6f, 1.0f}, 12.0f});

    // Tax rate with +/- buttons
    if (monetary != nullptr) {
        std::string taxText = "Tax Rate: "
                            + std::to_string(static_cast<int>(monetary->taxRate * 100.0f)) + "%";
        (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 250.0f, 16.0f},
                       LabelData{std::move(taxText), {0.8f, 0.8f, 0.6f, 1.0f}, 12.0f});

        // Create a horizontal row for tax buttons
        WidgetId taxRow = ui.createPanel(
            innerPanel, {0.0f, 0.0f, 200.0f, 26.0f},
            PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

        Widget* taxRowWidget = ui.getWidget(taxRow);
        if (taxRowWidget != nullptr) {
            taxRowWidget->layoutDirection = LayoutDirection::Horizontal;
            taxRowWidget->childSpacing = 8.0f;
        }

        ButtonData minusBtn;
        minusBtn.label = "Tax -5%";
        minusBtn.fontSize = 11.0f;
        minusBtn.normalColor = {0.3f, 0.2f, 0.2f, 0.9f};
        minusBtn.hoverColor = {0.4f, 0.3f, 0.3f, 0.9f};
        minusBtn.pressedColor = {0.2f, 0.15f, 0.15f, 0.9f};
        minusBtn.cornerRadius = 3.0f;

        aoc::game::GameState* gsPtr = this->m_gameState;
        const PlayerId player = this->m_player;
        minusBtn.onClick = [gsPtr, player]() {
            aoc::game::Player* p = gsPtr->player(player);
            if (p == nullptr) { return; }
            p->monetary().taxRate -= 0.05f;
            if (p->monetary().taxRate < 0.0f) {
                p->monetary().taxRate = 0.0f;
            }
            LOG_INFO("Tax rate: %d%%", static_cast<int>(p->monetary().taxRate * 100.0f));
        };

        ButtonData plusBtn;
        plusBtn.label = "Tax +5%";
        plusBtn.fontSize = 11.0f;
        plusBtn.normalColor = {0.2f, 0.3f, 0.2f, 0.9f};
        plusBtn.hoverColor = {0.3f, 0.4f, 0.3f, 0.9f};
        plusBtn.pressedColor = {0.15f, 0.2f, 0.15f, 0.9f};
        plusBtn.cornerRadius = 3.0f;

        plusBtn.onClick = [gsPtr, player]() {
            aoc::game::Player* p = gsPtr->player(player);
            if (p == nullptr) { return; }
            p->monetary().taxRate += 0.05f;
            if (p->monetary().taxRate > 1.0f) {
                p->monetary().taxRate = 1.0f;
            }
            LOG_INFO("Tax rate: %d%%", static_cast<int>(p->monetary().taxRate * 100.0f));
        };

        (void)ui.createButton(taxRow, {0.0f, 0.0f, 80.0f, 22.0f}, std::move(minusBtn));
        (void)ui.createButton(taxRow, {0.0f, 0.0f, 80.0f, 22.0f}, std::move(plusBtn));
    }

    // "Create Trade Route" button
    {
        ButtonData tradeRouteBtn;
        tradeRouteBtn.label = "Create Trade Route";
        tradeRouteBtn.fontSize = 12.0f;
        tradeRouteBtn.normalColor  = {0.20f, 0.30f, 0.20f, 0.9f};
        tradeRouteBtn.hoverColor   = {0.30f, 0.40f, 0.30f, 0.9f};
        tradeRouteBtn.pressedColor = {0.15f, 0.20f, 0.15f, 0.9f};
        tradeRouteBtn.cornerRadius = 4.0f;
        tradeRouteBtn.onClick = [this, &ui, innerPanel]() {
            this->buildTradeRoutePanel(ui, innerPanel);
        };
        (void)ui.createButton(innerPanel, {0.0f, 0.0f, 180.0f, 26.0f}, std::move(tradeRouteBtn));
    }

    // Market prices section
    (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 470.0f, 14.0f},
                   LabelData{"-- Market Prices (Trend | Supply | Demand) --",
                              {0.6f, 0.6f, 0.7f, 1.0f}, 12.0f});

    this->m_marketList = ui.createScrollList(
        innerPanel, {0.0f, 0.0f, 470.0f, 200.0f});

    Widget* listWidget = ui.getWidget(this->m_marketList);
    if (listWidget != nullptr) {
        listWidget->padding = {4.0f, 4.0f, 4.0f, 4.0f};
        listWidget->childSpacing = 2.0f;
    }

    // Find player economy component for supply/demand
    const aoc::sim::PlayerEconomyComponent* playerEcon =
        (owningPlayer != nullptr) ? &owningPlayer->economy() : nullptr;

    // Collect goods data for sorting by trade volume
    struct GoodDisplayInfo {
        uint16_t goodId;
        int32_t  supply;
        int32_t  demand;
        int32_t  currentPrice;
        int32_t  volume;  // supply + demand
        std::string_view name;
        std::string trend;
    };
    std::vector<GoodDisplayInfo> goodsInfo;

    const uint16_t totalGoods = aoc::sim::goodCount();
    for (uint16_t goodId = 0; goodId < totalGoods; ++goodId) {
        const aoc::sim::GoodDef& gDef = aoc::sim::goodDef(goodId);
        if (gDef.basePrice <= 0) {
            continue;
        }

        GoodDisplayInfo info{};
        info.goodId = goodId;
        info.name = gDef.name;

        // Get current market price from Market if available
        if (this->m_market != nullptr) {
            info.currentPrice = this->m_market->price(goodId);

            // Compute price trend from priceHistory
            const aoc::sim::Market::GoodMarketData& mdata = this->m_market->marketData(goodId);
            constexpr int32_t HISTORY_SIZE = aoc::sim::Market::GoodMarketData::HISTORY_SIZE;
            // Current price index is (historyIndex - 1), 5 turns ago is (historyIndex - 6)
            int32_t currentIdx = (mdata.historyIndex - 1 + HISTORY_SIZE) % HISTORY_SIZE;
            int32_t oldIdx = (mdata.historyIndex - 6 + HISTORY_SIZE) % HISTORY_SIZE;
            int32_t currentP = mdata.priceHistory[currentIdx];
            int32_t oldP = mdata.priceHistory[oldIdx];
            if (oldP > 0) {
                float change = static_cast<float>(currentP - oldP) / static_cast<float>(oldP);
                if (change > 0.10f) {
                    info.trend = "^";
                } else if (change < -0.10f) {
                    info.trend = "v";
                } else {
                    info.trend = "=";
                }
            } else {
                info.trend = "=";
            }
        } else {
            info.currentPrice = gDef.basePrice;
            info.trend = "=";
        }

        info.supply = 0;
        info.demand = 0;
        if (playerEcon != nullptr) {
            {
                std::unordered_map<uint16_t, int32_t>::const_iterator it = playerEcon->totalSupply.find(goodId);
                if (it != playerEcon->totalSupply.end()) {
                    info.supply = it->second;
                }
            }
            {
                std::unordered_map<uint16_t, int32_t>::const_iterator it = playerEcon->totalDemand.find(goodId);
                if (it != playerEcon->totalDemand.end()) {
                    info.demand = it->second;
                }
            }
        }
        info.volume = info.supply + info.demand;

        goodsInfo.push_back(std::move(info));
    }

    // Show first 20 goods with price trend indicators
    uint32_t shownCount = 0;
    for (const GoodDisplayInfo& info : goodsInfo) {
        if (shownCount >= 20) {
            break;
        }

        std::string line = std::string(info.name)
                         + " " + info.trend
                         + " $" + std::to_string(info.currentPrice)
                         + " S:" + std::to_string(info.supply)
                         + " D:" + std::to_string(info.demand);

        // Color the trend indicator
        Color lineColor = {0.75f, 0.75f, 0.8f, 1.0f};
        if (info.trend == "^") {
            lineColor = {0.3f, 0.9f, 0.3f, 1.0f};
        } else if (info.trend == "v") {
            lineColor = {0.9f, 0.3f, 0.3f, 1.0f};
        }

        (void)ui.createLabel(this->m_marketList, {0.0f, 0.0f, 460.0f, 16.0f},
                       LabelData{std::move(line), lineColor, 11.0f});
        ++shownCount;
    }

    // Market Detail: top 10 goods by trade volume
    std::sort(goodsInfo.begin(), goodsInfo.end(),
              [](const GoodDisplayInfo& a, const GoodDisplayInfo& b) {
                  return a.volume > b.volume;
              });

    (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 470.0f, 14.0f},
                   LabelData{"-- Top 10 by Trade Volume --",
                              {0.6f, 0.6f, 0.7f, 1.0f}, 11.0f});

    uint32_t detailCount = 0;
    for (const GoodDisplayInfo& info : goodsInfo) {
        if (detailCount >= 10) {
            break;
        }
        if (info.volume <= 0) {
            break;
        }
        std::string detailLine = std::string(info.name)
                               + " Vol:" + std::to_string(info.volume)
                               + " $" + std::to_string(info.currentPrice);
        (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 470.0f, 14.0f},
                       LabelData{std::move(detailLine), {0.7f, 0.75f, 0.8f, 1.0f}, 10.0f});
        ++detailCount;
    }

    ui.layout();
}

void EconomyScreen::buildTradeRoutePanel(UIManager& ui, WidgetId parentPanel) {
    // Remove old trade route panel if any
    if (this->m_tradeRoutePanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_tradeRoutePanel);
        this->m_tradeRoutePanel = INVALID_WIDGET;
    }

    this->m_trSourcePlayerIdx = -1;
    this->m_trSourceCityIdx   = -1;
    this->m_trDestPlayerIdx   = -1;
    this->m_trDestCityIdx     = -1;

    this->m_tradeRoutePanel = ui.createPanel(
        parentPanel, {0.0f, 0.0f, 470.0f, 300.0f},
        PanelData{{0.12f, 0.12f, 0.16f, 0.95f}, 4.0f});

    Widget* trPanel = ui.getWidget(this->m_tradeRoutePanel);
    if (trPanel != nullptr) {
        trPanel->padding = {6.0f, 6.0f, 6.0f, 6.0f};
        trPanel->childSpacing = 4.0f;
    }

    (void)ui.createLabel(this->m_tradeRoutePanel, {0.0f, 0.0f, 450.0f, 16.0f},
                   LabelData{"-- Create Trade Route --", {1.0f, 0.9f, 0.5f, 1.0f}, 13.0f});

    // Source city selection (player's own cities)
    (void)ui.createLabel(this->m_tradeRoutePanel, {0.0f, 0.0f, 450.0f, 14.0f},
                   LabelData{"Source City (yours):", {0.7f, 0.8f, 0.7f, 1.0f}, 11.0f});

    WidgetId sourceList = ui.createScrollList(
        this->m_tradeRoutePanel, {0.0f, 0.0f, 450.0f, 80.0f});
    Widget* srcListW = ui.getWidget(sourceList);
    if (srcListW != nullptr) {
        srcListW->padding = {2.0f, 2.0f, 2.0f, 2.0f};
        srcListW->childSpacing = 2.0f;
    }

    const aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    if (owningPlayer != nullptr) {
        const int32_t playerIdx = static_cast<int32_t>(this->m_player);
        int32_t cityIdx = 0;
        for (const std::unique_ptr<aoc::game::City>& city : owningPlayer->cities()) {
            const int32_t capturedCityIdx = cityIdx;
            ButtonData srcBtn;
            srcBtn.label = city->name();
            srcBtn.fontSize = 10.0f;
            srcBtn.normalColor  = {0.2f, 0.25f, 0.2f, 0.9f};
            srcBtn.hoverColor   = {0.3f, 0.35f, 0.3f, 0.9f};
            srcBtn.pressedColor = {0.15f, 0.18f, 0.15f, 0.9f};
            srcBtn.cornerRadius = 2.0f;
            srcBtn.onClick = [this, playerIdx, capturedCityIdx]() {
                this->m_trSourcePlayerIdx = playerIdx;
                this->m_trSourceCityIdx   = capturedCityIdx;
                LOG_INFO("Trade route source city selected");
            };
            (void)ui.createButton(sourceList, {0.0f, 0.0f, 440.0f, 20.0f}, std::move(srcBtn));
            ++cityIdx;
        }
    }

    // Destination city selection (other players' cities)
    (void)ui.createLabel(this->m_tradeRoutePanel, {0.0f, 0.0f, 450.0f, 14.0f},
                   LabelData{"Destination City (other players):", {0.7f, 0.8f, 0.7f, 1.0f}, 11.0f});

    WidgetId destList = ui.createScrollList(
        this->m_tradeRoutePanel, {0.0f, 0.0f, 450.0f, 80.0f});
    Widget* dstListW = ui.getWidget(destList);
    if (dstListW != nullptr) {
        dstListW->padding = {2.0f, 2.0f, 2.0f, 2.0f};
        dstListW->childSpacing = 2.0f;
    }

    for (const std::unique_ptr<aoc::game::Player>& otherPlayer : this->m_gameState->players()) {
        if (otherPlayer->id() == this->m_player) {
            continue;
        }
        const int32_t destPlayerIdx = static_cast<int32_t>(otherPlayer->id());
        int32_t cityIdx = 0;
        for (const std::unique_ptr<aoc::game::City>& city : otherPlayer->cities()) {
            const int32_t capturedCityIdx = cityIdx;
            std::string destLabel = city->name() + " (P" + std::to_string(static_cast<unsigned>(otherPlayer->id())) + ")";
            ButtonData dstBtn;
            dstBtn.label = std::move(destLabel);
            dstBtn.fontSize = 10.0f;
            dstBtn.normalColor  = {0.25f, 0.2f, 0.2f, 0.9f};
            dstBtn.hoverColor   = {0.35f, 0.3f, 0.3f, 0.9f};
            dstBtn.pressedColor = {0.18f, 0.15f, 0.15f, 0.9f};
            dstBtn.cornerRadius = 2.0f;
            dstBtn.onClick = [this, destPlayerIdx, capturedCityIdx]() {
                this->m_trDestPlayerIdx = destPlayerIdx;
                this->m_trDestCityIdx   = capturedCityIdx;
                LOG_INFO("Trade route destination city selected");
            };
            (void)ui.createButton(destList, {0.0f, 0.0f, 440.0f, 20.0f}, std::move(dstBtn));
            ++cityIdx;
        }
    }

    // "Establish Route" button
    ButtonData establishBtn;
    establishBtn.label = "Establish Route";
    establishBtn.fontSize = 12.0f;
    establishBtn.normalColor  = {0.15f, 0.35f, 0.15f, 0.9f};
    establishBtn.hoverColor   = {0.20f, 0.50f, 0.20f, 0.9f};
    establishBtn.pressedColor = {0.10f, 0.25f, 0.10f, 0.9f};
    establishBtn.cornerRadius = 4.0f;
    establishBtn.onClick = [this]() {
        if (this->m_trSourcePlayerIdx < 0 || this->m_trSourceCityIdx < 0
            || this->m_trDestPlayerIdx < 0 || this->m_trDestCityIdx < 0) {
            LOG_INFO("Trade route: must select both source and destination cities");
            return;
        }

        const aoc::game::Player* srcPlayer =
            this->m_gameState->player(static_cast<PlayerId>(this->m_trSourcePlayerIdx));
        const aoc::game::Player* dstPlayer =
            this->m_gameState->player(static_cast<PlayerId>(this->m_trDestPlayerIdx));

        if (srcPlayer == nullptr || dstPlayer == nullptr) {
            return;
        }
        if (this->m_trSourceCityIdx >= srcPlayer->cityCount()
            || this->m_trDestCityIdx >= dstPlayer->cityCount()) {
            return;
        }

        const aoc::game::City& srcCity =
            *srcPlayer->cities()[static_cast<std::size_t>(this->m_trSourceCityIdx)];
        const aoc::game::City& dstCity =
            *dstPlayer->cities()[static_cast<std::size_t>(this->m_trDestCityIdx)];

        // Compute path between cities
        std::optional<aoc::map::PathResult> pathResult = aoc::map::findPath(
            *this->m_grid, srcCity.location(), dstCity.location());

        if (!pathResult.has_value()) {
            LOG_INFO("Trade route: no path found between cities");
            return;
        }

        // Create the trade route and add it to global state
        aoc::sim::TradeRouteComponent route{};
        route.sourceCityId   = EntityId{};  // Routes now identified by location, not legacy entity
        route.destCityId     = EntityId{};
        route.sourcePlayer   = srcPlayer->id();
        route.destPlayer     = dstPlayer->id();
        route.path           = pathResult->path;
        route.turnsRemaining = static_cast<int32_t>(pathResult->path.size()) / 5 + 1;

        // Auto-fill cargo with top 3 surplus goods from source city stockpile
        const aoc::sim::CityStockpileComponent& stockpile = srcCity.stockpile();
        std::vector<std::pair<uint16_t, int32_t>> surplusGoods;
        for (const std::pair<const uint16_t, int32_t>& entry : stockpile.goods) {
            if (entry.second > 0) {
                surplusGoods.push_back({entry.first, entry.second});
            }
        }
        std::sort(surplusGoods.begin(), surplusGoods.end(),
                  [](const std::pair<uint16_t, int32_t>& a,
                     const std::pair<uint16_t, int32_t>& b) {
                      return a.second > b.second;
                  });
        const std::size_t cargoCount = (surplusGoods.size() < 3) ? surplusGoods.size() : 3;
        for (std::size_t c = 0; c < cargoCount; ++c) {
            aoc::sim::TradeOffer offer{};
            offer.goodId = surplusGoods[c].first;
            offer.amountPerTurn = surplusGoods[c].second / 2;  // Ship half surplus
            if (offer.amountPerTurn > 0) {
                route.cargo.push_back(std::move(offer));
            }
        }

        this->m_gameState->tradeRoutes().push_back(std::move(route));

        LOG_INFO("Trade route established from %s to %s (%d turns)",
                 srcCity.name().c_str(), dstCity.name().c_str(),
                 this->m_gameState->tradeRoutes().back().turnsRemaining);
    };
    (void)ui.createButton(this->m_tradeRoutePanel, {0.0f, 0.0f, 160.0f, 26.0f},
                     std::move(establishBtn));

    ui.layout();
}

void EconomyScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_infoLabel = INVALID_WIDGET;
    this->m_marketList = INVALID_WIDGET;
    this->m_tradeRoutePanel = INVALID_WIDGET;
    this->m_trSourcePlayerIdx = -1;
    this->m_trSourceCityIdx   = -1;
    this->m_trDestPlayerIdx   = -1;
    this->m_trDestCityIdx     = -1;
}

void EconomyScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }

    const aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    const aoc::sim::MonetaryStateComponent* monetary =
        (owningPlayer != nullptr) ? &owningPlayer->monetary() : nullptr;

    std::string infoText = "No economic data";
    if (monetary != nullptr) {
        infoText = std::string(aoc::sim::monetarySystemName(monetary->system));

        // Coin reserves detail
        if (monetary->system == aoc::sim::MonetarySystemType::CommodityMoney
            || monetary->system == aoc::sim::MonetarySystemType::GoldStandard) {
            infoText += "  Cu:" + std::to_string(monetary->copperCoinReserves)
                      + " Ag:" + std::to_string(monetary->silverCoinReserves)
                      + " Au:" + std::to_string(monetary->goldBarReserves);
        }

        infoText += "  Tier:" + std::string(aoc::sim::coinTierName(monetary->effectiveCoinTier))
                  + "  Treasury:" + std::to_string(monetary->treasury);

        if (monetary->system != aoc::sim::MonetarySystemType::Barter) {
            infoText += "  M:" + std::to_string(monetary->moneySupply);
            int inflPct = static_cast<int>(monetary->inflationRate * 100.0f);
            infoText += "  Infl:" + std::to_string(inflPct) + "%";
        }

        // Debasement warning
        if (monetary->debasement.discoveredByPartners) {
            int debPct = static_cast<int>(monetary->debasement.debasementRatio * 100.0f);
            infoText += "  DEBASED:" + std::to_string(debPct) + "%";
        }

        // Fiat trust info
        if (monetary->system == aoc::sim::MonetarySystemType::FiatMoney) {
            const aoc::sim::CurrencyTrustComponent& trust = owningPlayer->currencyTrust();
            int trustPct = static_cast<int>(trust.trustScore * 100.0f);
            infoText += "  Trust:" + std::to_string(trustPct) + "%";
            if (trust.isReserveCurrency) {
                infoText += " [RESERVE]";
            }
        }
    }
    ui.setLabelText(this->m_infoLabel, std::move(infoText));
}

// ============================================================================
// CityDetailScreen
// ============================================================================

void CityDetailScreen::setContext(aoc::game::GameState* gameState, const aoc::map::HexGrid* grid,
                                   aoc::hex::AxialCoord cityLocation, PlayerId player) {
    this->m_gameState  = gameState;
    this->m_grid       = grid;
    this->m_cityLocation = cityLocation;
    this->m_player     = player;
}

/// Resolve the City object matching m_cityEntity for this screen.
/// Returns nullptr when the city is no longer available.
static aoc::game::City* resolveCityByLocation(aoc::game::GameState* gs,
                                             PlayerId owner,
                                             aoc::hex::AxialCoord cityLocation) {
    if (gs == nullptr) { return nullptr; }
    aoc::game::Player* p = gs->player(owner);
    if (p == nullptr) { return nullptr; }
    for (const std::unique_ptr<aoc::game::City>& c : p->cities()) {
        if (c->location() == cityLocation) {
            return c.get();
        }
    }
    return nullptr;
}

void CityDetailScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    assert(this->m_gameState != nullptr);
    this->m_isOpen = true;

    constexpr float kPanelWidth = 350.0f;
    constexpr float kContentWidth = 330.0f;

    // Right-side panel anchored to the top-right edge of the screen (no full-screen overlay).
    // Height is set to a large value; the anchor system positions X from the right edge.
    const float panelHeight = this->m_screenH;

    this->m_rootPanel = ui.createPanel(
        {0.0f, 0.0f, kPanelWidth, panelHeight},
        PanelData{{0.12f, 0.14f, 0.18f, 0.95f}, 0.0f});
    {
        Widget* rootWidget = ui.getWidget(this->m_rootPanel);
        if (rootWidget != nullptr) {
            rootWidget->anchor = Anchor::TopRight;
            rootWidget->marginRight = 0.0f;
        }
    }

    WidgetId innerPanel = this->m_rootPanel;
    {
        Widget* inner = ui.getWidget(innerPanel);
        if (inner != nullptr) {
            inner->padding = {8.0f, 10.0f, 8.0f, 10.0f};
            inner->childSpacing = 4.0f;
        }
    }

    // Title label at top
    (void)ui.createLabel(innerPanel, {0.0f, 0.0f, kContentWidth, 22.0f},
                   LabelData{"City Detail", {1.0f, 0.9f, 0.5f, 1.0f}, 18.0f});

    // "Close [ESC]" button at top-right corner
    {
        ButtonData closeBtn;
        closeBtn.label = "X";
        closeBtn.fontSize = 13.0f;
        closeBtn.normalColor = {0.3f, 0.15f, 0.15f, 0.9f};
        closeBtn.hoverColor = {0.45f, 0.2f, 0.2f, 0.9f};
        closeBtn.pressedColor = {0.2f, 0.1f, 0.1f, 0.9f};
        closeBtn.cornerRadius = 3.0f;
        closeBtn.onClick = [this, &ui]() {
            this->close(ui);
        };
        (void)ui.createButton(innerPanel,
                        {kContentWidth - 24.0f, 0.0f, 28.0f, 22.0f},
                        std::move(closeBtn));
    }

    const aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);

    if (city == nullptr) {
        this->m_detailLabel = ui.createLabel(
            innerPanel, {0.0f, 0.0f, kContentWidth, 16.0f},
            LabelData{"City not found", {0.8f, 0.4f, 0.4f, 1.0f}, 13.0f});
        ui.layout();
        return;
    }

    // ====================================================================
    // Header bar: city name + population
    // ====================================================================
    {
        WidgetId headerBar = ui.createPanel(
            innerPanel, {0.0f, 0.0f, kContentWidth, 36.0f},
            PanelData{{0.08f, 0.10f, 0.14f, 0.98f}, 4.0f});
        {
            Widget* hdr = ui.getWidget(headerBar);
            if (hdr != nullptr) {
                hdr->layoutDirection = LayoutDirection::Horizontal;
                hdr->padding = {6.0f, 8.0f, 6.0f, 8.0f};
                hdr->childSpacing = 0.0f;
            }
        }

        // Player color accent stripe
        (void)ui.createPanel(
            headerBar, {0.0f, 0.0f, 4.0f, 24.0f},
            PanelData{{0.3f, 0.6f, 0.95f, 1.0f}, 2.0f});

        std::string nameText = "  " + city->name();
        this->m_detailLabel = ui.createLabel(
            headerBar, {0.0f, 4.0f, 200.0f, 18.0f},
            LabelData{std::move(nameText), {1.0f, 0.95f, 0.75f, 1.0f}, 16.0f});

        std::string popText = "Pop " + std::to_string(city->population());
        (void)ui.createLabel(
            headerBar, {0.0f, 5.0f, 100.0f, 16.0f},
            LabelData{std::move(popText), {0.7f, 0.85f, 0.7f, 1.0f}, 13.0f});
    }

    // ====================================================================
    // Tab bar: 4 tab buttons in a horizontal row
    // ====================================================================
    {
        constexpr float kTabWidth = 74.0f;
        constexpr float kTabHeight = 26.0f;
        constexpr float kTabGap = 4.0f;

        WidgetId tabBar = ui.createPanel(
            innerPanel, {0.0f, 0.0f, kContentWidth, kTabHeight + 6.0f},
            PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
        {
            Widget* tb = ui.getWidget(tabBar);
            if (tb != nullptr) {
                tb->layoutDirection = LayoutDirection::Horizontal;
                tb->padding = {2.0f, 2.0f, 2.0f, 2.0f};
                tb->childSpacing = kTabGap;
            }
        }

        constexpr std::array<const char*, TAB_COUNT> kTabNames = {
            "Overview", "Production", "Buildings", "Citizens"
        };
        constexpr Color kActiveTabColor   = {0.25f, 0.35f, 0.55f, 1.0f};
        constexpr Color kInactiveTabColor = {0.15f, 0.17f, 0.22f, 0.9f};

        for (int32_t tabIdx = 0; tabIdx < TAB_COUNT; ++tabIdx) {
            const bool isActive = (tabIdx == this->m_activeTab);
            const Color baseColor = isActive ? kActiveTabColor : kInactiveTabColor;

            ButtonData tabBtn;
            tabBtn.label = kTabNames[static_cast<std::size_t>(tabIdx)];
            tabBtn.fontSize = 11.0f;
            tabBtn.normalColor = baseColor;
            tabBtn.hoverColor = {baseColor.r + 0.08f, baseColor.g + 0.08f,
                                 baseColor.b + 0.08f, 1.0f};
            tabBtn.pressedColor = {baseColor.r - 0.04f, baseColor.g - 0.04f,
                                   baseColor.b - 0.04f, 1.0f};
            tabBtn.cornerRadius = 3.0f;

            CityDetailScreen* self = this;
            UIManager* uiPtr = &ui;
            const int32_t capturedIdx = tabIdx;
            tabBtn.onClick = [self, uiPtr, capturedIdx]() {
                self->switchTab(*uiPtr, capturedIdx);
            };

            this->m_tabButtons[static_cast<std::size_t>(tabIdx)] =
                ui.createButton(tabBar, {0.0f, 0.0f, kTabWidth, kTabHeight}, std::move(tabBtn));
        }
    }

    // ====================================================================
    // Content panel: holds tab content, swapped on tab switch
    // ====================================================================
    // Title(22) + headerBar(36) + tabBar(~32) + padding/spacing ~ 110px overhead
    const float contentHeight = panelHeight - 110.0f;
    this->m_contentPanel = ui.createPanel(
        innerPanel, {0.0f, 0.0f, kContentWidth, contentHeight},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
    {
        Widget* cp = ui.getWidget(this->m_contentPanel);
        if (cp != nullptr) {
            cp->padding = {0.0f, 0.0f, 0.0f, 0.0f};
            cp->childSpacing = 0.0f;
        }
    }

    // Populate with the default tab
    this->m_activeTab = TAB_OVERVIEW;
    this->buildOverviewTab(ui, this->m_contentPanel);

    ui.layout();
}

// ----------------------------------------------------------------------------
// switchTab -- clear content panel and rebuild the selected tab
// ----------------------------------------------------------------------------

void CityDetailScreen::switchTab(UIManager& ui, int32_t tabIndex) {
    if (tabIndex < 0 || tabIndex >= TAB_COUNT) {
        return;
    }

    this->m_activeTab = tabIndex;

    // Remove all children of the content panel
    {
        Widget* cp = ui.getWidget(this->m_contentPanel);
        if (cp != nullptr) {
            // Copy the children vector because removeWidget mutates it
            const std::vector<WidgetId> childrenCopy = cp->children;
            for (WidgetId child : childrenCopy) {
                ui.removeWidget(child);
            }
        }
    }

    // Build the selected tab's content
    switch (tabIndex) {
        case TAB_OVERVIEW:   this->buildOverviewTab(ui, this->m_contentPanel);   break;
        case TAB_PRODUCTION: this->buildProductionTab(ui, this->m_contentPanel); break;
        case TAB_BUILDINGS:  this->buildBuildingsTab(ui, this->m_contentPanel);  break;
        case TAB_CITIZENS:   this->buildCitizensTab(ui, this->m_contentPanel);   break;
        default: break;
    }

    // Update tab button colors to reflect active tab
    this->updateTabButtonColors(ui);

    ui.layout();
}

// ----------------------------------------------------------------------------
// updateTabButtonColors -- highlight the active tab, dim the rest
// ----------------------------------------------------------------------------

void CityDetailScreen::updateTabButtonColors(UIManager& ui) {
    constexpr Color kActiveTabColor   = {0.25f, 0.35f, 0.55f, 1.0f};
    constexpr Color kInactiveTabColor = {0.15f, 0.17f, 0.22f, 0.9f};

    for (int32_t tabIdx = 0; tabIdx < TAB_COUNT; ++tabIdx) {
        Widget* btnWidget = ui.getWidget(this->m_tabButtons[static_cast<std::size_t>(tabIdx)]);
        if (btnWidget == nullptr) {
            continue;
        }
        ButtonData* btnData = std::get_if<ButtonData>(&btnWidget->data);
        if (btnData == nullptr) {
            continue;
        }

        const bool isActive = (tabIdx == this->m_activeTab);
        const Color baseColor = isActive ? kActiveTabColor : kInactiveTabColor;
        btnData->normalColor  = baseColor;
        btnData->hoverColor   = {baseColor.r + 0.08f, baseColor.g + 0.08f,
                                 baseColor.b + 0.08f, 1.0f};
        btnData->pressedColor = {baseColor.r - 0.04f, baseColor.g - 0.04f,
                                 baseColor.b - 0.04f, 1.0f};
    }
}

// ----------------------------------------------------------------------------
// buildOverviewTab -- summary: yields, loyalty, happiness, current production
// ----------------------------------------------------------------------------

void CityDetailScreen::buildOverviewTab(UIManager& ui, WidgetId contentPanel) {
    constexpr float kListWidth = 310.0f;
    constexpr float kContentWidth = 330.0f;
    constexpr Color kHeaderBg = {0.18f, 0.20f, 0.28f, 0.95f};
    constexpr Color kHeaderTextColor = {0.9f, 0.85f, 0.6f, 1.0f};
    constexpr Color kDimTextColor = {0.55f, 0.55f, 0.60f, 0.8f};
    constexpr Color kBodyTextColor = {0.78f, 0.80f, 0.82f, 1.0f};
    constexpr Color kSeparatorColor = {0.22f, 0.24f, 0.30f, 0.6f};
    constexpr Color kFoodColor = {0.3f, 0.8f, 0.3f, 1.0f};
    constexpr Color kProdColor = {0.9f, 0.55f, 0.2f, 1.0f};
    constexpr Color kGoldColor = {0.95f, 0.85f, 0.2f, 1.0f};
    constexpr Color kSciColor = {0.3f, 0.55f, 0.95f, 1.0f};
    constexpr Color kCultColor = {0.7f, 0.35f, 0.85f, 1.0f};

    const aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr) {
        (void)ui.createLabel(contentPanel, {0.0f, 0.0f, kContentWidth, 16.0f},
            LabelData{"City not found", {0.8f, 0.4f, 0.4f, 1.0f}, 13.0f});
        return;
    }

    // Scrollable area for tab content
    WidgetId scrollArea = ui.createScrollList(
        contentPanel, {0.0f, 0.0f, kContentWidth, 520.0f},
        ScrollListData{{0.12f, 0.14f, 0.18f, 0.0f}, 0.0f, 0.0f});
    {
        Widget* listWidget = ui.getWidget(scrollArea);
        if (listWidget != nullptr) {
            listWidget->padding = {4.0f, 6.0f, 4.0f, 6.0f};
            listWidget->childSpacing = 3.0f;
        }
    }

    // -- Yield summary row --
    int32_t totalFood = 0;
    int32_t totalProd = 0;
    int32_t totalGold = 0;
    int32_t totalSci  = 0;
    int32_t totalCult = 0;

    for (const hex::AxialCoord& tile : city->workedTiles()) {
        if (!this->m_grid->isValid(tile)) {
            continue;
        }
        const int32_t tileIndex = this->m_grid->toIndex(tile);
        const aoc::map::TileYield ty = this->m_grid->tileYield(tileIndex);
        totalFood += ty.food;
        totalProd += ty.production;
        totalGold += ty.gold;
        totalSci  += ty.science;
        totalCult += ty.culture;
    }

    // Building bonuses for yield totals
    int32_t bldgProd = 0;
    int32_t bldgSci  = 0;
    int32_t bldgGold = 0;
    const aoc::sim::CityDistrictsComponent& districts = city->districts();
    for (const aoc::sim::CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
        for (BuildingId bid : district.buildings) {
            const aoc::sim::BuildingDef& bdef = aoc::sim::buildingDef(bid);
            bldgProd += bdef.productionBonus;
            bldgSci  += bdef.scienceBonus;
            bldgGold += bdef.goldBonus;
        }
    }

    {
        WidgetId yieldRow = ui.createPanel(
            scrollArea, {0.0f, 0.0f, kListWidth, 32.0f},
            PanelData{{0.15f, 0.17f, 0.22f, 0.95f}, 4.0f});
        {
            Widget* yr = ui.getWidget(yieldRow);
            if (yr != nullptr) {
                yr->layoutDirection = LayoutDirection::Horizontal;
                yr->padding = {4.0f, 6.0f, 4.0f, 6.0f};
                yr->childSpacing = 4.0f;
            }
        }

        struct YieldEntry {
            const char* name;
            int32_t value;
            Color dotColor;
        };
        const std::array<YieldEntry, 5> yieldEntries = {{
            {"Food",    totalFood,              kFoodColor},
            {"Prod",    totalProd + bldgProd,    kProdColor},
            {"Gold",    totalGold + bldgGold,    kGoldColor},
            {"Sci",     totalSci + bldgSci,      kSciColor},
            {"Cult",    totalCult,               kCultColor},
        }};

        for (const YieldEntry& entry : yieldEntries) {
            WidgetId entryPanel = ui.createPanel(
                yieldRow, {0.0f, 0.0f, 90.0f, 22.0f},
                PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
            {
                Widget* ep = ui.getWidget(entryPanel);
                if (ep != nullptr) {
                    ep->layoutDirection = LayoutDirection::Horizontal;
                    ep->padding = {2.0f, 4.0f, 2.0f, 4.0f};
                    ep->childSpacing = 4.0f;
                }
            }
            (void)ui.createPanel(
                entryPanel, {0.0f, 2.0f, 10.0f, 10.0f},
                PanelData{entry.dotColor, 5.0f});
            std::string valueText = std::to_string(entry.value) + " " + entry.name;
            (void)ui.createLabel(
                entryPanel, {0.0f, 0.0f, 64.0f, 14.0f},
                LabelData{std::move(valueText), {0.92f, 0.92f, 0.92f, 1.0f}, 11.0f});
        }
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{kSeparatorColor, 0.0f});

    // -- Loyalty section --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Loyalty", kHeaderTextColor, 11.0f});

    {
        const aoc::sim::CityLoyaltyComponent& loyaltyComp = city->loyalty();
        const aoc::sim::LoyaltyStatus loyaltyStatus = loyaltyComp.status();
        const char* statusName = aoc::sim::loyaltyStatusName(loyaltyStatus);

        Color loyaltyTextColor = {1.0f, 1.0f, 1.0f, 1.0f};
        Color loyaltyBarColor = {0.3f, 0.9f, 0.3f, 0.9f};
        switch (loyaltyStatus) {
            case aoc::sim::LoyaltyStatus::Loyal:
                loyaltyTextColor = {0.3f, 0.9f, 0.3f, 1.0f};
                loyaltyBarColor = {0.2f, 0.7f, 0.2f, 0.9f};
                break;
            case aoc::sim::LoyaltyStatus::Content:
                loyaltyTextColor = {0.85f, 0.85f, 0.85f, 1.0f};
                loyaltyBarColor = {0.5f, 0.7f, 0.3f, 0.9f};
                break;
            case aoc::sim::LoyaltyStatus::Disloyal:
                loyaltyTextColor = {0.9f, 0.9f, 0.2f, 1.0f};
                loyaltyBarColor = {0.8f, 0.8f, 0.15f, 0.9f};
                break;
            case aoc::sim::LoyaltyStatus::Unrest:
                loyaltyTextColor = {0.9f, 0.4f, 0.2f, 1.0f};
                loyaltyBarColor = {0.85f, 0.35f, 0.15f, 0.9f};
                break;
            case aoc::sim::LoyaltyStatus::Revolt:
                loyaltyTextColor = {0.9f, 0.1f, 0.1f, 1.0f};
                loyaltyBarColor = {0.8f, 0.1f, 0.1f, 0.9f};
                break;
        }

        char loyaltyBuf[128];
        const char* signStr = (loyaltyComp.loyaltyPerTurn >= 0.0f) ? "+" : "";
        std::snprintf(loyaltyBuf, sizeof(loyaltyBuf),
            "  %.0f / 100  %s    %s%.1f per turn",
            static_cast<double>(loyaltyComp.loyalty), statusName,
            signStr, static_cast<double>(loyaltyComp.loyaltyPerTurn));
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{std::string(loyaltyBuf), loyaltyTextColor, 11.0f});

        constexpr float kBarWidth = 470.0f;
        constexpr float kBarHeight = 10.0f;
        WidgetId barBg = ui.createPanel(
            scrollArea, {0.0f, 0.0f, kBarWidth, kBarHeight},
            PanelData{{0.08f, 0.08f, 0.10f, 0.9f}, 3.0f});
        {
            const float fillFraction = std::clamp(loyaltyComp.loyalty / 100.0f, 0.0f, 1.0f);
            const float fillWidth = std::max(fillFraction * kBarWidth, 2.0f);
            (void)ui.createPanel(
                barBg, {0.0f, 0.0f, fillWidth, kBarHeight},
                PanelData{loyaltyBarColor, 3.0f});
        }

        char factorBuf[128];
        std::snprintf(factorBuf, sizeof(factorBuf),
            "  Base: +%.0f   Own pressure: +%.1f   Foreign: %.1f",
            static_cast<double>(loyaltyComp.baseLoyalty),
            static_cast<double>(loyaltyComp.ownCityPressure),
            static_cast<double>(loyaltyComp.foreignCityPressure));
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 12.0f},
            LabelData{std::string(factorBuf), kBodyTextColor, 9.0f});

        std::string bonusLine = "  ";
        if (loyaltyComp.governorBonus > 0.0f) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "Governor: +%.0f  ", static_cast<double>(loyaltyComp.governorBonus));
            bonusLine += buf;
        }
        if (loyaltyComp.garrisonBonus > 0.0f) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "Garrison: +%.0f  ", static_cast<double>(loyaltyComp.garrisonBonus));
            bonusLine += buf;
        }
        if (loyaltyComp.monumentBonus > 0.0f) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "Monument: +%.0f  ", static_cast<double>(loyaltyComp.monumentBonus));
            bonusLine += buf;
        }
        if (loyaltyComp.ageEffect != 0.0f) {
            char buf[48];
            const char* ageSign = (loyaltyComp.ageEffect >= 0.0f) ? "+" : "";
            std::snprintf(buf, sizeof(buf), "Age: %s%.0f  ", ageSign, static_cast<double>(loyaltyComp.ageEffect));
            bonusLine += buf;
        }
        if (loyaltyComp.happinessEffect != 0.0f) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "Happiness: %.0f  ", static_cast<double>(loyaltyComp.happinessEffect));
            bonusLine += buf;
        }
        if (loyaltyComp.capturedPenalty != 0.0f) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "Captured: %.0f", static_cast<double>(loyaltyComp.capturedPenalty));
            bonusLine += buf;
        }
        if (bonusLine.size() > 2) {
            (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 12.0f},
                LabelData{std::move(bonusLine), kBodyTextColor, 9.0f});
        }
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{kSeparatorColor, 0.0f});

    // -- Happiness section --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Happiness", kHeaderTextColor, 11.0f});

    {
        const aoc::sim::CityHappinessComponent& happiness = city->happiness();
        const int32_t netHappy = static_cast<int32_t>(happiness.amenities - happiness.demand + happiness.modifiers);

        Color happyIndicatorColor = {0.3f, 0.8f, 0.3f, 1.0f};
        if (netHappy < 0) {
            happyIndicatorColor = {0.85f, 0.3f, 0.3f, 1.0f};
        } else if (netHappy == 0) {
            happyIndicatorColor = {0.8f, 0.8f, 0.3f, 1.0f};
        }

        WidgetId happyRow = ui.createPanel(
            scrollArea, {0.0f, 0.0f, kListWidth, 18.0f},
            PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
        {
            Widget* hr = ui.getWidget(happyRow);
            if (hr != nullptr) {
                hr->layoutDirection = LayoutDirection::Horizontal;
                hr->childSpacing = 6.0f;
                hr->padding = {2.0f, 4.0f, 2.0f, 4.0f};
            }
        }

        (void)ui.createPanel(
            happyRow, {0.0f, 1.0f, 12.0f, 12.0f},
            PanelData{happyIndicatorColor, 6.0f});

        char happyBuf[128];
        std::snprintf(happyBuf, sizeof(happyBuf),
            "%d amenities - %d demand + %d modifiers = %d net",
            static_cast<int>(happiness.amenities),
            static_cast<int>(happiness.demand),
            static_cast<int>(happiness.modifiers),
            netHappy);
        (void)ui.createLabel(
            happyRow, {0.0f, 0.0f, 420.0f, 14.0f},
            LabelData{std::string(happyBuf), kBodyTextColor, 11.0f});
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{kSeparatorColor, 0.0f});

    // -- Current production with progress bar --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Current Production", kHeaderTextColor, 11.0f});

    {
        std::string prodName = "Nothing";
        float prodProgress = 0.0f;
        float prodTotal = 1.0f;
        const aoc::sim::ProductionQueueItem* current = city->production().currentItem();
        if (current != nullptr) {
            prodName    = current->name;
            prodProgress = current->progress;
            prodTotal   = current->totalCost;
        }

        char prodBuf[128];
        std::snprintf(prodBuf, sizeof(prodBuf), "  %s   (%d / %d)",
            prodName.c_str(),
            static_cast<int>(prodProgress),
            static_cast<int>(prodTotal));
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{std::string(prodBuf), kBodyTextColor, 11.0f});

        constexpr float kProdBarWidth = 470.0f;
        constexpr float kProdBarHeight = 10.0f;
        WidgetId prodBarBg = ui.createPanel(
            scrollArea, {0.0f, 0.0f, kProdBarWidth, kProdBarHeight},
            PanelData{{0.08f, 0.08f, 0.10f, 0.9f}, 3.0f});
        {
            const float fraction = (prodTotal > 0.0f)
                ? std::clamp(prodProgress / prodTotal, 0.0f, 1.0f)
                : 0.0f;
            const float fillWidth = std::max(fraction * kProdBarWidth, 2.0f);
            (void)ui.createPanel(
                prodBarBg, {0.0f, 0.0f, fillWidth, kProdBarHeight},
                PanelData{kProdColor, 3.0f});
        }
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{kSeparatorColor, 0.0f});

    // -- Purchase hint --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{{0.3f, 0.3f, 0.4f, 0.3f}, 0.0f});
    (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
        LabelData{"  Right-click unowned tiles on the map to purchase them", kDimTextColor, 10.0f});
}

// ----------------------------------------------------------------------------
// buildProductionTab -- queue management and buildable item list
// ----------------------------------------------------------------------------

void CityDetailScreen::buildProductionTab(UIManager& ui, WidgetId contentPanel) {
    constexpr float kListWidth = 310.0f;
    constexpr float kContentWidth = 330.0f;
    constexpr Color kHeaderBg = {0.18f, 0.20f, 0.28f, 0.95f};
    constexpr Color kHeaderTextColor = {0.9f, 0.85f, 0.6f, 1.0f};
    constexpr Color kDimTextColor = {0.55f, 0.55f, 0.60f, 0.8f};
    constexpr Color kBodyTextColor = {0.78f, 0.80f, 0.82f, 1.0f};
    constexpr Color kSeparatorColor = {0.22f, 0.24f, 0.30f, 0.6f};
    constexpr Color kProdColor = {0.9f, 0.55f, 0.2f, 1.0f};

    aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr) {
        (void)ui.createLabel(contentPanel, {0.0f, 0.0f, kContentWidth, 16.0f},
            LabelData{"City not found", {0.8f, 0.4f, 0.4f, 1.0f}, 13.0f});
        return;
    }

    WidgetId scrollArea = ui.createScrollList(
        contentPanel, {0.0f, 0.0f, kContentWidth, 520.0f},
        ScrollListData{{0.12f, 0.14f, 0.18f, 0.0f}, 0.0f, 0.0f});
    {
        Widget* listWidget = ui.getWidget(scrollArea);
        if (listWidget != nullptr) {
            listWidget->padding = {4.0f, 6.0f, 4.0f, 6.0f};
            listWidget->childSpacing = 3.0f;
        }
    }

    // -- Current production queue --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Production Queue", kHeaderTextColor, 11.0f});

    const aoc::sim::ProductionQueueComponent& queue = city->production();
    if (queue.queue.empty()) {
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{"  (empty)", kDimTextColor, 11.0f});
    } else {
        for (const aoc::sim::ProductionQueueItem& item : queue.queue) {
            char itemBuf[128];
            std::snprintf(itemBuf, sizeof(itemBuf), "  %s   (%d / %d)",
                item.name.c_str(),
                static_cast<int>(item.progress),
                static_cast<int>(item.totalCost));
            (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
                LabelData{std::string(itemBuf), kBodyTextColor, 11.0f});

            // Progress bar per queue item
            constexpr float kProdBarWidth = 470.0f;
            constexpr float kProdBarHeight = 8.0f;
            WidgetId barBg = ui.createPanel(
                scrollArea, {0.0f, 0.0f, kProdBarWidth, kProdBarHeight},
                PanelData{{0.08f, 0.08f, 0.10f, 0.9f}, 3.0f});
            {
                const float fraction = (item.totalCost > 0.0f)
                    ? std::clamp(item.progress / item.totalCost, 0.0f, 1.0f)
                    : 0.0f;
                const float fillWidth = std::max(fraction * kProdBarWidth, 2.0f);
                (void)ui.createPanel(
                    barBg, {0.0f, 0.0f, fillWidth, kProdBarHeight},
                    PanelData{kProdColor, 3.0f});
            }
        }
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{kSeparatorColor, 0.0f});

    // -- Buildable items --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Available Items", kHeaderTextColor, 11.0f});

    const std::vector<aoc::sim::BuildableItem> buildableItems =
        aoc::sim::getBuildableItems(*this->m_gameState, this->m_player, *resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation));

    if (buildableItems.empty()) {
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{"  Nothing available to build", kDimTextColor, 11.0f});
    }

    for (const aoc::sim::BuildableItem& buildable : buildableItems) {
        std::string itemLabel = std::string(buildable.name) + " ("
                              + std::to_string(static_cast<int>(buildable.cost)) + ")";
        if (buildable.type == aoc::sim::ProductionItemType::Wonder) {
            itemLabel += " [Wonder]";
        }

        ButtonData btn;
        btn.label = std::move(itemLabel);
        btn.fontSize = 10.0f;
        btn.cornerRadius = 3.0f;

        switch (buildable.type) {
            case aoc::sim::ProductionItemType::Unit:
                btn.normalColor  = {0.2f, 0.2f, 0.28f, 0.9f};
                btn.hoverColor   = {0.3f, 0.3f, 0.38f, 0.9f};
                btn.pressedColor = {0.15f, 0.15f, 0.2f, 0.9f};
                break;
            case aoc::sim::ProductionItemType::Building:
                btn.normalColor  = {0.2f, 0.25f, 0.2f, 0.9f};
                btn.hoverColor   = {0.3f, 0.35f, 0.3f, 0.9f};
                btn.pressedColor = {0.15f, 0.18f, 0.15f, 0.9f};
                break;
            case aoc::sim::ProductionItemType::Wonder:
                btn.normalColor  = {0.28f, 0.22f, 0.15f, 0.9f};
                btn.hoverColor   = {0.40f, 0.32f, 0.20f, 0.9f};
                btn.pressedColor = {0.20f, 0.15f, 0.10f, 0.9f};
                break;
            case aoc::sim::ProductionItemType::District:
                btn.normalColor  = {0.2f, 0.2f, 0.25f, 0.9f};
                btn.hoverColor   = {0.3f, 0.3f, 0.35f, 0.9f};
                btn.pressedColor = {0.15f, 0.15f, 0.18f, 0.9f};
                break;
        }

        const aoc::sim::ProductionItemType itemType = buildable.type;
        const uint16_t itemId = buildable.id;
        const float itemCost = buildable.cost;
        const std::string itemName(buildable.name);
        aoc::game::City* cityPtr = city;
        btn.onClick = [cityPtr, itemType, itemId, itemCost, itemName]() {
            if (cityPtr == nullptr) { return; }
            aoc::sim::ProductionQueueItem item{};
            item.type      = itemType;
            item.itemId    = itemId;
            item.name      = itemName;
            item.totalCost = itemCost;
            item.progress  = 0.0f;
            cityPtr->production().queue.push_back(std::move(item));
            LOG_INFO("Enqueued: %s", itemName.c_str());
        };

        (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 22.0f}, std::move(btn));
    }
}

// ----------------------------------------------------------------------------
// buildBuildingsTab -- districts and their buildings with bonuses
// ----------------------------------------------------------------------------

void CityDetailScreen::buildBuildingsTab(UIManager& ui, WidgetId contentPanel) {
    constexpr float kListWidth = 310.0f;
    constexpr float kContentWidth = 330.0f;
    constexpr Color kHeaderBg = {0.18f, 0.20f, 0.28f, 0.95f};
    constexpr Color kHeaderTextColor = {0.9f, 0.85f, 0.6f, 1.0f};
    constexpr Color kDimTextColor = {0.55f, 0.55f, 0.60f, 0.8f};

    const aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr) {
        (void)ui.createLabel(contentPanel, {0.0f, 0.0f, kContentWidth, 16.0f},
            LabelData{"City not found", {0.8f, 0.4f, 0.4f, 1.0f}, 13.0f});
        return;
    }

    WidgetId scrollArea = ui.createScrollList(
        contentPanel, {0.0f, 0.0f, kContentWidth, 520.0f},
        ScrollListData{{0.12f, 0.14f, 0.18f, 0.0f}, 0.0f, 0.0f});
    {
        Widget* listWidget = ui.getWidget(scrollArea);
        if (listWidget != nullptr) {
            listWidget->padding = {4.0f, 6.0f, 4.0f, 6.0f};
            listWidget->childSpacing = 3.0f;
        }
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Districts & Buildings", kHeaderTextColor, 11.0f});

    const aoc::sim::CityDistrictsComponent& districts = city->districts();

    if (!districts.districts.empty()) {
        constexpr std::array<Color, 4> kDistrictAccents = {{
            {0.20f, 0.24f, 0.38f, 0.9f},
            {0.26f, 0.20f, 0.35f, 0.9f},
            {0.20f, 0.30f, 0.24f, 0.9f},
            {0.30f, 0.24f, 0.20f, 0.9f},
        }};

        int32_t bldgProd = 0;
        int32_t bldgSci  = 0;
        int32_t bldgGold = 0;
        std::size_t districtIdx = 0;

        for (const aoc::sim::CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
            const Color accentColor = kDistrictAccents[districtIdx % kDistrictAccents.size()];
            ++districtIdx;

            (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 18.0f},
                PanelData{accentColor, 2.0f});
            const std::string districtName = std::string(aoc::sim::districtTypeName(district.type));
            (void)ui.createLabel(scrollArea, {0.0f, -17.0f, kListWidth, 14.0f},
                LabelData{"   " + districtName, {0.9f, 0.9f, 0.95f, 1.0f}, 10.0f});

            for (BuildingId bid : district.buildings) {
                const aoc::sim::BuildingDef& bdef = aoc::sim::buildingDef(bid);
                bldgProd += bdef.productionBonus;
                bldgSci  += bdef.scienceBonus;
                bldgGold += bdef.goldBonus;

                std::string bldgLine = "      " + std::string(bdef.name);
                bool hasBonus = false;
                if (bdef.productionBonus != 0) {
                    bldgLine += (hasBonus ? ", " : "  ");
                    bldgLine += "+" + std::to_string(bdef.productionBonus) + " prod";
                    hasBonus = true;
                }
                if (bdef.scienceBonus != 0) {
                    bldgLine += (hasBonus ? ", " : "  ");
                    bldgLine += "+" + std::to_string(bdef.scienceBonus) + " sci";
                    hasBonus = true;
                }
                if (bdef.goldBonus != 0) {
                    bldgLine += (hasBonus ? ", " : "  ");
                    bldgLine += "+" + std::to_string(bdef.goldBonus) + " gold";
                    hasBonus = true;
                }
                (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 13.0f},
                    LabelData{std::move(bldgLine), {0.72f, 0.76f, 0.82f, 1.0f}, 10.0f});
            }

            if (district.buildings.empty()) {
                (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 13.0f},
                    LabelData{"      (no buildings)", kDimTextColor, 10.0f});
            }
        }

        // Building totals summary at the bottom
        char bldgTotalBuf[128];
        std::snprintf(bldgTotalBuf, sizeof(bldgTotalBuf),
            "  Building totals: +%d prod, +%d sci, +%d gold",
            bldgProd, bldgSci, bldgGold);
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{std::string(bldgTotalBuf), {0.65f, 0.7f, 0.8f, 1.0f}, 10.0f});
    } else {
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{"  No districts built", kDimTextColor, 10.0f});
    }
}

// ----------------------------------------------------------------------------
// buildCitizensTab -- worked tiles, population, food surplus
// ----------------------------------------------------------------------------

void CityDetailScreen::buildCitizensTab(UIManager& ui, WidgetId contentPanel) {
    constexpr float kListWidth = 310.0f;
    constexpr float kContentWidth = 330.0f;
    constexpr Color kHeaderBg = {0.18f, 0.20f, 0.28f, 0.95f};
    constexpr Color kHeaderTextColor = {0.9f, 0.85f, 0.6f, 1.0f};
    constexpr Color kBodyTextColor = {0.78f, 0.80f, 0.82f, 1.0f};
    constexpr Color kSeparatorColor = {0.22f, 0.24f, 0.30f, 0.6f};

    aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr) {
        (void)ui.createLabel(contentPanel, {0.0f, 0.0f, kContentWidth, 16.0f},
            LabelData{"City not found", {0.8f, 0.4f, 0.4f, 1.0f}, 13.0f});
        return;
    }

    WidgetId scrollArea = ui.createScrollList(
        contentPanel, {0.0f, 0.0f, kContentWidth, 520.0f},
        ScrollListData{{0.12f, 0.14f, 0.18f, 0.0f}, 0.0f, 0.0f});
    {
        Widget* listWidget = ui.getWidget(scrollArea);
        if (listWidget != nullptr) {
            listWidget->padding = {4.0f, 6.0f, 4.0f, 6.0f};
            listWidget->childSpacing = 3.0f;
        }
    }

    // -- Population and growth info --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Population & Growth", kHeaderTextColor, 11.0f});

    {
        char popBuf[128];
        std::snprintf(popBuf, sizeof(popBuf),
            "  Population: %d   |   Worked tiles: %d   |   Food surplus: %.1f",
            city->population(),
            static_cast<int>(city->workedTiles().size()),
            static_cast<double>(city->foodSurplus()));
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{std::string(popBuf), kBodyTextColor, 11.0f});
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{kSeparatorColor, 0.0f});

    // -- Manage Citizens: tile assignment --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Manage Citizens", kHeaderTextColor, 11.0f});

    {
        aoc::game::Player* playerMut = this->m_gameState->player(this->m_player);

        std::vector<hex::AxialCoord> borderTiles;
        hex::spiral(city->location(), aoc::sim::CITY_WORK_RADIUS, std::back_inserter(borderTiles));

        for (const hex::AxialCoord& tile : borderTiles) {
            if (!this->m_grid->isValid(tile)) {
                continue;
            }
            const int32_t tileIdx = this->m_grid->toIndex(tile);
            if (this->m_grid->owner(tileIdx) != this->m_player) {
                continue;
            }
            if (this->m_grid->movementCost(tileIdx) == 0) {
                continue;
            }

            const aoc::map::TileYield ty = this->m_grid->tileYield(tileIdx);

            const bool isWorked = city->isTileWorked(tile);

            // Current managing city for this tile (override or nearest-city fallback).
            std::string mgmtLabel = "[auto]";
            if (playerMut != nullptr) {
                const aoc::hex::AxialCoord* ov = playerMut->tileCityOverride(tileIdx);
                if (ov != nullptr) {
                    aoc::game::City* mgmtCity = playerMut->cityAt(*ov);
                    mgmtLabel = mgmtCity ? mgmtCity->name() : std::string("?");
                }
            }

            char tileBuf[160];
            std::snprintf(tileBuf, sizeof(tileBuf),
                "%s (%d,%d)  F:%d P:%d G:%d  [%s]",
                isWorked ? "[W]" : "[ ]",
                tile.q, tile.r, ty.food, ty.production, ty.gold,
                mgmtLabel.c_str());

            ButtonData citizenBtn;
            citizenBtn.label = std::string(tileBuf);
            citizenBtn.fontSize = 10.0f;
            citizenBtn.normalColor = isWorked
                ? Color{0.12f, 0.28f, 0.14f, 0.9f}
                : Color{0.20f, 0.22f, 0.28f, 0.9f};
            citizenBtn.hoverColor = {citizenBtn.normalColor.r + 0.08f,
                                      citizenBtn.normalColor.g + 0.08f,
                                      citizenBtn.normalColor.b + 0.08f, 0.95f};
            citizenBtn.pressedColor = {citizenBtn.normalColor.r - 0.04f,
                                        citizenBtn.normalColor.g - 0.04f,
                                        citizenBtn.normalColor.b - 0.04f, 0.9f};
            citizenBtn.cornerRadius = 3.0f;

            aoc::game::City* cityPtr = city;
            const hex::AxialCoord toggleTile = tile;
            citizenBtn.onClick = [cityPtr, toggleTile]() {
                if (cityPtr == nullptr) { return; }
                cityPtr->toggleWorker(toggleTile);
                LOG_INFO("Citizen toggled on tile (%d,%d)", toggleTile.q, toggleTile.r);
            };

            (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
                                   std::move(citizenBtn));

            // Reassign-to-city cycle button: nearest -> city0 -> city1 -> ... -> nearest
            ButtonData assignBtn;
            assignBtn.label = std::string("   Reassign (") + mgmtLabel + ")";
            assignBtn.fontSize = 9.0f;
            assignBtn.normalColor = {0.16f, 0.18f, 0.24f, 0.85f};
            assignBtn.hoverColor = {0.24f, 0.26f, 0.34f, 0.9f};
            assignBtn.pressedColor = {0.12f, 0.14f, 0.20f, 0.85f};
            assignBtn.cornerRadius = 3.0f;

            aoc::game::Player* playerCap = playerMut;
            const int32_t captureTileIdx = tileIdx;
            assignBtn.onClick = [playerCap, captureTileIdx]() {
                if (playerCap == nullptr) { return; }
                const auto& cities = playerCap->cities();
                if (cities.empty()) { return; }
                const aoc::hex::AxialCoord* curr = playerCap->tileCityOverride(captureTileIdx);
                int32_t nextIdx = 0;
                if (curr != nullptr) {
                    for (std::size_t i = 0; i < cities.size(); ++i) {
                        if (cities[i] && cities[i]->location() == *curr) {
                            nextIdx = static_cast<int32_t>(i) + 1;
                            break;
                        }
                    }
                }
                if (nextIdx >= static_cast<int32_t>(cities.size())) {
                    playerCap->clearTileCity(captureTileIdx);
                    LOG_INFO("Tile %d -> [auto]", captureTileIdx);
                } else {
                    const aoc::hex::AxialCoord loc = cities[nextIdx]->location();
                    playerCap->setTileCity(captureTileIdx, loc);
                    LOG_INFO("Tile %d -> city at (%d,%d)", captureTileIdx, loc.q, loc.r);
                }
            };

            (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
                                   std::move(assignBtn));
        }
    }
}

void CityDetailScreen::toggleWorkerOnTile(aoc::hex::AxialCoord tile) {
    if (this->m_gameState == nullptr) {
        return;
    }
    aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr) {
        return;
    }

    if (this->m_grid == nullptr || !this->m_grid->isValid(tile)) {
        return;
    }
    const int32_t tileIdx = this->m_grid->toIndex(tile);
    if (this->m_grid->owner(tileIdx) != this->m_player) {
        return;
    }
    if (this->m_grid->distance(city->location(), tile) > 3) {
        return;
    }
    if (this->m_grid->movementCost(tileIdx) == 0) {
        return;
    }

    city->toggleWorker(tile);
    LOG_INFO("Citizen toggled on tile (%d,%d) via map click", tile.q, tile.r);
}

void CityDetailScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_detailLabel = INVALID_WIDGET;
    this->m_contentPanel = INVALID_WIDGET;
    for (WidgetId& tabBtn : this->m_tabButtons) {
        tabBtn = INVALID_WIDGET;
    }
}

void CityDetailScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }

    const aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr) {
        ui.setLabelText(this->m_detailLabel, "City not found");
        return;
    }

    // Update header label
    std::string nameText = "  " + city->name();
    ui.setLabelText(this->m_detailLabel, std::move(nameText));

    // Rebuild current tab content to reflect latest data
    this->switchTab(ui, this->m_activeTab);
}

} // namespace aoc::ui
