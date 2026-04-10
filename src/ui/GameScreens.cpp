/**
 * @file GameScreens.cpp
 * @brief Implementation of modal game screens (production, tech, government, economy, city detail).
 */

#include "aoc/ui/GameScreens.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ecs/World.hpp"
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

void ProductionScreen::setContext(aoc::ecs::World* world, aoc::map::HexGrid* grid,
                                  EntityId cityEntity, PlayerId player) {
    this->m_world = world;
    this->m_grid = grid;
    this->m_cityEntity = cityEntity;
    this->m_player = player;
}

void ProductionScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    assert(this->m_world != nullptr);
    this->m_isOpen = true;

    WidgetId innerPanel = this->createScreenFrame(
        ui, "Production", 450.0f, 500.0f, this->m_screenW, this->m_screenH);

    // Current queue label
    std::string queueText = "Queue: empty";
    if (this->m_world->hasComponent<aoc::sim::ProductionQueueComponent>(this->m_cityEntity)) {
        const aoc::sim::ProductionQueueComponent& queue =
            this->m_world->getComponent<aoc::sim::ProductionQueueComponent>(this->m_cityEntity);
        const aoc::sim::ProductionQueueItem* current = queue.currentItem();
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
        aoc::sim::getBuildableItems(*this->m_world, this->m_player, this->m_cityEntity);

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
        const EntityId cityEnt = this->m_cityEntity;
        aoc::ecs::World* world = this->m_world;
        btn.onClick = [world, cityEnt, itemType, itemId, itemCost, itemName]() {
            if (!world->isAlive(cityEnt)) {
                return;
            }
            aoc::sim::ProductionQueueComponent* queue =
                world->tryGetComponent<aoc::sim::ProductionQueueComponent>(cityEnt);
            if (queue == nullptr) {
                return;
            }
            aoc::sim::ProductionQueueItem item{};
            item.type      = itemType;
            item.itemId    = itemId;
            item.name      = itemName;
            item.totalCost = itemCost;
            item.progress  = 0.0f;
            queue->queue.push_back(std::move(item));
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
    if (!this->m_isOpen || this->m_world == nullptr) {
        return;
    }

    std::string queueText = "Queue: empty";
    if (this->m_world->isAlive(this->m_cityEntity) &&
        this->m_world->hasComponent<aoc::sim::ProductionQueueComponent>(this->m_cityEntity)) {
        const aoc::sim::ProductionQueueComponent& queue =
            this->m_world->getComponent<aoc::sim::ProductionQueueComponent>(this->m_cityEntity);
        const aoc::sim::ProductionQueueItem* current = queue.currentItem();
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

void TechScreen::setContext(aoc::ecs::World* world, PlayerId player) {
    this->m_world = world;
    this->m_player = player;
}

void TechScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    assert(this->m_world != nullptr);
    this->m_isOpen = true;

    WidgetId innerPanel = this->createScreenFrame(
        ui, "Technology", 500.0f, 550.0f, this->m_screenW, this->m_screenH);

    // Find player tech component
    aoc::sim::PlayerTechComponent* playerTech = nullptr;
    this->m_world->forEach<aoc::sim::PlayerTechComponent>(
        [this, &playerTech](EntityId, aoc::sim::PlayerTechComponent& tech) {
            if (tech.owner == this->m_player) {
                playerTech = &tech;
            }
        });

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
            aoc::ecs::World* world = this->m_world;
            const PlayerId player = this->m_player;
            btn.onClick = [world, player, techValue]() {
                world->forEach<aoc::sim::PlayerTechComponent>(
                    [player, techValue](EntityId, aoc::sim::PlayerTechComponent& tc) {
                        if (tc.owner == player) {
                            tc.currentResearch = TechId{techValue};
                            tc.researchProgress = 0.0f;
                            const aoc::sim::TechDef& def = aoc::sim::techDef(TechId{techValue});
                            LOG_INFO("Now researching: %.*s",
                                     static_cast<int>(def.name.size()), def.name.data());
                        }
                    });
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
    if (!this->m_isOpen || this->m_world == nullptr) {
        return;
    }

    aoc::sim::PlayerTechComponent* playerTech = nullptr;
    this->m_world->forEach<aoc::sim::PlayerTechComponent>(
        [this, &playerTech](EntityId, aoc::sim::PlayerTechComponent& tech) {
            if (tech.owner == this->m_player) {
                playerTech = &tech;
            }
        });

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

void GovernmentScreen::setContext(aoc::ecs::World* world, PlayerId player) {
    this->m_world = world;
    this->m_player = player;
}

void GovernmentScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    assert(this->m_world != nullptr);
    this->m_isOpen = true;

    WidgetId innerPanel = this->createScreenFrame(
        ui, "Government", 450.0f, 400.0f, this->m_screenW, this->m_screenH);

    // Find player government component
    aoc::sim::PlayerGovernmentComponent* playerGov = nullptr;
    this->m_world->forEach<aoc::sim::PlayerGovernmentComponent>(
        [this, &playerGov](EntityId, aoc::sim::PlayerGovernmentComponent& gov) {
            if (gov.owner == this->m_player) {
                playerGov = &gov;
            }
        });

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

            aoc::ecs::World* world = this->m_world;
            const PlayerId player = this->m_player;
            btn.onClick = [world, player, govType]() {
                world->forEach<aoc::sim::PlayerGovernmentComponent>(
                    [player, govType](EntityId, aoc::sim::PlayerGovernmentComponent& gov) {
                        if (gov.owner == player) {
                            gov.government = govType;
                            const aoc::sim::GovernmentDef& def = aoc::sim::governmentDef(govType);
                            LOG_INFO("Switched government to: %.*s",
                                     static_cast<int>(def.name.size()), def.name.data());
                        }
                    });
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
    if (!this->m_isOpen || this->m_world == nullptr) {
        return;
    }

    aoc::sim::PlayerGovernmentComponent* playerGov = nullptr;
    this->m_world->forEach<aoc::sim::PlayerGovernmentComponent>(
        [this, &playerGov](EntityId, aoc::sim::PlayerGovernmentComponent& gov) {
            if (gov.owner == this->m_player) {
                playerGov = &gov;
            }
        });

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

void EconomyScreen::setContext(aoc::ecs::World* world, const aoc::map::HexGrid* grid,
                                PlayerId player, const aoc::sim::Market* market) {
    this->m_world = world;
    this->m_grid = grid;
    this->m_player = player;
    this->m_market = market;
}

void EconomyScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    assert(this->m_world != nullptr);
    this->m_isOpen = true;

    WidgetId innerPanel = this->createScreenFrame(
        ui, "Economy", 500.0f, 500.0f, this->m_screenW, this->m_screenH);

    // Find monetary state
    aoc::sim::MonetaryStateComponent* monetary = nullptr;
    this->m_world->forEach<aoc::sim::MonetaryStateComponent>(
        [this, &monetary](EntityId, aoc::sim::MonetaryStateComponent& ms) {
            if (ms.owner == this->m_player) {
                monetary = &ms;
            }
        });

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

        aoc::ecs::World* world = this->m_world;
        const PlayerId player = this->m_player;
        minusBtn.onClick = [world, player]() {
            world->forEach<aoc::sim::MonetaryStateComponent>(
                [player](EntityId, aoc::sim::MonetaryStateComponent& ms) {
                    if (ms.owner == player) {
                        ms.taxRate -= 0.05f;
                        if (ms.taxRate < 0.0f) {
                            ms.taxRate = 0.0f;
                        }
                        LOG_INFO("Tax rate: %d%%", static_cast<int>(ms.taxRate * 100.0f));
                    }
                });
        };

        ButtonData plusBtn;
        plusBtn.label = "Tax +5%";
        plusBtn.fontSize = 11.0f;
        plusBtn.normalColor = {0.2f, 0.3f, 0.2f, 0.9f};
        plusBtn.hoverColor = {0.3f, 0.4f, 0.3f, 0.9f};
        plusBtn.pressedColor = {0.15f, 0.2f, 0.15f, 0.9f};
        plusBtn.cornerRadius = 3.0f;

        plusBtn.onClick = [world, player]() {
            world->forEach<aoc::sim::MonetaryStateComponent>(
                [player](EntityId, aoc::sim::MonetaryStateComponent& ms) {
                    if (ms.owner == player) {
                        ms.taxRate += 0.05f;
                        if (ms.taxRate > 1.0f) {
                            ms.taxRate = 1.0f;
                        }
                        LOG_INFO("Tax rate: %d%%", static_cast<int>(ms.taxRate * 100.0f));
                    }
                });
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
    aoc::sim::PlayerEconomyComponent* playerEcon = nullptr;
    this->m_world->forEach<aoc::sim::PlayerEconomyComponent>(
        [this, &playerEcon](EntityId, aoc::sim::PlayerEconomyComponent& econ) {
            if (econ.owner == this->m_player) {
                playerEcon = &econ;
            }
        });

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
                std::unordered_map<uint16_t, int32_t>::iterator it = playerEcon->totalSupply.find(goodId);
                if (it != playerEcon->totalSupply.end()) {
                    info.supply = it->second;
                }
            }
            {
                std::unordered_map<uint16_t, int32_t>::iterator it = playerEcon->totalDemand.find(goodId);
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

    this->m_trSourceCity = NULL_ENTITY;
    this->m_trDestCity = NULL_ENTITY;

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

    const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
        this->m_world->getPool<aoc::sim::CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            const aoc::sim::CityComponent& city = cityPool->data()[i];
            if (city.owner != this->m_player) {
                continue;
            }
            EntityId cityEntity = cityPool->entities()[i];
            ButtonData srcBtn;
            srcBtn.label = city.name;
            srcBtn.fontSize = 10.0f;
            srcBtn.normalColor  = {0.2f, 0.25f, 0.2f, 0.9f};
            srcBtn.hoverColor   = {0.3f, 0.35f, 0.3f, 0.9f};
            srcBtn.pressedColor = {0.15f, 0.18f, 0.15f, 0.9f};
            srcBtn.cornerRadius = 2.0f;
            srcBtn.onClick = [this, cityEntity]() {
                this->m_trSourceCity = cityEntity;
                LOG_INFO("Trade route source city selected");
            };
            (void)ui.createButton(sourceList, {0.0f, 0.0f, 440.0f, 20.0f}, std::move(srcBtn));
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

    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            const aoc::sim::CityComponent& city = cityPool->data()[i];
            if (city.owner == this->m_player) {
                continue;
            }
            EntityId cityEntity = cityPool->entities()[i];
            std::string destLabel = city.name + " (P" + std::to_string(static_cast<unsigned>(city.owner)) + ")";
            ButtonData dstBtn;
            dstBtn.label = std::move(destLabel);
            dstBtn.fontSize = 10.0f;
            dstBtn.normalColor  = {0.25f, 0.2f, 0.2f, 0.9f};
            dstBtn.hoverColor   = {0.35f, 0.3f, 0.3f, 0.9f};
            dstBtn.pressedColor = {0.18f, 0.15f, 0.15f, 0.9f};
            dstBtn.cornerRadius = 2.0f;
            dstBtn.onClick = [this, cityEntity]() {
                this->m_trDestCity = cityEntity;
                LOG_INFO("Trade route destination city selected");
            };
            (void)ui.createButton(destList, {0.0f, 0.0f, 440.0f, 20.0f}, std::move(dstBtn));
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
        if (!this->m_trSourceCity.isValid() || !this->m_trDestCity.isValid()) {
            LOG_INFO("Trade route: must select both source and destination cities");
            return;
        }
        if (!this->m_world->isAlive(this->m_trSourceCity) ||
            !this->m_world->isAlive(this->m_trDestCity)) {
            return;
        }

        const aoc::sim::CityComponent& srcCity =
            this->m_world->getComponent<aoc::sim::CityComponent>(this->m_trSourceCity);
        const aoc::sim::CityComponent& dstCity =
            this->m_world->getComponent<aoc::sim::CityComponent>(this->m_trDestCity);

        // Compute path between cities
        std::optional<aoc::map::PathResult> pathResult = aoc::map::findPath(
            *this->m_grid, srcCity.location, dstCity.location);

        if (!pathResult.has_value()) {
            LOG_INFO("Trade route: no path found between cities");
            return;
        }

        // Create the trade route entity
        EntityId routeEntity = this->m_world->createEntity();
        aoc::sim::TradeRouteComponent route{};
        route.sourceCityId = this->m_trSourceCity;
        route.destCityId = this->m_trDestCity;
        route.sourcePlayer = srcCity.owner;
        route.destPlayer = dstCity.owner;
        route.path = pathResult->path;
        route.turnsRemaining = static_cast<int32_t>(pathResult->path.size()) / 5 + 1;

        // Auto-fill cargo with top 3 surplus goods from source city
        const aoc::sim::CityStockpileComponent* stockpile =
            this->m_world->tryGetComponent<aoc::sim::CityStockpileComponent>(this->m_trSourceCity);
        if (stockpile != nullptr) {
            // Collect goods with positive stockpile amounts
            std::vector<std::pair<uint16_t, int32_t>> surplusGoods;
            for (const std::pair<const uint16_t, int32_t>& entry : stockpile->goods) {
                if (entry.second > 0) {
                    surplusGoods.push_back({entry.first, entry.second});
                }
            }
            // Sort by amount descending
            std::sort(surplusGoods.begin(), surplusGoods.end(),
                      [](const std::pair<uint16_t, int32_t>& a,
                         const std::pair<uint16_t, int32_t>& b) {
                          return a.second > b.second;
                      });
            // Take top 3
            const std::size_t cargoCount = (surplusGoods.size() < 3) ? surplusGoods.size() : 3;
            for (std::size_t c = 0; c < cargoCount; ++c) {
                aoc::sim::TradeOffer offer{};
                offer.goodId = surplusGoods[c].first;
                offer.amountPerTurn = surplusGoods[c].second / 2;  // Ship half surplus
                if (offer.amountPerTurn > 0) {
                    route.cargo.push_back(std::move(offer));
                }
            }
        }

        this->m_world->addComponent<aoc::sim::TradeRouteComponent>(
            routeEntity, std::move(route));

        LOG_INFO("Trade route established from %s to %s (%d turns)",
                 srcCity.name.c_str(), dstCity.name.c_str(), route.turnsRemaining);
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
    this->m_trSourceCity = NULL_ENTITY;
    this->m_trDestCity = NULL_ENTITY;
}

void EconomyScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_world == nullptr) {
        return;
    }

    aoc::sim::MonetaryStateComponent* monetary = nullptr;
    this->m_world->forEach<aoc::sim::MonetaryStateComponent>(
        [this, &monetary](EntityId, aoc::sim::MonetaryStateComponent& ms) {
            if (ms.owner == this->m_player) {
                monetary = &ms;
            }
        });

    std::string infoText = "No economic data";
    if (monetary != nullptr) {
        infoText = std::string(aoc::sim::monetarySystemName(monetary->system));

        // Coin reserves detail
        if (monetary->system == aoc::sim::MonetarySystemType::CommodityMoney
            || monetary->system == aoc::sim::MonetarySystemType::GoldStandard) {
            infoText += "  Cu:" + std::to_string(monetary->copperCoinReserves)
                      + " Ag:" + std::to_string(monetary->silverCoinReserves)
                      + " Au:" + std::to_string(monetary->goldCoinReserves);
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
            const aoc::sim::CurrencyTrustComponent* trust = nullptr;
            this->m_world->forEach<aoc::sim::CurrencyTrustComponent>(
                [this, &trust](EntityId, aoc::sim::CurrencyTrustComponent& ct) {
                    if (ct.owner == this->m_player) {
                        trust = &ct;
                    }
                });
            if (trust != nullptr) {
                int trustPct = static_cast<int>(trust->trustScore * 100.0f);
                infoText += "  Trust:" + std::to_string(trustPct) + "%";
                if (trust->isReserveCurrency) {
                    infoText += " [RESERVE]";
                }
            }
        }
    }
    ui.setLabelText(this->m_infoLabel, std::move(infoText));
}

// ============================================================================
// CityDetailScreen
// ============================================================================

void CityDetailScreen::setContext(aoc::ecs::World* world, const aoc::map::HexGrid* grid,
                                   EntityId cityEntity, PlayerId player) {
    this->m_world = world;
    this->m_grid = grid;
    this->m_cityEntity = cityEntity;
    this->m_player = player;
}

void CityDetailScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    assert(this->m_world != nullptr);
    this->m_isOpen = true;

    WidgetId innerPanel = this->createScreenFrame(
        ui, "City Detail", 500.0f, 640.0f, this->m_screenW, this->m_screenH);

    if (!this->m_world->isAlive(this->m_cityEntity) ||
        !this->m_world->hasComponent<aoc::sim::CityComponent>(this->m_cityEntity)) {
        this->m_detailLabel = ui.createLabel(
            innerPanel, {0.0f, 0.0f, 450.0f, 16.0f},
            LabelData{"City not found", {0.8f, 0.4f, 0.4f, 1.0f}, 13.0f});
        ui.layout();
        return;
    }

    const aoc::sim::CityComponent& city =
        this->m_world->getComponent<aoc::sim::CityComponent>(this->m_cityEntity);

    // City name and population
    std::string nameText = city.name + "  Pop: " + std::to_string(city.population);
    this->m_detailLabel = ui.createLabel(
        innerPanel, {0.0f, 0.0f, 450.0f, 18.0f},
        LabelData{std::move(nameText), {1.0f, 0.95f, 0.7f, 1.0f}, 14.0f});

    // Scrollable detail list for city information
    WidgetId detailList = ui.createScrollList(
        innerPanel, {0.0f, 0.0f, 470.0f, 480.0f});
    {
        Widget* listWidget = ui.getWidget(detailList);
        if (listWidget != nullptr) {
            listWidget->padding = {4.0f, 4.0f, 4.0f, 4.0f};
            listWidget->childSpacing = 2.0f;
        }
    }

    // -- Loyalty --
    {
        const aoc::sim::CityLoyaltyComponent* loyaltyComp =
            this->m_world->tryGetComponent<aoc::sim::CityLoyaltyComponent>(this->m_cityEntity);
        if (loyaltyComp != nullptr) {
            const aoc::sim::LoyaltyStatus loyaltyStatus = loyaltyComp->status();
            const char* statusName = aoc::sim::loyaltyStatusName(loyaltyStatus);

            // Color based on status: green=Loyal, white=Content, yellow=Disloyal, red=Unrest/Revolt
            Color loyaltyColor = {1.0f, 1.0f, 1.0f, 1.0f};
            switch (loyaltyStatus) {
                case aoc::sim::LoyaltyStatus::Loyal:    loyaltyColor = {0.3f, 0.9f, 0.3f, 1.0f}; break;
                case aoc::sim::LoyaltyStatus::Content:  loyaltyColor = {1.0f, 1.0f, 1.0f, 1.0f}; break;
                case aoc::sim::LoyaltyStatus::Disloyal: loyaltyColor = {0.9f, 0.9f, 0.2f, 1.0f}; break;
                case aoc::sim::LoyaltyStatus::Unrest:   loyaltyColor = {0.9f, 0.3f, 0.3f, 1.0f}; break;
                case aoc::sim::LoyaltyStatus::Revolt:   loyaltyColor = {0.9f, 0.1f, 0.1f, 1.0f}; break;
            }

            char loyaltyBuf[128];
            const char* signStr = (loyaltyComp->loyaltyPerTurn >= 0.0f) ? "+" : "";
            std::snprintf(loyaltyBuf, sizeof(loyaltyBuf),
                "Loyalty: %.0f/100 (%s) %s%.1f/turn",
                static_cast<double>(loyaltyComp->loyalty), statusName,
                signStr, static_cast<double>(loyaltyComp->loyaltyPerTurn));
            (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 18.0f},
                LabelData{std::string(loyaltyBuf), loyaltyColor, 12.0f});

            // Loyalty breakdown
            (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 16.0f},
                LabelData{"-- Loyalty Breakdown --", {0.6f, 0.6f, 0.7f, 1.0f}, 11.0f});

            char factorBuf[96];

            std::snprintf(factorBuf, sizeof(factorBuf), "  Base: +%.0f", static_cast<double>(loyaltyComp->baseLoyalty));
            (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 14.0f},
                LabelData{std::string(factorBuf), {0.7f, 0.8f, 0.7f, 1.0f}, 10.0f});

            std::snprintf(factorBuf, sizeof(factorBuf), "  Own city pressure: +%.1f", static_cast<double>(loyaltyComp->ownCityPressure));
            (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 14.0f},
                LabelData{std::string(factorBuf), {0.7f, 0.8f, 0.7f, 1.0f}, 10.0f});

            std::snprintf(factorBuf, sizeof(factorBuf), "  Foreign pressure: %.1f", static_cast<double>(loyaltyComp->foreignCityPressure));
            (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 14.0f},
                LabelData{std::string(factorBuf), {0.8f, 0.6f, 0.6f, 1.0f}, 10.0f});

            if (loyaltyComp->governorBonus > 0.0f) {
                std::snprintf(factorBuf, sizeof(factorBuf), "  Governor: +%.0f", static_cast<double>(loyaltyComp->governorBonus));
                (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 14.0f},
                    LabelData{std::string(factorBuf), {0.7f, 0.8f, 0.7f, 1.0f}, 10.0f});
            }

            if (loyaltyComp->garrisonBonus > 0.0f) {
                std::snprintf(factorBuf, sizeof(factorBuf), "  Garrison: +%.0f", static_cast<double>(loyaltyComp->garrisonBonus));
                (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 14.0f},
                    LabelData{std::string(factorBuf), {0.7f, 0.8f, 0.7f, 1.0f}, 10.0f});
            }

            if (loyaltyComp->monumentBonus > 0.0f) {
                std::snprintf(factorBuf, sizeof(factorBuf), "  Monument: +%.0f", static_cast<double>(loyaltyComp->monumentBonus));
                (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 14.0f},
                    LabelData{std::string(factorBuf), {0.7f, 0.8f, 0.7f, 1.0f}, 10.0f});
            }

            if (loyaltyComp->ageEffect != 0.0f) {
                const char* ageSign = (loyaltyComp->ageEffect >= 0.0f) ? "+" : "";
                std::snprintf(factorBuf, sizeof(factorBuf), "  Age effect: %s%.0f", ageSign, static_cast<double>(loyaltyComp->ageEffect));
                (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 14.0f},
                    LabelData{std::string(factorBuf),
                        (loyaltyComp->ageEffect >= 0.0f)
                            ? Color{0.7f, 0.8f, 0.7f, 1.0f}
                            : Color{0.8f, 0.6f, 0.6f, 1.0f},
                        10.0f});
            }

            if (loyaltyComp->happinessEffect != 0.0f) {
                std::snprintf(factorBuf, sizeof(factorBuf), "  Happiness: %.0f", static_cast<double>(loyaltyComp->happinessEffect));
                (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 14.0f},
                    LabelData{std::string(factorBuf), {0.8f, 0.6f, 0.6f, 1.0f}, 10.0f});
            }

            if (loyaltyComp->capturedPenalty != 0.0f) {
                std::snprintf(factorBuf, sizeof(factorBuf), "  Captured: %.0f", static_cast<double>(loyaltyComp->capturedPenalty));
                (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 14.0f},
                    LabelData{std::string(factorBuf), {0.8f, 0.5f, 0.5f, 1.0f}, 10.0f});
            }
        } else {
            (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 16.0f},
                LabelData{"Loyalty: N/A", {0.5f, 0.5f, 0.5f, 0.7f}, 11.0f});
        }
    }

    // -- Population and growth --
    {
        char growthBuf[96];
        std::snprintf(growthBuf, sizeof(growthBuf),
            "Population: %d  Food surplus: %.1f",
            city.population, static_cast<double>(city.foodSurplus));
        (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 16.0f},
            LabelData{std::string(growthBuf), {0.7f, 0.9f, 0.7f, 1.0f}, 11.0f});
    }

    // -- Per-tile yield contributions --
    (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 16.0f},
        LabelData{"-- Tile Yields --", {0.6f, 0.6f, 0.7f, 1.0f}, 11.0f});

    int32_t totalFood = 0;
    int32_t totalProd = 0;
    int32_t totalGold = 0;
    int32_t totalSci  = 0;
    int32_t totalCult = 0;

    for (const hex::AxialCoord& tile : city.workedTiles) {
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

        const std::string tileText = "Tile (" + std::to_string(tile.q) + ","
                                   + std::to_string(tile.r) + "): F:"
                                   + std::to_string(ty.food) + " P:"
                                   + std::to_string(ty.production) + " G:"
                                   + std::to_string(ty.gold) + " S:"
                                   + std::to_string(ty.science);
        (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 14.0f},
            LabelData{tileText, {0.7f, 0.75f, 0.7f, 1.0f}, 10.0f});
    }

    // -- Districts and buildings --
    (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 16.0f},
        LabelData{"-- Districts & Buildings --", {0.6f, 0.6f, 0.7f, 1.0f}, 11.0f});

    int32_t bldgProd = 0;
    int32_t bldgSci  = 0;
    int32_t bldgGold = 0;
    const aoc::sim::CityDistrictsComponent* districts =
        this->m_world->tryGetComponent<aoc::sim::CityDistrictsComponent>(this->m_cityEntity);
    if (districts != nullptr) {
        for (const aoc::sim::CityDistrictsComponent::PlacedDistrict& district : districts->districts) {
            const std::string districtHeader = std::string(aoc::sim::districtTypeName(district.type));
            (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 14.0f},
                LabelData{districtHeader, {0.8f, 0.8f, 0.9f, 1.0f}, 10.0f});

            for (BuildingId bid : district.buildings) {
                const aoc::sim::BuildingDef& bdef = aoc::sim::buildingDef(bid);
                bldgProd += bdef.productionBonus;
                bldgSci  += bdef.scienceBonus;
                bldgGold += bdef.goldBonus;

                const std::string bldgLine = "  " + std::string(bdef.name)
                    + " (P:+" + std::to_string(bdef.productionBonus)
                    + " S:+" + std::to_string(bdef.scienceBonus)
                    + " G:+" + std::to_string(bdef.goldBonus) + ")";
                (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 14.0f},
                    LabelData{bldgLine, {0.7f, 0.75f, 0.8f, 1.0f}, 10.0f});
            }

            if (district.buildings.empty()) {
                (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 14.0f},
                    LabelData{"  (no buildings)", {0.5f, 0.5f, 0.5f, 0.7f}, 10.0f});
            }
        }
    } else {
        (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 14.0f},
            LabelData{"No districts", {0.5f, 0.5f, 0.5f, 0.7f}, 10.0f});
    }

    const std::string bldgText = "Building totals: +" + std::to_string(bldgProd) + " production, +"
                               + std::to_string(bldgSci) + " science, +"
                               + std::to_string(bldgGold) + " gold";
    (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 14.0f},
        LabelData{bldgText, {0.7f, 0.7f, 0.8f, 1.0f}, 10.0f});

    // -- Total yields row --
    (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 16.0f},
        LabelData{"-- Totals --", {0.6f, 0.6f, 0.7f, 1.0f}, 11.0f});

    const std::string totalText = "Total: F:" + std::to_string(totalFood)
                                + " P:" + std::to_string(totalProd + bldgProd)
                                + " G:" + std::to_string(totalGold + bldgGold)
                                + " S:" + std::to_string(totalSci + bldgSci)
                                + " C:" + std::to_string(totalCult);
    (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 16.0f},
        LabelData{totalText, {0.9f, 0.9f, 0.7f, 1.0f}, 12.0f});

    // -- Happiness breakdown --
    const aoc::sim::CityHappinessComponent* happiness =
        this->m_world->tryGetComponent<aoc::sim::CityHappinessComponent>(this->m_cityEntity);
    if (happiness != nullptr) {
        const int32_t netHappy = static_cast<int32_t>(happiness->amenities - happiness->demand + happiness->modifiers);
        const std::string happyText = "Happiness: "
            + std::to_string(static_cast<int>(happiness->amenities)) + " amenities - "
            + std::to_string(static_cast<int>(happiness->demand)) + " demand = "
            + std::to_string(netHappy);
        (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 16.0f},
            LabelData{happyText, {0.8f, 0.8f, 0.5f, 1.0f}, 11.0f});
    } else {
        (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 16.0f},
            LabelData{"Happiness: N/A", {0.8f, 0.8f, 0.5f, 1.0f}, 11.0f});
    }

    // -- Production queue status --
    std::string queueText = "Building: Nothing";
    if (this->m_world->hasComponent<aoc::sim::ProductionQueueComponent>(this->m_cityEntity)) {
        const aoc::sim::ProductionQueueComponent& queue =
            this->m_world->getComponent<aoc::sim::ProductionQueueComponent>(this->m_cityEntity);
        const aoc::sim::ProductionQueueItem* current = queue.currentItem();
        if (current != nullptr) {
            queueText = "Building: " + current->name + " ("
                      + std::to_string(static_cast<int>(current->progress)) + "/"
                      + std::to_string(static_cast<int>(current->totalCost)) + ")";
        }
    }
    (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 16.0f},
        LabelData{std::move(queueText), {0.8f, 0.9f, 0.8f, 1.0f}, 11.0f});

    // -- Manage Citizens: tile assignment --
    (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 16.0f},
        LabelData{"-- Manage Citizens --", {0.6f, 0.6f, 0.7f, 1.0f}, 11.0f});

    // Show tiles within city borders that citizens can work
    {
        // Gather all owned tiles within 3 hexes of city center
        std::vector<hex::AxialCoord> borderTiles;
        hex::spiral(city.location, 3, std::back_inserter(borderTiles));

        for (const hex::AxialCoord& tile : borderTiles) {
            if (!this->m_grid->isValid(tile)) {
                continue;
            }
            const int32_t tileIdx = this->m_grid->toIndex(tile);
            if (this->m_grid->owner(tileIdx) != this->m_player) {
                continue;
            }
            if (this->m_grid->movementCost(tileIdx) == 0) {
                continue;  // Skip impassable tiles
            }

            const aoc::map::TileYield ty = this->m_grid->tileYield(tileIdx);

            // Check if tile is currently worked
            bool isWorked = false;
            for (const hex::AxialCoord& wt : city.workedTiles) {
                if (wt == tile) {
                    isWorked = true;
                    break;
                }
            }

            const std::string statusMark = isWorked ? "[W]" : "[ ]";
            const std::string tileLine = statusMark + " (" + std::to_string(tile.q) + ","
                                       + std::to_string(tile.r) + ") F:"
                                       + std::to_string(ty.food) + " P:"
                                       + std::to_string(ty.production) + " G:"
                                       + std::to_string(ty.gold);

            ButtonData citizenBtn;
            citizenBtn.label = tileLine;
            citizenBtn.fontSize = 10.0f;
            citizenBtn.normalColor = isWorked
                ? Color{0.15f, 0.30f, 0.15f, 0.9f}
                : Color{0.25f, 0.25f, 0.30f, 0.9f};
            citizenBtn.hoverColor = {citizenBtn.normalColor.r + 0.10f,
                                      citizenBtn.normalColor.g + 0.10f,
                                      citizenBtn.normalColor.b + 0.10f, 0.9f};
            citizenBtn.pressedColor = {citizenBtn.normalColor.r - 0.05f,
                                        citizenBtn.normalColor.g - 0.05f,
                                        citizenBtn.normalColor.b - 0.05f, 0.9f};
            citizenBtn.cornerRadius = 2.0f;

            aoc::ecs::World* world = this->m_world;
            const EntityId cityEnt = this->m_cityEntity;
            const hex::AxialCoord toggleTile = tile;
            citizenBtn.onClick = [world, cityEnt, toggleTile, isWorked]() {
                if (!world->isAlive(cityEnt)) { return; }
                aoc::sim::CityComponent* c =
                    world->tryGetComponent<aoc::sim::CityComponent>(cityEnt);
                if (c == nullptr) { return; }

                if (isWorked) {
                    // Remove from workedTiles (don't remove city center)
                    if (toggleTile == c->location) { return; }
                    for (std::size_t wi = 0; wi < c->workedTiles.size(); ++wi) {
                        if (c->workedTiles[wi] == toggleTile) {
                            c->workedTiles.erase(c->workedTiles.begin() +
                                                  static_cast<std::ptrdiff_t>(wi));
                            LOG_INFO("Citizen unassigned from (%d,%d)",
                                     toggleTile.q, toggleTile.r);
                            break;
                        }
                    }
                } else {
                    // Add to workedTiles if population allows
                    if (static_cast<int32_t>(c->workedTiles.size()) < c->population) {
                        c->workedTiles.push_back(toggleTile);
                        LOG_INFO("Citizen assigned to (%d,%d)",
                                 toggleTile.q, toggleTile.r);
                    } else {
                        LOG_INFO("Cannot assign citizen: population limit (%d)",
                                 c->population);
                    }
                }
            };

            (void)ui.createButton(detailList, {0.0f, 0.0f, 440.0f, 20.0f},
                                   std::move(citizenBtn));
        }
    }

    // -- Buy Tile section --
    (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 16.0f},
        LabelData{"-- Buy Tile --", {0.6f, 0.6f, 0.7f, 1.0f}, 11.0f});

    {
        // Find adjacent unowned tiles
        std::vector<hex::AxialCoord> adjacentTiles;
        // Gather all tiles within city borders + 1 ring
        std::vector<hex::AxialCoord> spiralTiles;
        hex::spiral(city.location, 4, std::back_inserter(spiralTiles));

        for (const hex::AxialCoord& tile : spiralTiles) {
            if (!this->m_grid->isValid(tile)) {
                continue;
            }
            const int32_t tileIdx = this->m_grid->toIndex(tile);
            if (this->m_grid->owner(tileIdx) != INVALID_PLAYER) {
                continue;  // Already owned
            }
            if (this->m_grid->movementCost(tileIdx) == 0) {
                continue;  // Impassable
            }

            // Must be adjacent to an already-owned tile of this player
            bool adjacentToOwned = false;
            const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(tile);
            for (const hex::AxialCoord& nbr : nbrs) {
                if (!this->m_grid->isValid(nbr)) {
                    continue;
                }
                if (this->m_grid->owner(this->m_grid->toIndex(nbr)) == this->m_player) {
                    adjacentToOwned = true;
                    break;
                }
            }
            if (!adjacentToOwned) {
                continue;
            }

            adjacentTiles.push_back(tile);
        }

        for (const hex::AxialCoord& tile : adjacentTiles) {
            const int32_t tileIdx = this->m_grid->toIndex(tile);
            const aoc::map::TileYield ty = this->m_grid->tileYield(tileIdx);
            const int32_t cost = 50 + 25 * city.tilesClaimedCount;

            const std::string buyLine = "(" + std::to_string(tile.q) + ","
                                      + std::to_string(tile.r) + ") F:"
                                      + std::to_string(ty.food) + " P:"
                                      + std::to_string(ty.production) + " G:"
                                      + std::to_string(ty.gold)
                                      + " - Cost: " + std::to_string(cost) + "g";

            ButtonData buyBtn;
            buyBtn.label = buyLine;
            buyBtn.fontSize = 10.0f;
            buyBtn.normalColor = {0.25f, 0.22f, 0.15f, 0.9f};
            buyBtn.hoverColor = {0.35f, 0.32f, 0.25f, 0.9f};
            buyBtn.pressedColor = {0.18f, 0.15f, 0.10f, 0.9f};
            buyBtn.cornerRadius = 2.0f;

            aoc::ecs::World* world = this->m_world;
            aoc::map::HexGrid* gridMut = const_cast<aoc::map::HexGrid*>(this->m_grid);
            const EntityId cityEnt = this->m_cityEntity;
            const PlayerId buyPlayer = this->m_player;
            const hex::AxialCoord buyTile = tile;
            const int32_t buyCost = cost;
            buyBtn.onClick = [world, gridMut, cityEnt, buyPlayer, buyTile, buyCost]() {
                if (!world->isAlive(cityEnt)) { return; }

                // Check treasury
                aoc::sim::PlayerEconomyComponent* econ = nullptr;
                world->forEach<aoc::sim::PlayerEconomyComponent>(
                    [buyPlayer, &econ](EntityId, aoc::sim::PlayerEconomyComponent& ec) {
                        if (ec.owner == buyPlayer) {
                            econ = &ec;
                        }
                    });

                if (econ == nullptr || econ->treasury < static_cast<CurrencyAmount>(buyCost)) {
                    LOG_INFO("Cannot buy tile: insufficient gold");
                    return;
                }

                // Deduct cost and claim tile
                econ->treasury -= static_cast<CurrencyAmount>(buyCost);
                const int32_t buyIdx = gridMut->toIndex(buyTile);
                gridMut->setOwner(buyIdx, buyPlayer);

                aoc::sim::CityComponent* c =
                    world->tryGetComponent<aoc::sim::CityComponent>(cityEnt);
                if (c != nullptr) {
                    ++c->tilesClaimedCount;
                }

                LOG_INFO("Tile (%d,%d) purchased for %d gold",
                         buyTile.q, buyTile.r, buyCost);
            };

            (void)ui.createButton(detailList, {0.0f, 0.0f, 440.0f, 20.0f},
                                   std::move(buyBtn));
        }

        if (adjacentTiles.empty()) {
            (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 14.0f},
                LabelData{"No tiles available to purchase",
                           {0.5f, 0.5f, 0.5f, 0.7f}, 10.0f});
        }
    }

    // Production button hint
    ButtonData prodBtn;
    prodBtn.label = "Open Production [P]";
    prodBtn.fontSize = 12.0f;
    prodBtn.normalColor = {0.2f, 0.25f, 0.35f, 0.9f};
    prodBtn.hoverColor = {0.3f, 0.35f, 0.5f, 0.9f};
    prodBtn.pressedColor = {0.15f, 0.18f, 0.25f, 0.9f};
    prodBtn.cornerRadius = 4.0f;
    prodBtn.onClick = []() {
        LOG_INFO("Press P to open Production screen");
    };

    (void)ui.createButton(innerPanel, {0.0f, 0.0f, 160.0f, 28.0f}, std::move(prodBtn));

    ui.layout();
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
}

void CityDetailScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_world == nullptr) {
        return;
    }

    if (!this->m_world->isAlive(this->m_cityEntity) ||
        !this->m_world->hasComponent<aoc::sim::CityComponent>(this->m_cityEntity)) {
        ui.setLabelText(this->m_detailLabel, "City not found");
        return;
    }

    const aoc::sim::CityComponent& city =
        this->m_world->getComponent<aoc::sim::CityComponent>(this->m_cityEntity);
    std::string nameText = city.name + "  Pop: " + std::to_string(city.population);
    ui.setLabelText(this->m_detailLabel, std::move(nameText));
}

} // namespace aoc::ui
