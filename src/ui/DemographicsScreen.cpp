/**
 * @file DemographicsScreen.cpp
 * @brief Read-only Demographics screen implementation.
 */

#include "aoc/ui/DemographicsScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace aoc::ui {

namespace {

constexpr float PANEL_W = 720.0f;
constexpr float PANEL_H = 560.0f;
constexpr float LIST_W  = PANEL_W - 30.0f;
constexpr float LIST_H  = PANEL_H - 130.0f;
constexpr float ROW_W   = LIST_W - 10.0f;
constexpr float ROW_H   = 16.0f;

[[nodiscard]] std::string civName(const aoc::game::Player& player) {
    const aoc::sim::CivilizationDef& def = aoc::sim::civDef(player.civId());
    if (def.name.empty()) {
        return "P" + std::to_string(static_cast<unsigned>(player.id()));
    }
    return std::string(def.name);
}

/// Population over cities the player still owns, the rule the CSI stats use.
[[nodiscard]] int64_t ownedPopulation(const aoc::game::Player& player) {
    int64_t total = 0;
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (city->owner() == player.id()) {
            total += city->population();
        }
    }
    return total;
}

void mixHash(uint64_t& hash, uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
}

} // namespace

void DemographicsScreen::setContext(aoc::game::GameState* gameState,
                                    const aoc::map::HexGrid* grid, PlayerId humanPlayer,
                                    const aoc::sim::DiplomacyManager* diplomacy) {
    this->m_gameState = gameState;
    this->m_grid      = grid;
    this->m_player    = humanPlayer;
    this->m_diplomacy = diplomacy;
    this->setBaseContext(gameState, grid, humanPlayer);
}

void DemographicsScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }
    this->m_isOpen = true;

    const WidgetId innerPanel = this->createScreenFrame(ui, "Demographics", PANEL_W, PANEL_H,
                                                        this->m_screenW, this->m_screenH);
    LabelData summary;
    summary.text         = "";
    summary.color        = tokens::TEXT_INK;
    summary.fontSize     = 13.0f;
    this->m_summaryLabel = ui.createLabel(innerPanel, {0.0f, 0.0f, LIST_W, 20.0f},
                                          std::move(summary));

    this->m_list = ui.createScrollList(innerPanel, {0.0f, 0.0f, LIST_W, LIST_H});
    if (Widget* list = ui.getWidget(this->m_list); list != nullptr) {
        list->padding      = {4.0f, 4.0f, 4.0f, 4.0f};
        list->childSpacing = 2.0f;
    }

    this->buildRows(ui);
    ui.layout();
}

void DemographicsScreen::close(UIManager& ui) {
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

void DemographicsScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }
    if (this->stateFingerprint() == this->m_shownFingerprint) {
        return;
    }
    this->close(ui);
    this->open(ui);
}

std::vector<DemographicsScreen::Row> DemographicsScreen::knownRows(int32_t& unmetOut) const {
    std::vector<Row> rows;
    unmetOut = 0;
    if (this->m_gameState == nullptr) {
        return rows;
    }
    for (const std::unique_ptr<aoc::game::Player>& p : this->m_gameState->players()) {
        if (p == nullptr || p->id() == BARBARIAN_PLAYER || p->victoryTracker().isEliminated) {
            continue;
        }
        const bool known = p->id() == this->m_player || this->m_diplomacy == nullptr ||
                           this->m_diplomacy->haveMet(this->m_player, p->id());
        if (!known) {
            ++unmetOut;
            continue;
        }
        Row row{};
        row.id         = p->id();
        row.population = ownedPopulation(*p);
        row.cities     = p->ownedCityCount();
        row.military   = p->militaryUnitCount();
        row.techs      = p->victoryTracker().scienceProgress;
        row.treasury   = static_cast<int64_t>(p->monetary().treasury);
        row.gdp        = static_cast<int64_t>(p->monetary().gdp);
        row.tourism    = static_cast<int64_t>(p->tourism().tourismPerTurn);
        row.score      = p->victoryTracker().score;
        rows.push_back(row);
    }
    return rows;
}

void DemographicsScreen::buildRows(UIManager& ui) {
    this->m_shownFingerprint = this->stateFingerprint();
    int32_t unmet            = 0;
    std::vector<Row> rows    = this->knownRows(unmet);
    if (rows.empty()) {
        ui.setLabelText(this->m_summaryLabel, "No player context");
        return;
    }
    ui.setLabelText(this->m_summaryLabel,
                    "Ranking " + std::to_string(rows.size()) + " known civilizations (" +
                        std::to_string(unmet) + " not yet met)");

    this->addHeader(ui, "YOUR STANDING");
    this->addMetricRow(ui, rows, "Population", &Row::population);
    this->addMetricRow(ui, rows, "Cities", &Row::cities);
    this->addMetricRow(ui, rows, "Military units", &Row::military);
    this->addMetricRow(ui, rows, "Technologies", &Row::techs);
    this->addMetricRow(ui, rows, "Treasury", &Row::treasury);
    this->addMetricRow(ui, rows, "GDP", &Row::gdp);
    this->addMetricRow(ui, rows, "Tourism", &Row::tourism);
    this->addMetricRow(ui, rows, "Score", &Row::score);

    this->addHeader(ui, "CIVILIZATIONS BY SCORE");
    this->addCivRows(ui, std::move(rows));
}

void DemographicsScreen::addMetricRow(UIManager& ui, const std::vector<Row>& rows,
                                      const char* label, int64_t Row::*field) {
    int64_t mine  = 0;
    int64_t best  = rows.front().*field;
    int64_t worst = rows.front().*field;
    int64_t sum   = 0;
    int32_t rank  = 1;
    for (const Row& r : rows) {
        const int64_t v = r.*field;
        best  = std::max(best, v);
        worst = std::min(worst, v);
        sum += v;
        if (r.id == this->m_player) {
            mine = v;
        }
    }
    for (const Row& r : rows) {
        if (r.id != this->m_player && r.*field > mine) {
            ++rank;
        }
    }
    const int64_t average = sum / static_cast<int64_t>(rows.size());
    std::string text = std::string(label) + "  yours " + std::to_string(mine) + "  rank " +
                       std::to_string(rank) + " of " + std::to_string(rows.size()) +
                       "  |  best " + std::to_string(best) + "  average " +
                       std::to_string(average) + "  worst " + std::to_string(worst);
    this->addLine(ui, std::move(text), false);
}

void DemographicsScreen::addCivRows(UIManager& ui, std::vector<Row> rows) {
    // Highest score first; ties by player id so the order is stable.
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return a.score != b.score ? a.score > b.score : a.id < b.id;
    });
    for (const Row& r : rows) {
        const aoc::game::Player* p = this->m_gameState->player(r.id);
        if (p == nullptr) {
            continue;
        }
        std::string text = civName(*p);
        if (r.id == this->m_player) {
            text += " (you)";
        }
        text += "  score " + std::to_string(r.score) + "  pop " + std::to_string(r.population) +
                "  cities " + std::to_string(r.cities) + "  techs " + std::to_string(r.techs);
        this->addLine(ui, std::move(text), r.id != this->m_player);
    }
}

void DemographicsScreen::addHeader(UIManager& ui, const std::string& text) {
    LabelData ld;
    ld.text     = text;
    ld.color    = tokens::TEXT_HEADER;
    ld.fontSize = 12.0f;
    static_cast<void>(ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, 20.0f}, std::move(ld)));
}

void DemographicsScreen::addLine(UIManager& ui, std::string text, bool dim) {
    LabelData ld;
    ld.text     = std::move(text);
    ld.color    = dim ? tokens::TEXT_DISABLED : tokens::TEXT_INK;
    ld.fontSize = 11.0f;
    static_cast<void>(ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, ROW_H}, std::move(ld)));
}

uint64_t DemographicsScreen::stateFingerprint() const {
    uint64_t hash = 14695981039346656037ULL;
    if (this->m_gameState == nullptr) {
        return hash;
    }
    mixHash(hash, static_cast<uint64_t>(this->m_gameState->currentTurn()));
    int32_t unmet = 0;
    for (const Row& r : this->knownRows(unmet)) {
        mixHash(hash, static_cast<uint64_t>(r.id));
        mixHash(hash, static_cast<uint64_t>(r.population));
        mixHash(hash, static_cast<uint64_t>(r.cities));
        mixHash(hash, static_cast<uint64_t>(r.military));
        mixHash(hash, static_cast<uint64_t>(r.techs));
        mixHash(hash, static_cast<uint64_t>(r.treasury));
        mixHash(hash, static_cast<uint64_t>(r.gdp));
        mixHash(hash, static_cast<uint64_t>(r.score));
    }
    mixHash(hash, static_cast<uint64_t>(unmet));
    return hash;
}

} // namespace aoc::ui
