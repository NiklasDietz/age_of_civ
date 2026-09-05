/**
 * @file GreatWorksScreen.cpp
 * @brief Great Works by city with move buttons and the tourism race (see the header).
 */

#include "aoc/ui/GreatWorksScreen.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/culture/GreatWorks.hpp"
#include "aoc/simulation/culture/Tourism.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/ui/UIManager.hpp"

#include <cstdio>
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
constexpr float MOVE_BTN_W = 120.0f;
/// Buttons per work row; more destinations than this do not fit the row.
constexpr int32_t MAX_MOVE_TARGETS = 3;

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

} // namespace

void GreatWorksScreen::setContext(aoc::game::GameState* gameState, const aoc::map::HexGrid* grid,
                                  PlayerId humanPlayer,
                                  const aoc::sim::DiplomacyManager* diplomacy) {
    this->m_gameState = gameState;
    this->m_grid      = grid;
    this->m_player    = humanPlayer;
    this->m_diplomacy = diplomacy;
    this->setBaseContext(gameState, grid, humanPlayer);
}

void GreatWorksScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }
    this->m_isOpen = true;

    const WidgetId innerPanel = this->createScreenFrame(ui, "Great Works", PANEL_W, PANEL_H,
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

void GreatWorksScreen::close(UIManager& ui) {
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

void GreatWorksScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }
    if (this->stateFingerprint() == this->m_shownFingerprint) {
        return;
    }
    this->close(ui);
    this->open(ui);
}

void GreatWorksScreen::buildRows(UIManager& ui) {
    const aoc::game::Player* owner =
        this->m_gameState != nullptr ? this->m_gameState->player(this->m_player) : nullptr;
    if (owner == nullptr) {
        ui.setLabelText(this->m_summaryLabel, "No player");
        return;
    }
    const aoc::sim::GreatWorkTally tally    = aoc::sim::tallyGreatWorks(*owner);
    const aoc::sim::PlayerTourismComponent& t = owner->tourism();
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%d works in %d slots  |  tourism +%.0f per turn  |  %d foreign tourists",
                  tally.works, tally.capacity, static_cast<double>(t.tourismPerTurn),
                  t.foreignTourists);
    ui.setLabelText(this->m_summaryLabel, buf);

    this->addHeader(ui, "TOURISM");
    this->addTourismRows(ui);
    this->addHeader(ui, "WORKS BY CITY");
    this->addCityRows(ui);
}

void GreatWorksScreen::addTourismRows(UIManager& ui) {
    const aoc::game::Player* owner = this->m_gameState->player(this->m_player);
    const aoc::sim::PlayerTourismComponent& t = owner->tourism();
    char buf[200];
    std::snprintf(buf, sizeof(buf),
                  "Per turn +%.0f  (lifetime %.0f)  |  foreign tourists %d  |  domestic tourists %d",
                  static_cast<double>(t.tourismPerTurn), static_cast<double>(t.cumulativeTourism),
                  t.foreignTourists, t.domesticTourists);
    this->addLine(ui, buf, false);
    this->addLine(ui, "Culture victory: your foreign tourists must exceed every rival's domestic tourists",
                  true);

    for (const std::unique_ptr<aoc::game::Player>& rival : this->m_gameState->players()) {
        if (rival == nullptr || rival->id() == this->m_player || rival->id() == BARBARIAN_PLAYER
            || rival->victoryTracker().isEliminated) {
            continue;
        }
        if (this->m_diplomacy != nullptr && !this->m_diplomacy->haveMet(this->m_player, rival->id())) {
            continue;
        }
        const int32_t theirs = rival->tourism().domesticTourists;
        const bool ahead     = t.foreignTourists > theirs;
        std::snprintf(buf, sizeof(buf), "vs %s  their domestic %d  yours foreign %d  (%s)",
                      civName(*rival).c_str(), theirs, t.foreignTourists, ahead ? "ahead" : "behind");
        this->addLine(ui, buf, !ahead);
    }
}

void GreatWorksScreen::addCityRows(UIManager& ui) {
    const aoc::game::Player* owner = this->m_gameState->player(this->m_player);
    std::vector<const aoc::game::City*> cities;
    for (const std::unique_ptr<aoc::game::City>& city : owner->cities()) {
        if (city != nullptr && city->owner() == this->m_player) {
            cities.push_back(city.get());
        }
    }
    if (cities.empty()) {
        this->addLine(ui, "No cities yet", true);
        return;
    }
    bool anySlot = false;
    for (const aoc::game::City* city : cities) {
        const int32_t capacity = aoc::sim::greatWorkCapacity(*city);
        anySlot                = anySlot || capacity > 0;
        const int32_t used     = static_cast<int32_t>(city->greatWorks().works.size());
        this->addLine(ui, city->name() + "  works " + std::to_string(used) + "/" + std::to_string(capacity),
                      capacity == 0);

        for (int32_t index = 0; index < used; ++index) {
            PanelData rowBg;
            rowBg.backgroundColor = tokens::SURFACE_PARCHMENT_DIM;
            rowBg.cornerRadius    = tokens::CORNER_BUTTON;
            const WidgetId row =
                ui.createPanel(this->m_list, {0.0f, 0.0f, ROW_W, ROW_H}, std::move(rowBg));
            if (Widget* rw = ui.getWidget(row); rw != nullptr) {
                rw->layoutDirection = LayoutDirection::Horizontal;
                rw->childSpacing    = 6.0f;
                rw->padding         = {2.0f, 4.0f, 2.0f, 4.0f};
            }
            const std::string text =
                "    " + aoc::sim::describeGreatWork(city->greatWorks().works[static_cast<size_t>(index)]);
            (void)ui.createLabel(row, {0.0f, 0.0f, ROW_W - (MOVE_BTN_W + 6.0f) * MAX_MOVE_TARGETS, 18.0f},
                                 LabelData{text, tokens::TEXT_INK, 11.0f});

            int32_t targets = 0;
            for (const aoc::game::City* other : cities) {
                if (other == city || aoc::sim::freeGreatWorkSlots(*other) <= 0) {
                    continue;
                }
                if (targets >= MAX_MOVE_TARGETS) {
                    break;
                }
                ++targets;
                const aoc::hex::AxialCoord from = city->location();
                const aoc::hex::AxialCoord to   = other->location();
                ButtonData mv;
                mv.label        = "Move to " + other->name();
                mv.fontSize     = 10.0f;
                mv.cornerRadius = tokens::CORNER_BUTTON;
                mv.normalColor  = tokens::BRONZE_BASE;
                mv.hoverColor   = tokens::BRONZE_LIGHT;
                mv.pressedColor = tokens::STATE_PRESSED;
                mv.labelColor   = tokens::TEXT_GILT;
                mv.onClick      = [this, from, index, to]() {
                    if (this->m_gameState == nullptr) {
                        return;
                    }
                    const ErrorCode rc = aoc::sim::requestMoveGreatWork(*this->m_gameState, this->m_player,
                                                                        from, index, to);
                    if (rc != ErrorCode::Ok) {
                        LOG_WARN("Great Work move rejected: %.*s",
                                 static_cast<int>(describeError(rc).size()), describeError(rc).data());
                    }
                };
                (void)ui.createButton(row, {0.0f, 0.0f, MOVE_BTN_W, 18.0f}, std::move(mv));
            }
        }
    }
    if (!anySlot) {
        this->addLine(ui, "Build an Amphitheater or Art Museum in a Theatre Square to house works", true);
    }
}

void GreatWorksScreen::addHeader(UIManager& ui, const char* text) {
    LabelData ld;
    ld.text     = text;
    ld.color    = tokens::TEXT_HEADER;
    ld.fontSize = 12.0f;
    (void)ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, 20.0f}, std::move(ld));
}

void GreatWorksScreen::addLine(UIManager& ui, std::string text, bool dim) {
    LabelData ld;
    ld.text     = std::move(text);
    ld.color    = dim ? tokens::TEXT_DISABLED : tokens::TEXT_INK;
    ld.fontSize = 11.0f;
    (void)ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, 18.0f}, std::move(ld));
}

uint64_t GreatWorksScreen::stateFingerprint() const {
    if (this->m_gameState == nullptr) {
        return 0;
    }
    const aoc::game::Player* owner = this->m_gameState->player(this->m_player);
    if (owner == nullptr) {
        return 0;
    }
    uint64_t h = 1469598103934665603ULL;
    mixHash(h, static_cast<uint64_t>(this->m_gameState->currentTurn()) + 1u);
    mixHash(h, static_cast<uint64_t>(owner->tourism().tourismPerTurn * 10.0f) + 3u);
    mixHash(h, static_cast<uint64_t>(owner->tourism().foreignTourists) + 5u);
    for (const std::unique_ptr<aoc::game::City>& city : owner->cities()) {
        if (city == nullptr) { continue; }
        mixHash(h, static_cast<uint64_t>(city->greatWorks().works.size()) + 7u);
        mixHash(h, static_cast<uint64_t>(aoc::sim::greatWorkCapacity(*city)) + 11u);
        mixHash(h, static_cast<uint64_t>(static_cast<uint8_t>(city->owner())) + 13u);
    }
    return h;
}

} // namespace aoc::ui
