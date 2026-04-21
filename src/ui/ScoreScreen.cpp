/**
 * @file ScoreScreen.cpp
 * @brief End-game score screen implementation.
 */

#include "aoc/ui/ScoreScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <string>

namespace aoc::ui {

// ============================================================================
// ScoreScreen
// ============================================================================

void ScoreScreen::setContext(aoc::game::GameState* gameState,
                             const aoc::map::HexGrid* grid,
                             const aoc::sim::VictoryResult& result,
                             uint8_t playerCount,
                             std::function<void()> onReturnToMenu) {
    this->m_gameState      = gameState;
    this->m_grid           = grid;
    this->m_victoryResult  = result;
    this->m_playerCount    = playerCount;
    this->m_onReturnToMenu = std::move(onReturnToMenu);
}

void ScoreScreen::computeScores() {
    this->m_scores.clear();
    this->m_scores.resize(this->m_playerCount);

    for (uint8_t p = 0; p < this->m_playerCount; ++p) {
        this->m_scores[p].owner = p;
    }

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : this->m_gameState->players()) {
        const PlayerId ownerId = playerPtr->id();
        if (ownerId >= this->m_playerCount) {
            continue;
        }

        PlayerScoreEntry& entry = this->m_scores[ownerId];

        // Military: sum of combat strength for each unit owned
        for (const std::unique_ptr<aoc::game::Unit>& unit : playerPtr->units()) {
            const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(unit->typeId());
            int32_t strength = def.combatStrength;
            if (strength == 0) {
                strength = def.rangedStrength;
            }
            entry.military += strength;
        }

        // Science: count completed techs * 10
        {
            int32_t completedCount = 0;
            const aoc::sim::PlayerTechComponent& tech = playerPtr->tech();
            for (std::size_t i = 0; i < tech.completedTechs.size(); ++i) {
                if (tech.completedTechs[i]) {
                    ++completedCount;
                }
            }
            entry.science = completedCount * 10;
        }

        // Culture: from victory tracker
        entry.culture = static_cast<int32_t>(
            playerPtr->victoryTracker().totalCultureAccumulated);

        // Economy: treasury + monetary money supply
        {
            const aoc::sim::MonetaryStateComponent& ms = playerPtr->monetary();
            entry.economy = static_cast<int32_t>(playerPtr->treasury())
                          + static_cast<int32_t>(ms.moneySupply)
                          + ms.totalCoinValue();
        }

        // Cities: population * 5 + city count * 20
        for (const std::unique_ptr<aoc::game::City>& city : playerPtr->cities()) {
            entry.cities += city->population() * 5 + 20;
        }
    }

    // Religion: count cities following each player's founded religion * 5
    // Build a map of religion -> founder player ID
    std::array<PlayerId, aoc::sim::MAX_RELIGIONS> religionFounder;
    religionFounder.fill(INVALID_PLAYER);

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : this->m_gameState->players()) {
        const aoc::sim::PlayerFaithComponent& faith = playerPtr->faith();
        if (faith.foundedReligion != aoc::sim::NO_RELIGION
            && faith.foundedReligion < aoc::sim::MAX_RELIGIONS) {
            religionFounder[faith.foundedReligion] = playerPtr->id();
        }
    }

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : this->m_gameState->players()) {
        for (const std::unique_ptr<aoc::game::City>& city : playerPtr->cities()) {
            const aoc::sim::ReligionId dominant = city->religion().dominantReligion();
            if (dominant != aoc::sim::NO_RELIGION && dominant < aoc::sim::MAX_RELIGIONS) {
                const PlayerId founder = religionFounder[dominant];
                if (founder < this->m_playerCount) {
                    this->m_scores[founder].religion += 5;
                }
            }
        }
    }

    // Wonders: count per player * 15
    {
        const aoc::sim::GlobalWonderTracker& tracker = this->m_gameState->wonderTracker();
        for (uint8_t w = 0; w < aoc::sim::WONDER_COUNT; ++w) {
            const PlayerId builder = tracker.builtBy[w];
            if (builder < this->m_playerCount) {
                this->m_scores[builder].wonders += 15;
            }
        }
    }

    // Compute totals
    for (PlayerScoreEntry& entry : this->m_scores) {
        entry.total = entry.military + entry.science + entry.culture
                    + entry.economy + entry.cities + entry.religion + entry.wonders;
    }

    // Sort descending by total score
    std::sort(this->m_scores.begin(), this->m_scores.end(),
              [](const PlayerScoreEntry& a, const PlayerScoreEntry& b) {
                  return a.total > b.total;
              });
}

void ScoreScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    this->m_isOpen = true;
    this->computeScores();

    LOG_INFO("Opening end-game score screen");

    constexpr float SCREEN_W = 700.0f;
    constexpr float SCREEN_H = 520.0f;
    WidgetId innerPanel = this->createScreenFrame(
        ui, "Final Scores", SCREEN_W, SCREEN_H, this->m_screenW, this->m_screenH);

    // Victory announcement
    {
        const char* victoryName =
            this->m_victoryResult.type == aoc::sim::VictoryType::Science       ? "Science" :
            this->m_victoryResult.type == aoc::sim::VictoryType::Domination    ? "Domination" :
            this->m_victoryResult.type == aoc::sim::VictoryType::Culture       ? "Culture" :
            this->m_victoryResult.type == aoc::sim::VictoryType::Score         ? "Score" :
            this->m_victoryResult.type == aoc::sim::VictoryType::Religion      ? "Religion" :
            this->m_victoryResult.type == aoc::sim::VictoryType::Confederation ? "Confederation" : "Unknown";

        std::string header;
        if (this->m_victoryResult.type == aoc::sim::VictoryType::Confederation) {
            header = "Confederation Victory! Players ";
            header += std::to_string(static_cast<unsigned>(this->m_victoryResult.winner));
            for (aoc::PlayerId co : this->m_victoryResult.coWinners) {
                header += ", ";
                header += std::to_string(static_cast<unsigned>(co));
            }
            header += " share the win!";
        } else {
            header = "Player " + std::to_string(static_cast<unsigned>(this->m_victoryResult.winner))
                   + " wins by " + victoryName + " Victory!";
        }
        (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 660.0f, 24.0f},
            LabelData{std::move(header), {1.0f, 0.85f, 0.2f, 1.0f}, 18.0f});
    }

    // Column header
    {
        std::string colHeader = "Player     Mil   Sci   Cul   Eco   City  Rel   Won   TOTAL";
        (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 660.0f, 16.0f},
            LabelData{std::move(colHeader), {0.7f, 0.7f, 0.7f, 1.0f}, 12.0f});
    }

    // Score rows
    for (const PlayerScoreEntry& entry : this->m_scores) {
        bool isWinner = (entry.owner == this->m_victoryResult.winner);
        if (!isWinner) {
            for (aoc::PlayerId co : this->m_victoryResult.coWinners) {
                if (co == entry.owner) { isWinner = true; break; }
            }
        }
        const Color rowColor = isWinner
            ? Color{1.0f, 0.95f, 0.4f, 1.0f}
            : Color{0.85f, 0.85f, 0.85f, 1.0f};

        // Resolve civ name from player's civId
        std::string civName;
        const aoc::game::Player* playerObj = this->m_gameState->player(entry.owner);
        if (playerObj != nullptr) {
            civName = std::string(aoc::sim::civDef(playerObj->civId()).name);
        }
        if (civName.empty()) {
            civName = "P" + std::to_string(static_cast<unsigned>(entry.owner));
        }

        // Pad civ name to 10 chars
        while (civName.size() < 10) {
            civName += ' ';
        }

        // Helper to pad number to 6 chars
        constexpr std::size_t COL_WIDTH = 6;
        // Lambda type is unnameable — kept as a local lambda
        const auto padNum = [](int32_t val) -> std::string {
            std::string s = std::to_string(val);
            while (s.size() < COL_WIDTH) {
                s = " " + s;
            }
            return s;
        };

        std::string row = civName
                        + padNum(entry.military)
                        + padNum(entry.science)
                        + padNum(entry.culture)
                        + padNum(entry.economy)
                        + padNum(entry.cities)
                        + padNum(entry.religion)
                        + padNum(entry.wonders)
                        + padNum(entry.total);

        (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 660.0f, 16.0f},
            LabelData{std::move(row), rowColor, 12.0f});
    }

    // Return to Menu button
    {
        ButtonData btn;
        btn.label        = "Return to Menu";
        btn.fontSize     = 14.0f;
        btn.normalColor  = {0.2f, 0.35f, 0.2f, 0.9f};
        btn.hoverColor   = {0.3f, 0.50f, 0.3f, 0.9f};
        btn.pressedColor = {0.15f, 0.25f, 0.15f, 0.9f};
        btn.labelColor   = {1.0f, 1.0f, 1.0f, 1.0f};
        btn.cornerRadius = 4.0f;
        btn.onClick = [this]() {
            if (this->m_onReturnToMenu) {
                this->m_onReturnToMenu();
            }
        };
        (void)ui.createButton(innerPanel, {0.0f, 0.0f, 180.0f, 36.0f}, std::move(btn));
    }

    ui.layout();
}

void ScoreScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
}

void ScoreScreen::refresh(UIManager& /*ui*/) {
    // Score screen is static once opened -- nothing to refresh.
}

} // namespace aoc::ui
