/**
 * @file ReligionScreen.cpp
 * @brief Religion screen implementation: faith display, founding, and unit purchase.
 */

#include "aoc/ui/ReligionScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include <cstdio>
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
    this->m_shownFingerprint = this->stateFingerprint();
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

uint64_t ReligionScreen::stateFingerprint() const {
    if (this->m_gameState == nullptr) {
        return 0;
    }
    const aoc::game::Player* player = this->m_gameState->player(this->m_player);
    if (player == nullptr) {
        return 0;
    }
    uint64_t h = 1469598103934665603ULL;
    const auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ULL; };
    const aoc::sim::PlayerFaithComponent& faith = player->faith();
    mix(faith.hasPantheon ? 1u : 2u);
    mix(static_cast<uint64_t>(faith.pantheonBelief) + 3u);
    mix(static_cast<uint64_t>(faith.foundedReligion) + 5u);
    mix(static_cast<uint64_t>(faith.faith >= aoc::sim::PANTHEON_FAITH_COST) + 7u);
    mix(static_cast<uint64_t>(faith.faith >= aoc::sim::RELIGION_FAITH_COST) + 11u);
    mix(static_cast<uint64_t>(faith.faith >= aoc::sim::MISSIONARY_FAITH_COST) + 13u);
    mix(static_cast<uint64_t>(faith.faith >= aoc::sim::APOSTLE_FAITH_COST) + 17u);
    mix(static_cast<uint64_t>(this->m_pickFounder) * 1000003u + this->m_pickWorship * 1009u
        + this->m_pickEnhancer);
    const aoc::sim::GlobalReligionTracker& tracker = this->m_gameState->religionTracker();
    mix(static_cast<uint64_t>(tracker.religionsFoundedCount) + 19u);
    for (const std::unique_ptr<aoc::game::Player>& other : this->m_gameState->players()) {
        for (const std::unique_ptr<aoc::game::City>& city : other->cities()) {
            mix(static_cast<uint64_t>(city->religion().dominantReligion()) + 23u);
        }
    }
    return h;
}

void ReligionScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }
    if (const uint64_t now = this->stateFingerprint(); now != this->m_shownFingerprint) {
        this->close(ui);
        this->open(ui);
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

    // Found Pantheon section: the human picks the follower belief (Civ VI); taken
    // beliefs are listed but not offered. Until 2026-09-05 one button took the
    // first free belief.
    if (!hasPantheon) {
        const bool canAfford = currentFaith >= aoc::sim::PANTHEON_FAITH_COST;
        (void)ui.createLabel(this->m_beliefList, {0.0f, 0.0f, 510.0f, 16.0f},
            LabelData{"Found a pantheon (" + std::to_string(static_cast<int>(aoc::sim::PANTHEON_FAITH_COST))
                          + " faith): choose its belief", {0.9f, 0.8f, 0.5f, 1.0f}, 12.0f});
        aoc::game::GameState* gsPtr = this->m_gameState;
        const PlayerId playerId     = this->m_player;
        for (uint8_t bi = 0; bi < aoc::sim::BELIEF_COUNT; ++bi) {
            if (beliefs[bi].type != aoc::sim::BeliefType::Follower) { continue; }
            const bool free = aoc::sim::beliefIsFree(*this->m_gameState, bi, aoc::sim::BeliefType::Follower);
            ButtonData btnData;
            btnData.label = std::string(free ? "Found: " : "Taken: ") + std::string(beliefs[bi].name)
                          + " - " + std::string(beliefs[bi].description);
            btnData.normalColor = (canAfford && free) ? tokens::STATE_SUCCESS : tokens::TEXT_DISABLED;
            btnData.hoverColor  = tokens::DIPLO_FRIENDLY;
            btnData.labelColor  = tokens::TEXT_PARCHMENT;
            btnData.fontSize    = 11.0f;
            if (canAfford && free) {
                btnData.onClick = [gsPtr, playerId, bi]() {
                    const ErrorCode rc = aoc::sim::requestFoundPantheon(*gsPtr, playerId, bi);
                    if (rc != ErrorCode::Ok) {
                        LOG_WARN("Pantheon refused: %.*s", static_cast<int>(describeError(rc).size()),
                                 describeError(rc).data());
                    }
                };
            }
            (void)ui.createButton(this->m_beliefList, {0.0f, 0.0f, 510.0f, 24.0f}, std::move(btnData));
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

        // The picks default to the first free belief of each type; the rows below
        // change them (Civ VI's belief choice, 2026-09-05).
        const uint8_t founder = this->m_pickFounder != 255 ? this->m_pickFounder
            : aoc::sim::firstFreeBelief(*this->m_gameState, aoc::sim::BeliefType::Founder);
        const uint8_t worship = this->m_pickWorship != 255 ? this->m_pickWorship
            : aoc::sim::firstFreeBelief(*this->m_gameState, aoc::sim::BeliefType::Worship);
        const uint8_t enhancer = this->m_pickEnhancer != 255 ? this->m_pickEnhancer
            : aoc::sim::firstFreeBelief(*this->m_gameState, aoc::sim::BeliefType::Enhancer);
        if (canAfford) {
            aoc::game::GameState* gsPtr = this->m_gameState;
            const PlayerId playerId = this->m_player;
            btnData.onClick = [gsPtr, playerId, founder, worship, enhancer]() {
                const ErrorCode rc =
                    aoc::sim::requestFoundReligion(*gsPtr, playerId, founder, worship, enhancer);
                if (rc != ErrorCode::Ok) {
                    LOG_WARN("Founding refused: %.*s", static_cast<int>(describeError(rc).size()),
                             describeError(rc).data());
                }
            };
        }

        (void)ui.createButton(this->m_beliefList, {0.0f, 0.0f, 510.0f, 28.0f},
                               std::move(btnData));

        const struct { aoc::sim::BeliefType type; const char* title; uint8_t chosen; uint8_t* pick; } groups[] = {
            {aoc::sim::BeliefType::Founder,  "Founder belief",  founder,  &this->m_pickFounder},
            {aoc::sim::BeliefType::Worship,  "Worship belief",  worship,  &this->m_pickWorship},
            {aoc::sim::BeliefType::Enhancer, "Enhancer belief", enhancer, &this->m_pickEnhancer},
        };
        for (const auto& group : groups) {
            (void)ui.createLabel(this->m_beliefList, {0.0f, 0.0f, 510.0f, 16.0f},
                LabelData{std::string(group.title) + ":", {0.9f, 0.8f, 0.5f, 1.0f}, 12.0f});
            for (uint8_t bi = 0; bi < aoc::sim::BELIEF_COUNT; ++bi) {
                if (beliefs[bi].type != group.type) { continue; }
                const bool free = aoc::sim::beliefIsFree(*this->m_gameState, bi, group.type);
                const bool chosen = bi == group.chosen;
                ButtonData pick;
                pick.label = std::string(chosen ? "[x] " : (free ? "[ ] " : "[taken] "))
                           + std::string(beliefs[bi].name) + " - " + std::string(beliefs[bi].description);
                pick.normalColor = chosen ? tokens::DIPLO_ALLIED : (free ? tokens::BRONZE_BASE : tokens::TEXT_DISABLED);
                pick.hoverColor  = tokens::BRONZE_LIGHT;
                pick.labelColor  = tokens::TEXT_PARCHMENT;
                pick.fontSize    = 11.0f;
                if (free) {
                    uint8_t* slot = group.pick;
                    pick.onClick = [slot, bi]() { *slot = bi; };
                }
                (void)ui.createButton(this->m_beliefList, {0.0f, 0.0f, 510.0f, 22.0f}, std::move(pick));
            }
        }

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

    // World religions and the pressure in the player's cities (every state).
    {
        const aoc::sim::GlobalReligionTracker& tracker = this->m_gameState->religionTracker();
        (void)ui.createLabel(this->m_beliefList, {0.0f, 0.0f, 510.0f, 16.0f},
            LabelData{"World religions: " + std::to_string(tracker.religionsFoundedCount),
                      {0.8f, 0.8f, 0.6f, 1.0f}, 12.0f});
        for (uint8_t r = 0; r < tracker.religionsFoundedCount; ++r) {
            const aoc::sim::ReligionDef& def = tracker.religions[r];
            int32_t cities = 0;
            for (const std::unique_ptr<aoc::game::Player>& other : this->m_gameState->players()) {
                for (const std::unique_ptr<aoc::game::City>& city : other->cities()) {
                    if (city->religion().dominantReligion() == r) { ++cities; }
                }
            }
            std::string row = "  " + def.name + " (founder P" + std::to_string(def.founder) + ")  "
                            + std::to_string(cities) + " cities";
            if (def.founder == this->m_player) { row += "  [yours]"; }
            (void)ui.createLabel(this->m_beliefList, {0.0f, 0.0f, 510.0f, 14.0f},
                LabelData{std::move(row), {0.75f, 0.75f, 0.75f, 1.0f}, 11.0f});
        }
        (void)ui.createLabel(this->m_beliefList, {0.0f, 0.0f, 510.0f, 16.0f},
            LabelData{"Your cities:", {0.8f, 0.8f, 0.6f, 1.0f}, 12.0f});
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            const aoc::sim::ReligionId dom = city->religion().dominantReligion();
            std::string row = "  " + city->name() + ": ";
            if (dom == aoc::sim::NO_RELIGION) {
                row += "no religion";
            } else {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s (%.0f pressure)", tracker.religions[dom].name.c_str(),
                              static_cast<double>(city->religion().pressure[dom]));
                row += buf;
            }
            (void)ui.createLabel(this->m_beliefList, {0.0f, 0.0f, 510.0f, 14.0f},
                LabelData{std::move(row), {0.75f, 0.75f, 0.75f, 1.0f}, 11.0f});
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

    aoc::game::Unit& unit  = player->addUnit(typeId, spawnLocation);
    unit.spreadingReligion = religion;   // addUnit derives it too; the purchase names it

    const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(typeId);
    LOG_INFO("Player %u purchased %.*s at (%d,%d)",
             static_cast<unsigned>(this->m_player),
             static_cast<int>(def.name.size()), def.name.data(),
             spawnLocation.q, spawnLocation.r);
}

} // namespace aoc::ui
