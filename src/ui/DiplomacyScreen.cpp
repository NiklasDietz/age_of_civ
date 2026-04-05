/**
 * @file DiplomacyScreen.cpp
 * @brief Diplomacy screen implementation.
 */

#include "aoc/ui/DiplomacyScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/core/Log.hpp"

#include <string>

namespace aoc::ui {

void DiplomacyScreen::setContext(aoc::ecs::World* world, PlayerId humanPlayer,
                                  aoc::sim::DiplomacyManager* diplomacy) {
    this->m_world     = world;
    this->m_player    = humanPlayer;
    this->m_diplomacy = diplomacy;
}

void DiplomacyScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    this->m_isOpen = true;

    WidgetId innerPanel = this->createScreenFrame(
        ui, "Diplomacy", 550.0f, 500.0f, this->m_screenW, this->m_screenH);

    // Player list
    this->m_playerList = ui.createScrollList(
        innerPanel, {0.0f, 0.0f, 520.0f, 400.0f});

    Widget* listWidget = ui.getWidget(this->m_playerList);
    if (listWidget != nullptr) {
        listWidget->padding = {4.0f, 4.0f, 4.0f, 4.0f};
        listWidget->childSpacing = 6.0f;
    }

    // Iterate over known players
    const aoc::ecs::ComponentPool<aoc::sim::PlayerCivilizationComponent>* civPool =
        this->m_world->getPool<aoc::sim::PlayerCivilizationComponent>();
    if (civPool == nullptr || this->m_diplomacy == nullptr) {
        ui.layout();
        return;
    }

    for (uint32_t i = 0; i < civPool->size(); ++i) {
        const aoc::sim::PlayerCivilizationComponent& civ = civPool->data()[i];
        if (civ.owner == this->m_player || civ.owner == BARBARIAN_PLAYER) {
            continue;
        }

        const PlayerId otherId = civ.owner;
        const aoc::sim::CivilizationDef& civDef = aoc::sim::civDef(civ.civId);
        const aoc::sim::PairwiseRelation& rel = this->m_diplomacy->relation(this->m_player, otherId);
        const int32_t score = rel.totalScore();
        const aoc::sim::DiplomaticStance stance = rel.stance();

        // Player info panel
        WidgetId playerPanel = ui.createPanel(
            this->m_playerList, {0.0f, 0.0f, 510.0f, 110.0f},
            PanelData{{0.12f, 0.12f, 0.16f, 0.9f}, 4.0f});
        Widget* ppWidget = ui.getWidget(playerPanel);
        if (ppWidget != nullptr) {
            ppWidget->padding = {6.0f, 8.0f, 6.0f, 8.0f};
            ppWidget->childSpacing = 4.0f;
        }

        // Civ name and leader
        std::string nameText = std::string(civDef.name) + " - " + std::string(civDef.leaderName);
        (void)ui.createLabel(playerPanel, {0.0f, 0.0f, 490.0f, 16.0f},
                              LabelData{std::move(nameText), {1.0f, 0.9f, 0.5f, 1.0f}, 13.0f});

        // Relation and stance info
        std::string relationText = "Score: " + std::to_string(score)
                                 + "  Stance: " + std::string(aoc::sim::stanceName(stance));
        if (rel.isAtWar) {
            relationText += "  [AT WAR]";
        }
        if (rel.hasOpenBorders) {
            relationText += "  [Open Borders]";
        }
        if (rel.hasDefensiveAlliance) {
            relationText += "  [Alliance]";
        }

        Color relationColor = {0.7f, 0.7f, 0.7f, 1.0f};
        if (stance == aoc::sim::DiplomaticStance::Hostile) {
            relationColor = {1.0f, 0.3f, 0.3f, 1.0f};
        } else if (stance == aoc::sim::DiplomaticStance::Friendly ||
                   stance == aoc::sim::DiplomaticStance::Allied) {
            relationColor = {0.3f, 1.0f, 0.3f, 1.0f};
        }
        (void)ui.createLabel(playerPanel, {0.0f, 0.0f, 490.0f, 14.0f},
                              LabelData{std::move(relationText), relationColor, 11.0f});

        // Active modifiers
        if (!rel.modifiers.empty()) {
            std::string modText = "Modifiers: ";
            bool first = true;
            for (const aoc::sim::RelationModifier& mod : rel.modifiers) {
                if (!first) {
                    modText += ", ";
                }
                modText += mod.reason + " (" + std::to_string(mod.amount) + ")";
                first = false;
            }
            (void)ui.createLabel(playerPanel, {0.0f, 0.0f, 490.0f, 14.0f},
                                  LabelData{std::move(modText), {0.6f, 0.6f, 0.7f, 1.0f}, 10.0f});
        }

        // Action buttons row
        WidgetId btnRow = ui.createPanel(
            playerPanel, {0.0f, 0.0f, 490.0f, 26.0f},
            PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
        Widget* btnRowWidget = ui.getWidget(btnRow);
        if (btnRowWidget != nullptr) {
            btnRowWidget->layoutDirection = LayoutDirection::Horizontal;
            btnRowWidget->childSpacing = 6.0f;
        }

        aoc::sim::DiplomacyManager* diplomacy = this->m_diplomacy;
        const PlayerId humanPlayer = this->m_player;

        if (!rel.isAtWar) {
            // Declare War button
            ButtonData warBtn;
            warBtn.label = "Declare War";
            warBtn.fontSize = 11.0f;
            warBtn.normalColor  = {0.4f, 0.15f, 0.15f, 0.9f};
            warBtn.hoverColor   = {0.55f, 0.2f, 0.2f, 0.9f};
            warBtn.pressedColor = {0.3f, 0.1f, 0.1f, 0.9f};
            warBtn.cornerRadius = 3.0f;
            warBtn.onClick = [diplomacy, humanPlayer, otherId, &ui, this]() {
                diplomacy->declareWar(humanPlayer, otherId);
                LOG_INFO("Declared war on player %u", static_cast<unsigned>(otherId));
                this->close(ui);
            };
            (void)ui.createButton(btnRow, {0.0f, 0.0f, 100.0f, 22.0f}, std::move(warBtn));
        } else {
            // Propose Peace button
            ButtonData peaceBtn;
            peaceBtn.label = "Propose Peace";
            peaceBtn.fontSize = 11.0f;
            peaceBtn.normalColor  = {0.15f, 0.35f, 0.15f, 0.9f};
            peaceBtn.hoverColor   = {0.20f, 0.50f, 0.20f, 0.9f};
            peaceBtn.pressedColor = {0.10f, 0.25f, 0.10f, 0.9f};
            peaceBtn.cornerRadius = 3.0f;
            peaceBtn.onClick = [diplomacy, humanPlayer, otherId, &ui, this]() {
                diplomacy->makePeace(humanPlayer, otherId);
                LOG_INFO("Made peace with player %u", static_cast<unsigned>(otherId));
                this->close(ui);
            };
            (void)ui.createButton(btnRow, {0.0f, 0.0f, 110.0f, 22.0f}, std::move(peaceBtn));
        }

        if (!rel.hasOpenBorders && !rel.isAtWar) {
            // Open Borders button
            ButtonData bordersBtn;
            bordersBtn.label = "Open Borders";
            bordersBtn.fontSize = 11.0f;
            bordersBtn.normalColor  = {0.2f, 0.25f, 0.35f, 0.9f};
            bordersBtn.hoverColor   = {0.3f, 0.35f, 0.50f, 0.9f};
            bordersBtn.pressedColor = {0.15f, 0.18f, 0.25f, 0.9f};
            bordersBtn.cornerRadius = 3.0f;
            bordersBtn.onClick = [diplomacy, humanPlayer, otherId, &ui, this]() {
                diplomacy->grantOpenBorders(humanPlayer, otherId);
                LOG_INFO("Granted open borders with player %u", static_cast<unsigned>(otherId));
                this->close(ui);
            };
            (void)ui.createButton(btnRow, {0.0f, 0.0f, 110.0f, 22.0f}, std::move(bordersBtn));
        }

        // Embargo button (only when not at war and not already embargoed)
        if (!rel.isAtWar && !rel.hasEmbargo) {
            ButtonData embargoBtn;
            embargoBtn.label = "Embargo";
            embargoBtn.fontSize = 11.0f;
            embargoBtn.normalColor  = {0.35f, 0.20f, 0.10f, 0.9f};
            embargoBtn.hoverColor   = {0.50f, 0.30f, 0.15f, 0.9f};
            embargoBtn.pressedColor = {0.25f, 0.15f, 0.08f, 0.9f};
            embargoBtn.cornerRadius = 3.0f;
            embargoBtn.onClick = [diplomacy, humanPlayer, otherId, &ui, this]() {
                diplomacy->setEmbargo(humanPlayer, otherId, true);
                LOG_INFO("Imposed trade embargo on player %u", static_cast<unsigned>(otherId));
                this->close(ui);
            };
            (void)ui.createButton(btnRow, {0.0f, 0.0f, 90.0f, 22.0f}, std::move(embargoBtn));
        }
    }

    ui.layout();
}

void DiplomacyScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_playerList = INVALID_WIDGET;
}

void DiplomacyScreen::refresh(UIManager& /*ui*/) {
    // Diplomacy screen is static once opened; re-open to refresh.
}

} // namespace aoc::ui
