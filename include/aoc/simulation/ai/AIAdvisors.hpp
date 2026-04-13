#pragma once

/**
 * @file AIAdvisors.hpp
 * @brief AI advisor subsystems that post domain assessments to the AIBlackboard.
 *
 * Each advisor is a free function that updates a subset of the blackboard fields.
 * They run at different update frequencies (every 1, 5, 10, or 20 turns) and
 * together paint a complete picture of the game situation that the production
 * scoring and strategic posture evaluator consume.
 *
 * Call order within a turn (all from AIController::executeTurn):
 *   1. updateMilitaryAssessment  -- every turn
 *   2. updateEconomyAssessment   -- every 5 turns
 *   3. updateExpansionAssessment -- every 10 turns
 *   4. updateResearchAssessment  -- every 10 turns
 *   5. updateDiplomacyAssessment -- every 20 turns
 *   6. evaluateStrategicPosture  -- every turn (cheap read + compare)
 */

#include "aoc/simulation/ai/AIBlackboard.hpp"

namespace aoc::game {
class GameState;
class Player;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {
class DiplomacyManager;
}

namespace aoc::sim::ai {

/**
 * @brief MilitaryAdvisor: assess threat level and force requirements.
 *
 * Counts own military strength vs enemy strength within 10 tiles of any
 * own city.  Posts threat_level [0,1], desiredMilitaryUnits, attackTargets,
 * and defendPriorities to the blackboard.
 *
 * @param gameState  Full game state for player and unit iteration.
 * @param player     The AI player whose blackboard is being updated.
 */
void updateMilitaryAssessment(const aoc::game::GameState& gameState,
                               aoc::game::Player& player);

/**
 * @brief EconomyAdvisor: assess gold pressure and recommend tax rate.
 *
 * Compares per-turn unit and building maintenance against current income.
 * Posts goldPressure [0,1] and recommendedTaxRate to the blackboard.
 *
 * @param player  The AI player whose blackboard is being updated.
 */
void updateEconomyAssessment(aoc::game::Player& player);

/**
 * @brief ExpansionAdvisor: score candidate city sites and assess expansion need.
 *
 * Scans nearby tiles using scoreCityLocation to find the top candidate
 * founding sites.  Posts expansionOpportunity [0,1] and bestCitySites
 * to the blackboard.
 *
 * @param gameState  Full game state for existing city positions.
 * @param player     The AI player whose blackboard is being updated.
 * @param grid       Hex grid for terrain and resource queries.
 */
void updateExpansionAssessment(const aoc::game::GameState& gameState,
                                aoc::game::Player& player,
                                const aoc::map::HexGrid& grid);

/**
 * @brief ResearchAdvisor: compute how far behind the tech average this player is.
 *
 * Compares this player's completed tech count against the all-player average.
 * Posts techGap [0,1] to the blackboard.
 *
 * @param gameState  Full game state for all-player tech comparison.
 * @param player     The AI player whose blackboard is being updated.
 */
void updateResearchAssessment(const aoc::game::GameState& gameState,
                               aoc::game::Player& player);

/**
 * @brief DiplomacyAdvisor: count active wars and post diplomatic danger.
 *
 * A player in many simultaneous wars is in more danger than one at peace.
 * Posts diplomaticDanger [0,1] (saturates at 3 simultaneous wars) to
 * the blackboard.
 *
 * @param gameState  Full game state for player enumeration.
 * @param player     The AI player whose blackboard is being updated.
 * @param diplomacy  Diplomacy manager for war status queries.
 */
void updateDiplomacyAssessment(const aoc::game::GameState& gameState,
                                aoc::game::Player& player,
                                const aoc::sim::DiplomacyManager& diplomacy);

/**
 * @brief Derive a strategic posture from the current blackboard state.
 *
 * Reads all advisor assessments and selects the StrategicPosture that best
 * reflects the AI's situation.  Priority order: Defense > Aggression >
 * Expansion > Development > Economic.
 *
 * @param player  The AI player whose blackboard posture field is updated.
 */
void evaluateStrategicPosture(aoc::game::Player& player);

/**
 * @brief Compute a production utility multiplier for a given posture and category.
 *
 * The multiplier is applied to the raw utility score of every production
 * candidate whose category matches.  Values > 1.0 boost a category;
 * values < 1.0 suppress it.
 *
 * @param posture   The current strategic posture.
 * @param isMilitary  True if the candidate is a military unit.
 * @param isSettler   True if the candidate is a settler.
 * @param isBuilder   True if the candidate is a builder.
 * @param isScience   True if the candidate is a science building/district.
 * @param isGold      True if the candidate is a gold building.
 * @param isTrader    True if the candidate is a trader unit.
 * @return Multiplier to apply to the candidate's utility score.
 */
[[nodiscard]] float postureMultiplier(StrategicPosture posture,
                                       bool isMilitary,
                                       bool isSettler,
                                       bool isBuilder,
                                       bool isScience,
                                       bool isGold,
                                       bool isTrader);

} // namespace aoc::sim::ai
