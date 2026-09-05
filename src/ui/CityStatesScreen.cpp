/**
 * @file CityStatesScreen.cpp
 * @brief City-states with envoys, suzerain, quest and levy (see the header).
 */

#include "aoc/ui/CityStatesScreen.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/ui/UIManager.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aoc::ui {

namespace {

constexpr float PANEL_W = 760.0f;
constexpr float PANEL_H = 600.0f;
constexpr float LIST_W  = PANEL_W - 30.0f;
constexpr float LIST_H  = PANEL_H - 130.0f;
constexpr float ROW_W   = LIST_W - 10.0f;
constexpr float ROW_H   = 22.0f;
constexpr float BTN_W   = 130.0f;

void mixHash(uint64_t& hash, uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
}

[[nodiscard]] std::string civName(const aoc::game::Player& player) {
    const aoc::sim::CivilizationDef& def = aoc::sim::civDef(player.civId());
    if (def.name.empty()) {
        return "P" + std::to_string(static_cast<unsigned>(player.id()));
    }
    return std::string(def.name);
}

[[nodiscard]] std::string seatName(const aoc::game::GameState& gameState, PlayerId me, PlayerId seat) {
    if (seat == INVALID_PLAYER) {
        return "none";
    }
    if (seat == me) {
        return "you";
    }
    const aoc::game::Player* p = gameState.player(seat);
    return p != nullptr ? civName(*p) : "P" + std::to_string(static_cast<unsigned>(seat));
}

[[nodiscard]] std::string cityStateName(const aoc::game::GameState& gameState,
                                        const aoc::sim::CityStateComponent& cs, std::size_t index) {
    const aoc::game::Player* seat =
        gameState.player(static_cast<PlayerId>(aoc::sim::CITY_STATE_PLAYER_BASE + index));
    if (seat != nullptr && !seat->cities().empty() && seat->cities().front() != nullptr) {
        return seat->cities().front()->name();
    }
    if (cs.defId < aoc::sim::CITY_STATE_DEFS.size()) {
        return std::string(aoc::sim::CITY_STATE_DEFS[cs.defId].name);
    }
    return "City-state " + std::to_string(index);
}

} // namespace

void CityStatesScreen::setContext(aoc::game::GameState* gameState, const aoc::map::HexGrid* grid,
                                  PlayerId humanPlayer) {
    this->m_gameState = gameState;
    this->m_grid      = grid;
    this->m_player    = humanPlayer;
    this->setBaseContext(gameState, grid, humanPlayer);
}

void CityStatesScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }
    this->m_isOpen = true;

    const WidgetId innerPanel = this->createScreenFrame(ui, "City-States", PANEL_W, PANEL_H,
                                                        this->m_screenW, this->m_screenH);
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

void CityStatesScreen::close(UIManager& ui) {
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

void CityStatesScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }
    if (this->stateFingerprint() == this->m_shownFingerprint) {
        return;
    }
    this->close(ui);
    this->open(ui);
}

void CityStatesScreen::buildRows(UIManager& ui) {
    const aoc::game::Player* me =
        this->m_gameState != nullptr ? this->m_gameState->player(this->m_player) : nullptr;
    if (me == nullptr) {
        ui.setLabelText(this->m_summaryLabel, "No player");
        return;
    }
    const std::vector<aoc::sim::CityStateComponent>& states = this->m_gameState->cityStates();
    int32_t met = 0;
    for (const aoc::sim::CityStateComponent& cs : states) {
        if (cs.hasMet(this->m_player)) {
            ++met;
        }
    }
    ui.setLabelText(this->m_summaryLabel,
                    std::to_string(met) + " of " + std::to_string(states.size())
                        + " city-states met  |  envoys available "
                        + std::to_string(me->envoys().available) + " (lifetime "
                        + std::to_string(me->envoys().lifetime) + ")");

    this->addHeader(ui, "ENVOYS");
    this->addLine(ui,
                  "Each completed civic grants " + std::to_string(aoc::sim::ENVOYS_PER_CIVIC)
                      + " envoy; " + std::to_string(aoc::sim::CS_SUZERAIN_MIN_ENVOYS)
                      + " envoys and a strict lead make you suzerain",
                  true);

    this->addHeader(ui, "CITY-STATES");
    for (std::size_t i = 0; i < states.size(); ++i) {
        const aoc::sim::CityStateComponent& cs = states[i];
        if (!cs.hasMet(this->m_player)) {
            continue;
        }
        std::string levy = "free";
        if (cs.levyPlayer != INVALID_PLAYER) {
            levy = seatName(*this->m_gameState, this->m_player, cs.levyPlayer) + ", "
                   + std::to_string(cs.levyTurnsLeft) + " turns";
        }
        const int32_t mine = this->m_player < MAX_PLAYERS ? cs.envoys[this->m_player] : 0;
        this->addLine(ui,
                      cityStateName(*this->m_gameState, cs, i) + "  ("
                          + std::string(aoc::sim::cityStateTypeName(cs.type)) + ")  |  your envoys "
                          + std::to_string(mine) + "  |  suzerain: "
                          + seatName(*this->m_gameState, this->m_player, cs.suzerain)
                          + "  |  levy: " + levy,
                      false);
        if (cs.activeQuest.isActive && cs.activeQuest.assignedTo == this->m_player) {
            this->addLine(ui,
                          "    quest: " + std::string(aoc::sim::cityStateQuestName(cs.activeQuest.type))
                              + ", +" + std::to_string(cs.activeQuest.envoyReward) + " envoys, "
                              + std::to_string(cs.activeQuest.turnsRemaining) + " turns left",
                          false);
        } else {
            this->addLine(ui, "    no quest for you", true);
        }
        this->addActionRow(ui, i);
    }
    if (met == 0) {
        this->addLine(ui, "No city-state met yet: explore within 6 tiles of one", true);
    }
}

void CityStatesScreen::addActionRow(UIManager& ui, std::size_t cityStateIndex) {
    PanelData rowBg;
    rowBg.backgroundColor = tokens::SURFACE_PARCHMENT_DIM;
    rowBg.cornerRadius    = tokens::CORNER_BUTTON;
    const WidgetId row = ui.createPanel(this->m_list, {0.0f, 0.0f, ROW_W, ROW_H}, std::move(rowBg));
    if (Widget* rw = ui.getWidget(row); rw != nullptr) {
        rw->layoutDirection = LayoutDirection::Horizontal;
        rw->childSpacing    = 6.0f;
        rw->padding         = {2.0f, 4.0f, 2.0f, 4.0f};
    }

    struct Action {
        const char* label;
        ErrorCode (*request)(aoc::game::GameState&, PlayerId, std::size_t);
    };
    const Action actions[3] = {
        {"Send Envoy", &aoc::sim::requestSendEnvoy},
        {"Levy (200 gold)", &aoc::sim::requestLevyCityState},
        {"Bully", &aoc::sim::requestBullyCityState},
    };
    for (const Action& action : actions) {
        ButtonData btn;
        btn.label        = action.label;
        btn.fontSize     = 10.0f;
        btn.cornerRadius = tokens::CORNER_BUTTON;
        btn.normalColor  = tokens::BRONZE_BASE;
        btn.hoverColor   = tokens::BRONZE_LIGHT;
        btn.pressedColor = tokens::STATE_PRESSED;
        btn.labelColor   = tokens::TEXT_GILT;
        ErrorCode (*request)(aoc::game::GameState&, PlayerId, std::size_t) = action.request;
        const char* label = action.label;
        btn.onClick = [this, request, label, cityStateIndex]() {
            if (this->m_gameState == nullptr) {
                return;
            }
            const ErrorCode rc = request(*this->m_gameState, this->m_player, cityStateIndex);
            if (rc != ErrorCode::Ok) {
                LOG_WARN("%s rejected: %.*s", label, static_cast<int>(describeError(rc).size()),
                         describeError(rc).data());
            }
        };
        (void)ui.createButton(row, {0.0f, 0.0f, BTN_W, 18.0f}, std::move(btn));
    }
}

void CityStatesScreen::addHeader(UIManager& ui, const char* text) {
    LabelData ld;
    ld.text     = text;
    ld.color    = tokens::TEXT_HEADER;
    ld.fontSize = 12.0f;
    (void)ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, 20.0f}, std::move(ld));
}

void CityStatesScreen::addLine(UIManager& ui, std::string text, bool dim) {
    LabelData ld;
    ld.text     = std::move(text);
    ld.color    = dim ? tokens::TEXT_DISABLED : tokens::TEXT_INK;
    ld.fontSize = 11.0f;
    (void)ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, 18.0f}, std::move(ld));
}

uint64_t CityStatesScreen::stateFingerprint() const {
    if (this->m_gameState == nullptr) {
        return 0;
    }
    const aoc::game::Player* me = this->m_gameState->player(this->m_player);
    if (me == nullptr) {
        return 0;
    }
    uint64_t h = 1469598103934665603ULL;
    mixHash(h, static_cast<uint64_t>(this->m_gameState->currentTurn()) + 1u);
    mixHash(h, static_cast<uint64_t>(me->envoys().available) + 3u);
    mixHash(h, static_cast<uint64_t>(me->treasury()) + 5u);
    for (const aoc::sim::CityStateComponent& cs : this->m_gameState->cityStates()) {
        const int32_t mine = this->m_player < MAX_PLAYERS ? cs.envoys[this->m_player] : 0;
        mixHash(h, static_cast<uint64_t>(mine) + 7u);
        mixHash(h, static_cast<uint64_t>(static_cast<uint8_t>(cs.suzerain)) + 11u);
        mixHash(h, static_cast<uint64_t>(cs.metMask) + 13u);
        mixHash(h, static_cast<uint64_t>(cs.activeQuest.turnsRemaining) + 17u);
        mixHash(h, static_cast<uint64_t>(cs.activeQuest.isActive ? 1u : 0u) + 19u);
        mixHash(h, static_cast<uint64_t>(static_cast<uint8_t>(cs.levyPlayer)) + 23u);
        mixHash(h, static_cast<uint64_t>(cs.levyTurnsLeft) + 29u);
    }
    return h;
}

} // namespace aoc::ui
