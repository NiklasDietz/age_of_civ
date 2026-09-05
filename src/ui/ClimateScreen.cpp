/**
 * @file ClimateScreen.cpp
 * @brief Global climate readout (see the header).
 */

#include "aoc/ui/ClimateScreen.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/climate/Climate.hpp"
#include "aoc/simulation/climate/NaturalDisasters.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/ui/UIManager.hpp"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace aoc::ui {

namespace {

constexpr float PANEL_W = 700.0f;
constexpr float PANEL_H = 560.0f;
constexpr float LIST_W  = PANEL_W - 30.0f;
constexpr float LIST_H  = PANEL_H - 130.0f;
constexpr float ROW_W   = LIST_W - 10.0f;
/// The turn-loop thresholds in TurnProcessor: a flood event at this many drowned
/// coast tiles, a drought event at this temperature.
constexpr int32_t FLOOD_EVENT_TILES = 5;
constexpr float DROUGHT_EVENT_TEMP  = 2.0f;
constexpr float CO2_PENALTY_START   = 3000.0f;

void mixHash(uint64_t& hash, uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
}

[[nodiscard]] std::string ownerName(const aoc::game::GameState& gameState, PlayerId owner) {
    if (owner == INVALID_PLAYER) {
        return "unclaimed land";
    }
    const aoc::game::Player* p = gameState.player(owner);
    if (p == nullptr) {
        return "P" + std::to_string(static_cast<unsigned>(owner));
    }
    const aoc::sim::CivilizationDef& def = aoc::sim::civDef(p->civId());
    return def.name.empty() ? "P" + std::to_string(static_cast<unsigned>(owner)) : std::string(def.name);
}

} // namespace

void ClimateScreen::setContext(aoc::game::GameState* gameState, const aoc::map::HexGrid* grid,
                               PlayerId humanPlayer) {
    this->m_gameState = gameState;
    this->m_grid      = grid;
    this->m_player    = humanPlayer;
    this->setBaseContext(gameState, grid, humanPlayer);
}

void ClimateScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }
    this->m_isOpen = true;
    const WidgetId innerPanel =
        this->createScreenFrame(ui, "Climate", PANEL_W, PANEL_H, this->m_screenW, this->m_screenH);
    LabelData summary;
    summary.color    = tokens::TEXT_INK;
    summary.fontSize = 13.0f;
    this->m_summaryLabel = ui.createLabel(innerPanel, {0.0f, 0.0f, LIST_W, 20.0f}, std::move(summary));
    this->m_list = ui.createScrollList(innerPanel, {0.0f, 0.0f, LIST_W, LIST_H});
    if (Widget* list = ui.getWidget(this->m_list); list != nullptr) {
        list->padding      = {4.0f, 4.0f, 4.0f, 4.0f};
        list->childSpacing = 3.0f;
    }
    this->buildRows(ui);
    this->m_shownFingerprint = this->stateFingerprint();
}

void ClimateScreen::close(UIManager& ui) {
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

void ClimateScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }
    if (this->stateFingerprint() == this->m_shownFingerprint) {
        return;
    }
    this->close(ui);
    this->open(ui);
}

void ClimateScreen::buildRows(UIManager& ui) {
    if (this->m_gameState == nullptr) {
        ui.setLabelText(this->m_summaryLabel, "No game");
        return;
    }
    const aoc::sim::GlobalClimateComponent& climate = this->m_gameState->climate();
    char buf[200];
    std::snprintf(buf, sizeof(buf), "Temperature +%.2f C  |  CO2 %.0f  |  %d coast tiles flooded",
                  static_cast<double>(climate.globalTemperature), static_cast<double>(climate.co2Level),
                  climate.seaLevelRise);
    ui.setLabelText(this->m_summaryLabel, buf);

    this->addHeader(ui, "CLIMATE");
    std::snprintf(buf, sizeof(buf), "Global temperature: +%.2f C above the pre-industrial baseline",
                  static_cast<double>(climate.globalTemperature));
    this->addLine(ui, buf, false);
    std::snprintf(buf, sizeof(buf), "CO2: %.0f (natural sink %.1f per turn; food penalty starts at %.0f)",
                  static_cast<double>(climate.co2Level), static_cast<double>(aoc::sim::CO2_DECAY_PER_TURN),
                  static_cast<double>(CO2_PENALTY_START));
    this->addLine(ui, buf, false);
    std::snprintf(buf, sizeof(buf), "Food multiplier from CO2: x%.2f",
                  static_cast<double>(aoc::sim::climateFoodMultiplier(climate)));
    this->addLine(ui, buf, climate.co2Level < CO2_PENALTY_START);
    std::snprintf(buf, sizeof(buf), "Sea level: %d coast tiles flooded (flood emergency at %d)", climate.seaLevelRise,
                  FLOOD_EVENT_TILES);
    this->addLine(ui, buf, false);
    std::snprintf(buf, sizeof(buf), "Drought emergency at +%.1f C; wildfires above +1.2 C, hurricanes above +1.5 C",
                  static_cast<double>(DROUGHT_EVENT_TEMP));
    this->addLine(ui, buf, true);

    this->addHeader(ui, "RECENT DISASTERS");
    const std::vector<aoc::sim::DisasterRecord>& history = this->m_gameState->disasterHistory();
    if (history.empty()) {
        this->addLine(ui, "None so far", true);
    }
    for (std::size_t i = history.size(); i > 0; --i) {
        const aoc::sim::DisasterRecord& d = history[i - 1];
        std::snprintf(buf, sizeof(buf), "Turn %d  %.*s at (%d,%d), severity %d, %s", d.turn,
                      static_cast<int>(aoc::sim::disasterTypeName(d.type).size()),
                      aoc::sim::disasterTypeName(d.type).data(), d.at.q, d.at.r, d.severity,
                      ownerName(*this->m_gameState, d.owner).c_str());
        this->addLine(ui, buf, d.owner != this->m_player);
    }
}

void ClimateScreen::addHeader(UIManager& ui, const char* text) {
    LabelData ld;
    ld.text     = text;
    ld.color    = tokens::TEXT_HEADER;
    ld.fontSize = 12.0f;
    (void)ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, 20.0f}, std::move(ld));
}

void ClimateScreen::addLine(UIManager& ui, std::string text, bool dim) {
    LabelData ld;
    ld.text     = std::move(text);
    ld.color    = dim ? tokens::TEXT_DISABLED : tokens::TEXT_INK;
    ld.fontSize = 11.0f;
    (void)ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, 18.0f}, std::move(ld));
}

uint64_t ClimateScreen::stateFingerprint() const {
    if (this->m_gameState == nullptr) {
        return 0;
    }
    const aoc::sim::GlobalClimateComponent& climate = this->m_gameState->climate();
    uint64_t h = 1469598103934665603ULL;
    mixHash(h, static_cast<uint64_t>(this->m_gameState->currentTurn()) + 1u);
    mixHash(h, static_cast<uint64_t>(climate.co2Level * 10.0f) + 3u);
    mixHash(h, static_cast<uint64_t>(climate.globalTemperature * 100.0f) + 5u);
    mixHash(h, static_cast<uint64_t>(climate.seaLevelRise) + 7u);
    mixHash(h, static_cast<uint64_t>(this->m_gameState->disasterHistory().size()) + 11u);
    return h;
}

} // namespace aoc::ui
