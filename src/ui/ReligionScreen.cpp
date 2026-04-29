/**
 * @file ReligionScreen.cpp
 * @brief Religion screen implementation: faith display, founding, and unit purchase.
 */

#include "aoc/ui/ReligionScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/core/Log.hpp"

#include <string>

namespace aoc::ui {

void ReligionScreen::setContext(aoc::game::GameState* gameState, aoc::map::HexGrid* grid,
                                 PlayerId humanPlayer) {
    this->m_gameState = gameState;
    this->m_grid      = grid;
    this->m_player    = humanPlayer;
}

void ReligionScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    this->m_isOpen = true;

    WidgetId innerPanel = this->createScreenFrame(
        ui, "Religion", 550.0f, 520.0f, this->m_screenW, this->m_screenH);

    // Faith label
    this->m_faithLabel = ui.createLabel(
        innerPanel, {0.0f, 0.0f, 520.0f, 20.0f},
        LabelData{"Faith: 0", {1.0f, 0.85f, 0.3f, 1.0f}, 14.0f});

    // Status label
    this->m_statusLabel = ui.createLabel(
        innerPanel, {0.0f, 0.0f, 520.0f, 18.0f},
        LabelData{"No pantheon yet.", {0.8f, 0.8f, 0.8f, 1.0f}, 12.0f});

    // Belief/action list
    this->m_beliefList = ui.createScrollList(
        innerPanel, {0.0f, 0.0f, 520.0f, 400.0f});

    Widget* listWidget = ui.getWidget(this->m_beliefList);
    if (listWidget != nullptr) {
        listWidget->padding = {4.0f, 4.0f, 4.0f, 4.0f};
        listWidget->childSpacing = 6.0f;
    }

    this->buildBeliefList(ui);
    ui.layout();
}

void ReligionScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel  = INVALID_WIDGET;
    }
    this->m_faithLabel  = INVALID_WIDGET;
    this->m_statusLabel = INVALID_WIDGET;
    this->m_beliefList  = INVALID_WIDGET;
}

void ReligionScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }

    const aoc::game::Player* player = this->m_gameState->player(this->m_player);
    if (player == nullptr) {
        return;
    }

    const aoc::sim::PlayerFaithComponent& faith = player->faith();
    const float currentFaith = faith.faith;
    const bool hasPantheon = faith.hasPantheon;
    const aoc::sim::ReligionId foundedReligion = faith.foundedReligion;

    // Update faith label text
    ui.setLabelText(this->m_faithLabel,
                    "Faith: " + std::to_string(static_cast<int>(currentFaith)));

    // Update status label text
    {
        std::string statusText;
        if (foundedReligion != aoc::sim::NO_RELIGION) {
            const aoc::sim::GlobalReligionTracker& tracker = this->m_gameState->religionTracker();
            statusText = "Religion: " + tracker.religions[foundedReligion].name;
        } else if (hasPantheon) {
            statusText = "Pantheon founded. Need " +
                std::to_string(static_cast<int>(aoc::sim::RELIGION_FAITH_COST)) +
                " faith to found a religion.";
        } else {
            statusText = "No pantheon yet. Need " +
                std::to_string(static_cast<int>(aoc::sim::PANTHEON_FAITH_COST)) +
                " faith to found a pantheon.";
        }
        ui.setLabelText(this->m_statusLabel, std::move(statusText));
    }
}

void ReligionScreen::buildBeliefList(UIManager& ui) {
    if (this->m_beliefList == INVALID_WIDGET || this->m_gameState == nullptr) {
        return;
    }

    const aoc::game::Player* player = this->m_gameState->player(this->m_player);
    if (player == nullptr) {
        return;
    }

    const aoc::sim::PlayerFaithComponent& faith = player->faith();
    const float currentFaith = faith.faith;
    const bool hasPantheon = faith.hasPantheon;
    const aoc::sim::ReligionId foundedReligion = faith.foundedReligion;

    const std::array<aoc::sim::BeliefDef, aoc::sim::BELIEF_COUNT>& beliefs = aoc::sim::allBeliefs();

    // Found Pantheon section
    if (!hasPantheon) {
        const bool canAfford = currentFaith >= aoc::sim::PANTHEON_FAITH_COST;
        std::string btnText = "Found Pantheon (" +
            std::to_string(static_cast<int>(aoc::sim::PANTHEON_FAITH_COST)) + " faith)";

        ButtonData btnData;
        btnData.label = std::move(btnText);
        btnData.normalColor = canAfford
            ? tokens::STATE_SUCCESS
            : tokens::TEXT_DISABLED;
        btnData.hoverColor = tokens::DIPLO_FRIENDLY;
        btnData.labelColor = tokens::TEXT_PARCHMENT;
        btnData.fontSize = 12.0f;

        if (canAfford) {
            aoc::game::GameState* gsPtr = this->m_gameState;
            const PlayerId playerId = this->m_player;
            btnData.onClick = [gsPtr, playerId]() {
                const std::array<aoc::sim::BeliefDef, aoc::sim::BELIEF_COUNT>& b = aoc::sim::allBeliefs();
                aoc::game::Player* p = gsPtr->player(playerId);
                if (p == nullptr) {
                    return;
                }
                aoc::sim::PlayerFaithComponent& fp = p->faith();
                fp.faith -= aoc::sim::PANTHEON_FAITH_COST;
                fp.hasPantheon = true;
                fp.pantheonBelief = 4; // Divine Inspiration
                LOG_INFO("Player %u founded pantheon with belief: %.*s",
                         static_cast<unsigned>(playerId),
                         static_cast<int>(b[4].name.size()), b[4].name.data());
            };
        }

        (void)ui.createButton(this->m_beliefList, {0.0f, 0.0f, 510.0f, 28.0f},
                               std::move(btnData));

        // Show follower belief descriptions
        for (uint8_t bi = 4; bi < 8; ++bi) {
            std::string text = "  " + std::string(beliefs[bi].name) + " - " +
                std::string(beliefs[bi].description);
            (void)ui.createLabel(this->m_beliefList, {0.0f, 0.0f, 510.0f, 16.0f},
                LabelData{std::move(text), {0.7f, 0.7f, 0.7f, 1.0f}, 11.0f});
        }
    }
    // Found Religion section
    else if (foundedReligion == aoc::sim::NO_RELIGION) {
        const bool canAfford = currentFaith >= aoc::sim::RELIGION_FAITH_COST;
        std::string btnText = "Found Religion (" +
            std::to_string(static_cast<int>(aoc::sim::RELIGION_FAITH_COST)) + " faith)";

        ButtonData btnData;
        btnData.label = std::move(btnText);
        btnData.normalColor = canAfford
            ? tokens::DIPLO_ALLIED
            : tokens::TEXT_DISABLED;
        btnData.hoverColor = tokens::RES_SCIENCE;
        btnData.labelColor = tokens::TEXT_PARCHMENT;
        btnData.fontSize = 12.0f;

        if (canAfford) {
            aoc::game::GameState* gsPtr = this->m_gameState;
            const PlayerId playerId = this->m_player;
            btnData.onClick = [gsPtr, playerId]() {
                aoc::sim::GlobalReligionTracker& tracker = gsPtr->religionTracker();
                if (!tracker.canFoundReligion()) {
                    return;
                }

                std::string religionName = std::string(
                    aoc::sim::RELIGION_NAMES[tracker.religionsFoundedCount %
                                             aoc::sim::RELIGION_NAMES.size()]);

                aoc::sim::ReligionId newId = tracker.foundReligion(religionName, playerId);

                tracker.religions[newId].founderBelief  = 0;  // Tithe
                tracker.religions[newId].worshipBelief  = 8;  // Cathedral
                tracker.religions[newId].enhancerBelief = 12; // Missionary Zeal

                aoc::game::Player* p = gsPtr->player(playerId);
                if (p == nullptr) {
                    return;
                }
                aoc::sim::PlayerFaithComponent& fp = p->faith();
                fp.faith -= aoc::sim::RELIGION_FAITH_COST;
                fp.foundedReligion = newId;
                tracker.religions[newId].followerBelief = fp.pantheonBelief;

                LOG_INFO("Player %u founded religion: %s",
                         static_cast<unsigned>(playerId), religionName.c_str());
            };
        }

        (void)ui.createButton(this->m_beliefList, {0.0f, 0.0f, 510.0f, 28.0f},
                               std::move(btnData));

        // Show religion names
        (void)ui.createLabel(this->m_beliefList, {0.0f, 0.0f, 510.0f, 16.0f},
            LabelData{"Available names:", {0.8f, 0.8f, 0.6f, 1.0f}, 12.0f});
        for (std::size_t ni = 0; ni < aoc::sim::RELIGION_NAMES.size(); ++ni) {
            std::string nameText = "  " + std::string(aoc::sim::RELIGION_NAMES[ni]);
            (void)ui.createLabel(this->m_beliefList, {0.0f, 0.0f, 510.0f, 14.0f},
                LabelData{std::move(nameText), {0.7f, 0.7f, 0.7f, 1.0f}, 11.0f});
        }
    }
    // Has religion: show info and purchase buttons
    else {
        {
            const aoc::sim::GlobalReligionTracker& tracker = this->m_gameState->religionTracker();
            const aoc::sim::ReligionDef& religion = tracker.religions[foundedReligion];

            // Show beliefs
            if (religion.founderBelief < aoc::sim::BELIEF_COUNT) {
                std::string text = "Founder: " + std::string(beliefs[religion.founderBelief].name) +
                    " - " + std::string(beliefs[religion.founderBelief].description);
                (void)ui.createLabel(this->m_beliefList, {0.0f, 0.0f, 510.0f, 16.0f},
                    LabelData{std::move(text), {0.9f, 0.8f, 0.5f, 1.0f}, 11.0f});
            }
            if (religion.followerBelief < aoc::sim::BELIEF_COUNT) {
                std::string text = "Follower: " + std::string(beliefs[religion.followerBelief].name) +
                    " - " + std::string(beliefs[religion.followerBelief].description);
                (void)ui.createLabel(this->m_beliefList, {0.0f, 0.0f, 510.0f, 16.0f},
                    LabelData{std::move(text), {0.9f, 0.8f, 0.5f, 1.0f}, 11.0f});
            }
            if (religion.worshipBelief < aoc::sim::BELIEF_COUNT) {
                std::string text = "Worship: " + std::string(beliefs[religion.worshipBelief].name) +
                    " - " + std::string(beliefs[religion.worshipBelief].description);
                (void)ui.createLabel(this->m_beliefList, {0.0f, 0.0f, 510.0f, 16.0f},
                    LabelData{std::move(text), {0.9f, 0.8f, 0.5f, 1.0f}, 11.0f});
            }
            if (religion.enhancerBelief < aoc::sim::BELIEF_COUNT) {
                std::string text = "Enhancer: " + std::string(beliefs[religion.enhancerBelief].name) +
                    " - " + std::string(beliefs[religion.enhancerBelief].description);
                (void)ui.createLabel(this->m_beliefList, {0.0f, 0.0f, 510.0f, 16.0f},
                    LabelData{std::move(text), {0.9f, 0.8f, 0.5f, 1.0f}, 11.0f});
            }

            // Count follower cities by iterating all players
            int32_t followerCount = 0;
            for (const std::unique_ptr<aoc::game::Player>& otherPlayer : this->m_gameState->players()) {
                for (const std::unique_ptr<aoc::game::City>& city : otherPlayer->cities()) {
                    if (city->religion().dominantReligion() == foundedReligion) {
                        ++followerCount;
                    }
                }
            }
            std::string followerText = "Follower cities: " + std::to_string(followerCount);
            (void)ui.createLabel(this->m_beliefList, {0.0f, 0.0f, 510.0f, 16.0f},
                LabelData{std::move(followerText), {0.7f, 0.9f, 0.7f, 1.0f}, 12.0f});
        }

        // Buy Missionary button
        {
            const bool canAfford = currentFaith >= aoc::sim::MISSIONARY_FAITH_COST;
            std::string btnText = "Buy Missionary (" +
                std::to_string(static_cast<int>(aoc::sim::MISSIONARY_FAITH_COST)) + " faith)";

            ButtonData btnData;
            btnData.label = std::move(btnText);
            btnData.normalColor = canAfford
                ? tokens::RES_CULTURE
                : tokens::TEXT_DISABLED;
            btnData.hoverColor = tokens::RES_CULTURE;
            btnData.labelColor = tokens::TEXT_PARCHMENT;
            btnData.fontSize = 12.0f;

            if (canAfford) {
                btnData.onClick = [this, foundedReligion]() {
                    this->spawnReligiousUnit(UnitTypeId{19}, foundedReligion);
                };
            }

            (void)ui.createButton(this->m_beliefList, {0.0f, 0.0f, 510.0f, 28.0f},
                                   std::move(btnData));
        }

        // Buy Apostle button
        {
            const bool canAfford = currentFaith >= aoc::sim::APOSTLE_FAITH_COST;
            std::string btnText = "Buy Apostle (" +
                std::to_string(static_cast<int>(aoc::sim::APOSTLE_FAITH_COST)) + " faith)";

            ButtonData btnData;
            btnData.label = std::move(btnText);
            btnData.normalColor = canAfford
                ? tokens::DIPLO_HOSTILE
                : tokens::TEXT_DISABLED;
            btnData.hoverColor = tokens::STATE_DANGER;
            btnData.labelColor = tokens::TEXT_PARCHMENT;
            btnData.fontSize = 12.0f;

            if (canAfford) {
                btnData.onClick = [this, foundedReligion]() {
                    this->spawnReligiousUnit(UnitTypeId{20}, foundedReligion);
                };
            }

            (void)ui.createButton(this->m_beliefList, {0.0f, 0.0f, 510.0f, 28.0f},
                                   std::move(btnData));
        }
    }
}

void ReligionScreen::spawnReligiousUnit(UnitTypeId typeId, aoc::sim::ReligionId religion) {
    if (this->m_gameState == nullptr) {
        return;
    }

    aoc::game::Player* player = this->m_gameState->player(this->m_player);
    if (player == nullptr) {
        return;
    }

    // Deduct faith
    const float cost = (typeId.value == 19) ? aoc::sim::MISSIONARY_FAITH_COST
                                            : aoc::sim::APOSTLE_FAITH_COST;
    aoc::sim::PlayerFaithComponent& faith = player->faith();
    if (faith.faith < cost) {
        return;
    }
    faith.faith -= cost;

    // Find a city with a Holy Site to spawn the unit; fall back to any city
    hex::AxialCoord spawnLocation{0, 0};
    bool foundCity = false;

    for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
        if (city->districts().hasDistrict(aoc::sim::DistrictType::HolySite)) {
            spawnLocation = city->location();
            foundCity = true;
            break;
        }
    }

    if (!foundCity) {
        if (!player->cities().empty()) {
            spawnLocation = player->cities().front()->location();
            foundCity = true;
        }
    }

    if (!foundCity) {
        LOG_ERROR("Cannot spawn religious unit: no city found for player %u",
                  static_cast<unsigned>(this->m_player));
        // Refund faith since spawn failed
        faith.faith += cost;
        return;
    }

    player->addUnit(typeId, spawnLocation);
    // NOTE: religion spreading stored separately when Unit gains spreadingReligion field.
    (void)religion;

    const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(typeId);
    LOG_INFO("Player %u purchased %.*s at (%d,%d)",
             static_cast<unsigned>(this->m_player),
             static_cast<int>(def.name.size()), def.name.data(),
             spawnLocation.q, spawnLocation.r);
}

} // namespace aoc::ui
