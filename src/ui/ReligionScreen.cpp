/**
 * @file ReligionScreen.cpp
 * @brief Religion screen implementation: faith display, founding, and unit purchase.
 */

#include "aoc/ui/ReligionScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/core/Log.hpp"

#include <string>

namespace aoc::ui {

void ReligionScreen::setContext(aoc::ecs::World* world, aoc::map::HexGrid* grid,
                                 PlayerId humanPlayer) {
    this->m_world  = world;
    this->m_grid   = grid;
    this->m_player = humanPlayer;
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
    if (!this->m_isOpen || this->m_world == nullptr) {
        return;
    }

    // Find player faith
    float currentFaith = 0.0f;
    bool hasPantheon = false;
    aoc::sim::ReligionId foundedReligion = aoc::sim::NO_RELIGION;

    const aoc::ecs::ComponentPool<aoc::sim::PlayerFaithComponent>* faithPool =
        this->m_world->getPool<aoc::sim::PlayerFaithComponent>();
    if (faithPool != nullptr) {
        for (uint32_t i = 0; i < faithPool->size(); ++i) {
            if (faithPool->data()[i].owner == this->m_player) {
                currentFaith = faithPool->data()[i].faith;
                hasPantheon = faithPool->data()[i].hasPantheon;
                foundedReligion = faithPool->data()[i].foundedReligion;
                break;
            }
        }
    }

    // Update faith label text
    ui.setLabelText(this->m_faithLabel,
                    "Faith: " + std::to_string(static_cast<int>(currentFaith)));

    // Update status label text
    {
        std::string statusText;
        if (foundedReligion != aoc::sim::NO_RELIGION) {
            const aoc::ecs::ComponentPool<aoc::sim::GlobalReligionTracker>* trackerPool =
                this->m_world->getPool<aoc::sim::GlobalReligionTracker>();
            if (trackerPool != nullptr && trackerPool->size() > 0) {
                const aoc::sim::GlobalReligionTracker& tracker = trackerPool->data()[0];
                statusText = "Religion: " + tracker.religions[foundedReligion].name;
            }
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
    if (this->m_beliefList == INVALID_WIDGET || this->m_world == nullptr) {
        return;
    }

    // Find player faith state
    float currentFaith = 0.0f;
    bool hasPantheon = false;
    aoc::sim::ReligionId foundedReligion = aoc::sim::NO_RELIGION;

    const aoc::ecs::ComponentPool<aoc::sim::PlayerFaithComponent>* faithPool =
        this->m_world->getPool<aoc::sim::PlayerFaithComponent>();
    if (faithPool != nullptr) {
        for (uint32_t i = 0; i < faithPool->size(); ++i) {
            if (faithPool->data()[i].owner == this->m_player) {
                currentFaith = faithPool->data()[i].faith;
                hasPantheon = faithPool->data()[i].hasPantheon;
                foundedReligion = faithPool->data()[i].foundedReligion;
                break;
            }
        }
    }

    const std::array<aoc::sim::BeliefDef, aoc::sim::BELIEF_COUNT>& beliefs = aoc::sim::allBeliefs();

    // Found Pantheon section
    if (!hasPantheon) {
        const bool canAfford = currentFaith >= aoc::sim::PANTHEON_FAITH_COST;
        std::string btnText = "Found Pantheon (" +
            std::to_string(static_cast<int>(aoc::sim::PANTHEON_FAITH_COST)) + " faith)";

        ButtonData btnData;
        btnData.label = std::move(btnText);
        btnData.normalColor = canAfford
            ? Color{0.2f, 0.6f, 0.2f, 0.9f}
            : Color{0.4f, 0.4f, 0.4f, 0.9f};
        btnData.hoverColor = {0.3f, 0.7f, 0.3f, 0.9f};
        btnData.labelColor = {1.0f, 1.0f, 1.0f, 1.0f};
        btnData.fontSize = 12.0f;

        if (canAfford) {
            btnData.onClick = [this]() {
                const std::array<aoc::sim::BeliefDef, aoc::sim::BELIEF_COUNT>& b = aoc::sim::allBeliefs();
                aoc::ecs::ComponentPool<aoc::sim::PlayerFaithComponent>* fp =
                    this->m_world->getPool<aoc::sim::PlayerFaithComponent>();
                if (fp == nullptr) return;
                for (uint32_t fi = 0; fi < fp->size(); ++fi) {
                    if (fp->data()[fi].owner == this->m_player) {
                        fp->data()[fi].faith -= aoc::sim::PANTHEON_FAITH_COST;
                        fp->data()[fi].hasPantheon = true;
                        fp->data()[fi].pantheonBelief = 4; // Divine Inspiration
                        LOG_INFO("Player %u founded pantheon with belief: %.*s",
                                 static_cast<unsigned>(this->m_player),
                                 static_cast<int>(b[4].name.size()), b[4].name.data());
                        break;
                    }
                }
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
            ? Color{0.2f, 0.5f, 0.7f, 0.9f}
            : Color{0.4f, 0.4f, 0.4f, 0.9f};
        btnData.hoverColor = {0.3f, 0.6f, 0.8f, 0.9f};
        btnData.labelColor = {1.0f, 1.0f, 1.0f, 1.0f};
        btnData.fontSize = 12.0f;

        if (canAfford) {
            btnData.onClick = [this]() {
                aoc::ecs::ComponentPool<aoc::sim::GlobalReligionTracker>* tp =
                    this->m_world->getPool<aoc::sim::GlobalReligionTracker>();
                aoc::ecs::ComponentPool<aoc::sim::PlayerFaithComponent>* fp =
                    this->m_world->getPool<aoc::sim::PlayerFaithComponent>();
                if (tp == nullptr || fp == nullptr || tp->size() == 0) return;

                aoc::sim::GlobalReligionTracker& tracker = tp->data()[0];
                if (!tracker.canFoundReligion()) return;

                std::string religionName = std::string(
                    aoc::sim::RELIGION_NAMES[tracker.religionsFoundedCount %
                                             aoc::sim::RELIGION_NAMES.size()]);

                aoc::sim::ReligionId newId = tracker.foundReligion(religionName, this->m_player);

                tracker.religions[newId].founderBelief  = 0;  // Tithe
                tracker.religions[newId].worshipBelief  = 8;  // Cathedral
                tracker.religions[newId].enhancerBelief = 12; // Missionary Zeal

                for (uint32_t fi = 0; fi < fp->size(); ++fi) {
                    if (fp->data()[fi].owner == this->m_player) {
                        fp->data()[fi].faith -= aoc::sim::RELIGION_FAITH_COST;
                        fp->data()[fi].foundedReligion = newId;
                        tracker.religions[newId].followerBelief = fp->data()[fi].pantheonBelief;
                        break;
                    }
                }

                LOG_INFO("Player %u founded religion: %s",
                         static_cast<unsigned>(this->m_player), religionName.c_str());
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
        const aoc::ecs::ComponentPool<aoc::sim::GlobalReligionTracker>* trackerPool =
            this->m_world->getPool<aoc::sim::GlobalReligionTracker>();
        if (trackerPool != nullptr && trackerPool->size() > 0) {
            const aoc::sim::GlobalReligionTracker& tracker = trackerPool->data()[0];
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

            // Count follower cities
            int32_t followerCount = 0;
            const aoc::ecs::ComponentPool<aoc::sim::CityReligionComponent>* cityRelPool =
                this->m_world->getPool<aoc::sim::CityReligionComponent>();
            if (cityRelPool != nullptr) {
                for (uint32_t ci = 0; ci < cityRelPool->size(); ++ci) {
                    if (cityRelPool->data()[ci].dominantReligion() == foundedReligion) {
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
                ? Color{0.5f, 0.3f, 0.6f, 0.9f}
                : Color{0.4f, 0.4f, 0.4f, 0.9f};
            btnData.hoverColor = {0.6f, 0.4f, 0.7f, 0.9f};
            btnData.labelColor = {1.0f, 1.0f, 1.0f, 1.0f};
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
                ? Color{0.6f, 0.3f, 0.3f, 0.9f}
                : Color{0.4f, 0.4f, 0.4f, 0.9f};
            btnData.hoverColor = {0.7f, 0.4f, 0.4f, 0.9f};
            btnData.labelColor = {1.0f, 1.0f, 1.0f, 1.0f};
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
    if (this->m_world == nullptr) {
        return;
    }

    // Deduct faith
    const float cost = (typeId.value == 19) ? aoc::sim::MISSIONARY_FAITH_COST
                                            : aoc::sim::APOSTLE_FAITH_COST;
    aoc::ecs::ComponentPool<aoc::sim::PlayerFaithComponent>* faithPool =
        this->m_world->getPool<aoc::sim::PlayerFaithComponent>();
    if (faithPool == nullptr) {
        return;
    }

    bool deducted = false;
    for (uint32_t i = 0; i < faithPool->size(); ++i) {
        if (faithPool->data()[i].owner == this->m_player) {
            if (faithPool->data()[i].faith < cost) {
                return;
            }
            faithPool->data()[i].faith -= cost;
            deducted = true;
            break;
        }
    }
    if (!deducted) {
        return;
    }

    // Find a city with a Holy Site to spawn the unit
    hex::AxialCoord spawnLocation{0, 0};
    bool foundCity = false;

    const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
        this->m_world->getPool<aoc::sim::CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            const aoc::sim::CityComponent& city = cityPool->data()[i];
            if (city.owner != this->m_player) {
                continue;
            }
            EntityId cityEntity = cityPool->entities()[i];
            const aoc::sim::CityDistrictsComponent* districts =
                this->m_world->tryGetComponent<aoc::sim::CityDistrictsComponent>(cityEntity);
            if (districts != nullptr && districts->hasDistrict(aoc::sim::DistrictType::HolySite)) {
                spawnLocation = city.location;
                foundCity = true;
                break;
            }
        }
        // Fallback: any owned city
        if (!foundCity) {
            for (uint32_t i = 0; i < cityPool->size(); ++i) {
                if (cityPool->data()[i].owner == this->m_player) {
                    spawnLocation = cityPool->data()[i].location;
                    foundCity = true;
                    break;
                }
            }
        }
    }

    if (!foundCity) {
        LOG_ERROR("Cannot spawn religious unit: no city found for player %u",
                  static_cast<unsigned>(this->m_player));
        return;
    }

    EntityId unitEntity = this->m_world->createEntity();
    aoc::sim::UnitComponent unit = aoc::sim::UnitComponent::create(
        this->m_player, typeId, spawnLocation);
    unit.spreadingReligion = religion;
    this->m_world->addComponent<aoc::sim::UnitComponent>(unitEntity, std::move(unit));

    const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(typeId);
    LOG_INFO("Player %u purchased %.*s at (%d,%d)",
             static_cast<unsigned>(this->m_player),
             static_cast<int>(def.name.size()), def.name.data(),
             spawnLocation.q, spawnLocation.r);
}

} // namespace aoc::ui
