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
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>
#include <string>

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
                                PlayerId player) {
    this->m_world = world;
    this->m_grid = grid;
    this->m_player = player;
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
                 + "  Gold: " + std::to_string(monetary->goldReserves)
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

    // Market prices section
    (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 470.0f, 14.0f},
                   LabelData{"-- Market Prices --", {0.6f, 0.6f, 0.7f, 1.0f}, 12.0f});

    this->m_marketList = ui.createScrollList(
        innerPanel, {0.0f, 0.0f, 470.0f, 250.0f});

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

    // Show first 20 goods that have price > 0
    uint32_t shownCount = 0;
    const uint16_t totalGoods = aoc::sim::goodCount();
    for (uint16_t goodId = 0; goodId < totalGoods && shownCount < 20; ++goodId) {
        const aoc::sim::GoodDef& gDef = aoc::sim::goodDef(goodId);
        if (gDef.basePrice <= 0) {
            continue;
        }

        int32_t supply = 0;
        int32_t demand = 0;
        if (playerEcon != nullptr) {
            {
                auto it = playerEcon->totalSupply.find(goodId);
                if (it != playerEcon->totalSupply.end()) {
                    supply = it->second;
                }
            }
            {
                auto it = playerEcon->totalDemand.find(goodId);
                if (it != playerEcon->totalDemand.end()) {
                    demand = it->second;
                }
            }
        }

        std::string line = std::string(gDef.name) + ": " + std::to_string(gDef.basePrice)
                         + " (S:" + std::to_string(supply) + " D:" + std::to_string(demand) + ")";

        (void)ui.createLabel(this->m_marketList, {0.0f, 0.0f, 460.0f, 16.0f},
                       LabelData{std::move(line), {0.75f, 0.75f, 0.8f, 1.0f}, 11.0f});
        ++shownCount;
    }

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
        infoText = "System: " + std::string(aoc::sim::monetarySystemName(monetary->system))
                 + "  Gold: " + std::to_string(monetary->goldReserves)
                 + "  Money: " + std::to_string(monetary->moneySupply)
                 + "  Inflation: " + std::to_string(static_cast<int>(monetary->inflationRate * 100.0f)) + "%";
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
        ui, "City Detail", 480.0f, 550.0f, this->m_screenW, this->m_screenH);

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

    // Scrollable detail list for yields breakdown
    WidgetId detailList = ui.createScrollList(
        innerPanel, {0.0f, 0.0f, 450.0f, 380.0f});
    {
        Widget* listWidget = ui.getWidget(detailList);
        if (listWidget != nullptr) {
            listWidget->padding = {4.0f, 4.0f, 4.0f, 4.0f};
            listWidget->childSpacing = 2.0f;
        }
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

    // -- Building bonus summary --
    (void)ui.createLabel(detailList, {0.0f, 0.0f, 440.0f, 16.0f},
        LabelData{"-- Building Bonuses --", {0.6f, 0.6f, 0.7f, 1.0f}, 11.0f});

    int32_t bldgProd = 0;
    int32_t bldgSci  = 0;
    int32_t bldgGold = 0;
    const aoc::sim::CityDistrictsComponent* districts =
        this->m_world->tryGetComponent<aoc::sim::CityDistrictsComponent>(this->m_cityEntity);
    if (districts != nullptr) {
        for (const aoc::sim::CityDistrictsComponent::PlacedDistrict& district : districts->districts) {
            for (BuildingId bid : district.buildings) {
                const aoc::sim::BuildingDef& bdef = aoc::sim::buildingDef(bid);
                bldgProd += bdef.productionBonus;
                bldgSci  += bdef.scienceBonus;
                bldgGold += bdef.goldBonus;
            }
        }
    }

    const std::string bldgText = "Buildings: +" + std::to_string(bldgProd) + " production, +"
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
