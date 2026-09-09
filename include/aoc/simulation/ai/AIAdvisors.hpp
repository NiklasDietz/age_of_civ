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
 *
 * MEASURED 2026-09-09: this advisor is NOT the reason some civs never expand.
 * On seed 42 at 500 turns, players 0 and 2 stall at TWO cities for the whole
 * game while 1 and 3 reach 12 and 11. Instrumenting the advisor shows it doing
 * its job for the stalled pair every single run -- targets of 9 and 8 cities,
 * expansionOpportunity 0.78 and 0.75, three viable sites found, and
 * expansionExhausted never once set. They know where to go and are told to go.
 *
 * The block is downstream, in what the cities actually queue. Player 0's Ulundi
 * and Nodwengu produced Pike and Shot 26 and 14 times plus 7 Archers, and
 * player 2's Wallmapu and Temuco 23 and 10 Pike and Shot -- and between them
 * NOT ONE SETTLER in 500 turns. Players 1 and 3 spent the same period on
 * Builders and infrastructure. Both stalled civs are aggressive-leader civs
 * (Zulu and Mapuche), so military production appears to crowd the settler out
 * of the queue permanently, and the expansion advisor's signal never wins.
 *
 * The cost is not cosmetic: player 0 finished on GDP 8730 against player 3's
 * 133089, and one city against eleven. An aggressive leader that never expands
 * is not playing aggressively, it is not playing. The fix belongs in the
 * static scoreSettler() in AIController.cpp, which already receives
 * militaryUnits and treasury alongside expansionOpportunity -- the inputs
 * needed to notice that a two-city civ with forty Pike and Shot should be
 * building a settler instead. It is a balance change that will move both
 * goldens, so it is recorded rather than applied.
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
