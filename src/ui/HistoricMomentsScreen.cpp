/**
 * @file HistoricMomentsScreen.cpp
 * @brief Historic Moments screen implementation.
 */

#include "aoc/ui/HistoricMomentsScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/tech/EraScore.hpp"

#include <string>
#include <utility>

namespace aoc::ui {

namespace {

constexpr float PANEL_W = 700.0f;
constexpr float PANEL_H = 560.0f;
constexpr float LIST_W  = PANEL_W - 30.0f;
constexpr float LIST_H  = PANEL_H - 130.0f;
constexpr float ROW_W   = LIST_W - 10.0f;
constexpr float ROW_H   = 16.0f;

void mixHash(uint64_t& hash, uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
}

} // namespace

void HistoricMomentsScreen::setContext(aoc::game::GameState* gameState,
                                       const aoc::map::HexGrid* grid, PlayerId humanPlayer) {
    this->m_gameState = gameState;
    this->m_grid      = grid;
    this->m_player    = humanPlayer;
    this->setBaseContext(gameState, grid, humanPlayer);
}

void HistoricMomentsScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }
    this->m_isOpen = true;

    const WidgetId innerPanel = this->createScreenFrame(ui, "Historic Moments", PANEL_W, PANEL_H,
                                                        this->m_screenW, this->m_screenH);
    LabelData summary;
    summary.text     = "";
    summary.color    = tokens::TEXT_INK;
    summary.fontSize = 13.0f;
    this->m_summaryLabel =
        ui.createLabel(innerPanel, {0.0f, 0.0f, LIST_W, 20.0f}, std::move(summary));

    this->m_list = ui.createScrollList(innerPanel, {0.0f, 0.0f, LIST_W, LIST_H});
    if (Widget* list = ui.getWidget(this->m_list); list != nullptr) {
        list->padding      = {4.0f, 4.0f, 4.0f, 4.0f};
        list->childSpacing = 2.0f;
    }

    this->buildRows(ui);
    ui.layout();
}

void HistoricMomentsScreen::close(UIManager& ui) {
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

void HistoricMomentsScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }
    if (this->stateFingerprint() == this->m_shownFingerprint) {
        return;
    }
    this->close(ui);
    this->open(ui);
}

void HistoricMomentsScreen::buildRows(UIManager& ui) {
    this->m_shownFingerprint = this->stateFingerprint();
    const aoc::game::Player* self =
        this->m_gameState != nullptr ? this->m_gameState->player(this->m_player) : nullptr;
    if (self == nullptr) {
        ui.setLabelText(this->m_summaryLabel, "No player context");
        return;
    }
    const aoc::sim::PlayerEraScoreComponent& score = self->eraScore();

    std::string summary = "Age: " + std::string(aoc::sim::ageTypeName(score.currentAgeType));
    if (score.currentAgeType != aoc::sim::AgeType::Normal) {
        summary += " (" + std::to_string(score.turnsRemaining) + " turns left)";
    }
    summary += "   Era score: " + std::to_string(score.eraScore) + " (golden at " +
               std::to_string(score.goldenAgeThreshold) + ", dark below " +
               std::to_string(score.darkAgeThreshold) + ")";
    summary += "   Lifetime: " + std::to_string(score.lifetimeEraScore);
    ui.setLabelText(this->m_summaryLabel, std::move(summary));

    this->addHeader(ui, "TIMELINE (newest first)");
    if (score.moments.empty()) {
        this->addLine(ui, "Nothing yet. Research, adopt civics, complete wonders,", true);
        this->addLine(ui, "recruit great people and capture cities to make history.", true);
    }
    for (std::size_t i = score.moments.size(); i > 0; --i) {
        const aoc::sim::HistoricMoment& m = score.moments[i - 1];
        std::string row                   = "Turn " + std::to_string(m.turn) + "   ";
        row += m.points > 0 ? "+" + std::to_string(m.points) : std::string(" ");
        row += "   " + m.text;
        this->addLine(ui, std::move(row), m.points == 0);
    }
    this->addHeader(ui, "HOW IT WORKS");
    this->addLine(ui,
                  "Every " + std::to_string(10) +
                      " turns the era score decides the next age: at or above the golden threshold "
                      "a Golden Age,",
                  true);
    this->addLine(ui,
                  "below the dark threshold a Dark Age; then the score resets. Techs and civics "
                  "give +2, wonders and captures +3.",
                  true);
}

void HistoricMomentsScreen::addHeader(UIManager& ui, const std::string& text) {
    LabelData ld;
    ld.text     = text;
    ld.color    = tokens::TEXT_HEADER;
    ld.fontSize = 12.0f;
    static_cast<void>(ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, 20.0f}, std::move(ld)));
}

void HistoricMomentsScreen::addLine(UIManager& ui, std::string text, bool dim) {
    LabelData ld;
    ld.text     = std::move(text);
    ld.color    = dim ? tokens::TEXT_DISABLED : tokens::TEXT_INK;
    ld.fontSize = 11.0f;
    static_cast<void>(ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, ROW_H}, std::move(ld)));
}

uint64_t HistoricMomentsScreen::stateFingerprint() const {
    uint64_t hash = 14695981039346656037ULL;
    if (this->m_gameState == nullptr) {
        return hash;
    }
    const aoc::game::Player* self = this->m_gameState->player(this->m_player);
    if (self == nullptr) {
        return hash;
    }
    const aoc::sim::PlayerEraScoreComponent& score = self->eraScore();
    mixHash(hash, static_cast<uint64_t>(score.moments.size()));
    if (!score.moments.empty()) {
        mixHash(hash, static_cast<uint64_t>(static_cast<int64_t>(score.moments.back().turn)));
    }
    mixHash(hash, static_cast<uint64_t>(static_cast<int64_t>(score.eraScore)));
    mixHash(hash, static_cast<uint64_t>(score.currentAgeType));
    mixHash(hash, static_cast<uint64_t>(static_cast<int64_t>(score.turnsRemaining)));
    mixHash(hash, static_cast<uint64_t>(static_cast<int64_t>(score.lifetimeEraScore)));
    return hash;
}

} // namespace aoc::ui
