/**
 * @file ScoreScreen.cpp
 * @brief End-game score screen implementation.
 */

#include "aoc/ui/ScoreScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <string>

namespace aoc::ui {

// ============================================================================
// ScoreScreen
// ============================================================================

void ScoreScreen::setContext(aoc::ecs::World* world,
                             const aoc::map::HexGrid* grid,
                             const aoc::sim::VictoryResult& result,
                             uint8_t playerCount,
                             std::function<void()> onReturnToMenu) {
    this->m_world          = world;
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

    // Military: sum of (combat strength) for each unit owned
    this->m_world->forEach<aoc::sim::UnitComponent>(
        [this](EntityId /*id*/, aoc::sim::UnitComponent& unit) {
            if (unit.owner < this->m_playerCount) {
                const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(unit.typeId);
                int32_t strength = def.combatStrength;
                if (strength == 0) {
                    strength = def.rangedStrength;
                }
                this->m_scores[unit.owner].military += strength;
            }
        });

    // Science: count completed techs * 10
    this->m_world->forEach<aoc::sim::PlayerTechComponent>(
        [this](EntityId /*id*/, aoc::sim::PlayerTechComponent& tech) {
            if (tech.owner < this->m_playerCount) {
                int32_t completedCount = 0;
                for (std::size_t i = 0; i < tech.completedTechs.size(); ++i) {
                    if (tech.completedTechs[i]) {
                        ++completedCount;
                    }
                }
                this->m_scores[tech.owner].science = completedCount * 10;
            }
        });

    // Culture: from victory tracker
    this->m_world->forEach<aoc::sim::VictoryTrackerComponent>(
        [this](EntityId /*id*/, aoc::sim::VictoryTrackerComponent& vt) {
            if (vt.owner < this->m_playerCount) {
                this->m_scores[vt.owner].culture = static_cast<int32_t>(vt.totalCultureAccumulated);
            }
        });

    // Economy: gold reserves as proxy for treasury + GDP
    this->m_world->forEach<aoc::sim::MonetaryStateComponent>(
        [this](EntityId /*id*/, aoc::sim::MonetaryStateComponent& ms) {
            if (ms.owner < this->m_playerCount) {
                this->m_scores[ms.owner].economy = static_cast<int32_t>(ms.treasury)
                                                 + static_cast<int32_t>(ms.moneySupply)
                                                 + ms.totalCoinValue();
            }
        });

    // Cities: population * 5 + city count * 20
    this->m_world->forEach<aoc::sim::CityComponent>(
        [this](EntityId /*id*/, aoc::sim::CityComponent& city) {
            if (city.owner < this->m_playerCount) {
                this->m_scores[city.owner].cities += city.population * 5 + 20;
            }
        });

    // Religion: count cities following each player's founded religion * 5
    // First find which player founded which religion
    std::array<PlayerId, aoc::sim::MAX_RELIGIONS> religionFounder;
    religionFounder.fill(INVALID_PLAYER);

    this->m_world->forEach<aoc::sim::PlayerFaithComponent>(
        [&religionFounder](EntityId /*id*/, aoc::sim::PlayerFaithComponent& faith) {
            if (faith.foundedReligion != aoc::sim::NO_RELIGION
                && faith.foundedReligion < aoc::sim::MAX_RELIGIONS) {
                religionFounder[faith.foundedReligion] = faith.owner;
            }
        });

    this->m_world->forEach<aoc::sim::CityReligionComponent>(
        [this, &religionFounder](EntityId /*id*/, aoc::sim::CityReligionComponent& rel) {
            aoc::sim::ReligionId dominant = rel.dominantReligion();
            if (dominant != aoc::sim::NO_RELIGION && dominant < aoc::sim::MAX_RELIGIONS) {
                PlayerId founder = religionFounder[dominant];
                if (founder < this->m_playerCount) {
                    this->m_scores[founder].religion += 5;
                }
            }
        });

    // Wonders: count per player * 15
    const aoc::ecs::ComponentPool<aoc::sim::GlobalWonderTracker>* wonderPool =
        this->m_world->getPool<aoc::sim::GlobalWonderTracker>();
    if (wonderPool != nullptr && wonderPool->size() > 0) {
        const aoc::sim::GlobalWonderTracker& tracker = wonderPool->data()[0];
        for (uint8_t w = 0; w < aoc::sim::WONDER_COUNT; ++w) {
            PlayerId builder = tracker.builtBy[w];
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
            this->m_victoryResult.type == aoc::sim::VictoryType::Science    ? "Science" :
            this->m_victoryResult.type == aoc::sim::VictoryType::Domination ? "Domination" :
            this->m_victoryResult.type == aoc::sim::VictoryType::Culture    ? "Culture" :
            this->m_victoryResult.type == aoc::sim::VictoryType::Score      ? "Score" :
            this->m_victoryResult.type == aoc::sim::VictoryType::Religion   ? "Religion" : "Unknown";

        std::string header = "Player " + std::to_string(static_cast<unsigned>(this->m_victoryResult.winner))
                           + " wins by " + victoryName + " Victory!";
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
        const bool isWinner = (entry.owner == this->m_victoryResult.winner);
        const Color rowColor = isWinner
            ? Color{1.0f, 0.95f, 0.4f, 1.0f}
            : Color{0.85f, 0.85f, 0.85f, 1.0f};

        // Build formatted score row
        std::string civName;
        // Resolve civ name from the player's civ component
        bool foundCiv = false;
        this->m_world->forEach<aoc::sim::PlayerCivilizationComponent>(
            [&entry, &civName, &foundCiv](EntityId /*id*/, aoc::sim::PlayerCivilizationComponent& pc) {
                if (pc.owner == entry.owner) {
                    civName = std::string(aoc::sim::civDef(pc.civId).name);
                    foundCiv = true;
                }
            });
        if (!foundCiv) {
            civName = "P" + std::to_string(static_cast<unsigned>(entry.owner));
        }

        // Pad civ name to 10 chars
        while (civName.size() < 10) {
            civName += ' ';
        }

        // Helper to pad number to 6 chars
        constexpr std::size_t COL_WIDTH = 6;
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
