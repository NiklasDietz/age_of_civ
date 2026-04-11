#pragma once

/**
 * @file VictoryCondition.hpp
 * @brief Civilization Score Index (CSI) victory system.
 *
 * Instead of single "first to X" win conditions, the game uses a composite
 * score index evaluated across multiple dimensions simultaneously.
 *
 * CSI Categories (each scored relative to global average):
 *   - Economic Power (GDP, trade volume, production capacity, monetary stability)
 *   - Military Strength (army size, tech advantage, territory)
 *   - Cultural Influence (great works, wonders, culture output)
 *   - Scientific Achievement (techs researched, tech lead)
 *   - Diplomatic Standing (alliances, trade partners, World Congress influence)
 *   - Quality of Life (happiness, health, population welfare)
 *   - Territorial Control (cities, improved tiles, resources)
 *   - Financial Power (bond holdings, reserve currency, trade surplus)
 *
 * Interdependence multipliers reward trade and diplomacy:
 *   - Trade network bonus (more partners = higher CSI multiplier)
 *   - Financial integration bonus (bonds, reserve currency)
 *   - Diplomatic web bonus (alliances, agreements)
 *
 * Era Evaluations every 30 turns award Victory Points to top performers.
 * Cumulative VP at game end determines the winner.
 *
 * Losing conditions (collapse) can eliminate a player:
 *   - Economic collapse: GDP < 50% of peak for 10 turns
 *   - Revolution: average loyalty < 30 for 5 turns
 *   - Conquest: capital lost + 75% cities lost
 *   - Debt spiral: sovereign default + hyperinflation simultaneously
 *
 * Game end trigger:
 *   - A player completes the "Global Integration Project" (sustained high
 *     CSI across all categories for 10 turns), OR
 *   - Turn limit reached (configurable, default 500), OR
 *   - All but one player eliminated.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

class EconomySimulation;

// ============================================================================
// CSI Categories
// ============================================================================

enum class CSICategory : uint8_t {
    EconomicPower    = 0,
    MilitaryStrength = 1,
    CulturalInfluence = 2,
    ScientificAchievement = 3,
    DiplomaticStanding = 4,
    QualityOfLife    = 5,
    TerritorialControl = 6,
    FinancialPower   = 7,

    Count
};

inline constexpr int32_t CSI_CATEGORY_COUNT = static_cast<int32_t>(CSICategory::Count);

// ============================================================================
// Victory types (kept for compatibility + losing conditions)
// ============================================================================

enum class VictoryType : uint8_t {
    None,
    Score,        ///< Highest cumulative Era VP at game end
    Integration,  ///< Completed Global Integration Project
    LastStanding, ///< All other players eliminated
    // Legacy types (can still trigger as special achievements)
    Science,
    Domination,
    Culture,
    Religion,
};

// ============================================================================
// Losing condition types
// ============================================================================

enum class CollapseType : uint8_t {
    None              = 0,
    EconomicCollapse  = 1,  ///< GDP < 50% of peak for 10 turns
    Revolution        = 2,  ///< Average loyalty < 30 for 5 turns
    Conquest          = 3,  ///< Lost capital + 75% of cities
    DebtSpiral        = 4,  ///< Default + hyperinflation simultaneously

    Count
};

// ============================================================================
// Per-player CSI and victory state (ECS component)
// ============================================================================

struct VictoryTrackerComponent {
    PlayerId owner = INVALID_PLAYER;

    // -- CSI scores per category (raw, before interdependence multiplier) --
    float categoryScores[CSI_CATEGORY_COUNT] = {};

    // -- Interdependence multipliers --
    float tradeNetworkMultiplier = 1.0f;    ///< 0.7 (hermit) to 1.2 (trade hub)
    float financialIntegrationMult = 1.0f;  ///< Bonds, reserve currency
    float diplomaticWebMult = 1.0f;         ///< Alliances, agreements

    // -- Composite CSI (all categories * multipliers) --
    float compositeCSI = 0.0f;

    // -- Era Victory Points (accumulated across era evaluations) --
    int32_t eraVictoryPoints = 0;
    int32_t erasEvaluated = 0;

    // -- Global Integration Project --
    int32_t integrationProgress = 0;  ///< Consecutive turns with all categories above threshold
    bool    integrationComplete = false;

    // -- Collapse tracking --
    CollapseType activeCollapse = CollapseType::None;
    int32_t peakGDP = 0;
    int32_t turnsGDPBelowHalf = 0;
    int32_t turnsLowLoyalty = 0;
    bool    isEliminated = false;

    // -- Legacy compatibility --
    int32_t scienceProgress = 0;
    float   totalCultureAccumulated = 0.0f;
    int32_t score = 0;  ///< Old-style score (still computed for display)
};

struct VictoryResult {
    VictoryType type   = VictoryType::None;
    PlayerId    winner = INVALID_PLAYER;
};

// ============================================================================
// CSI computation
// ============================================================================

/**
 * @brief Compute all CSI category scores for all players.
 *
 * Scores are relative to the global average: 1.0 = average, 2.0 = twice average.
 * Also computes interdependence multipliers and composite CSI.
 *
 * @param world   ECS world.
 * @param grid    Hex grid.
 * @param economy Economy simulation (for market/trade data).
 */
void computeCSI(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                const EconomySimulation& economy);

/**
 * @brief Perform an era evaluation. Awards Victory Points to top performers.
 *
 * Called every 30 turns. Awards VP:
 *   - 1st in a category: 3 VP
 *   - 2nd: 2 VP
 *   - 3rd: 1 VP
 *   - Highest composite CSI: 5 bonus VP
 *
 * @param world  ECS world.
 */
void performEraEvaluation(aoc::game::GameState& gameState);

/**
 * @brief Check for losing conditions (collapse) for all players.
 *
 * @param world  ECS world.
 */
void checkCollapseConditions(aoc::game::GameState& gameState);

/**
 * @brief Check Global Integration Project progress.
 *
 * A player must have all CSI categories above 1.5 (50% above average)
 * for 10 consecutive turns to complete the project.
 *
 * @param world  ECS world.
 */
void updateIntegrationProject(aoc::game::GameState& gameState);

/**
 * @brief Master victory check. Replaces the old checkVictoryConditions.
 *
 * @param world       ECS world.
 * @param currentTurn Current turn number.
 * @param maxTurns    Turn limit for score victory (default 500).
 * @return VictoryResult with type != None if a winner is determined.
 */
[[nodiscard]] VictoryResult checkVictoryConditions(const aoc::game::GameState& gameState,
                                                    TurnNumber currentTurn,
                                                    TurnNumber maxTurns = 500);

/**
 * @brief Update victory trackers. Call once per turn.
 *
 * Computes CSI, checks collapse, updates integration project,
 * and performs era evaluation if on an era boundary.
 */
void updateVictoryTrackers(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                           const EconomySimulation& economy, TurnNumber currentTurn);

/// Backwards-compatible overload (no economy param -- uses limited scoring).
void updateVictoryTrackers(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid);

} // namespace aoc::sim
