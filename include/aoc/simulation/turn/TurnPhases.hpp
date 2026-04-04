#pragma once

/**
 * @file TurnPhases.hpp
 * @brief Turn phase enum defining the execution order of per-turn simulation.
 */

#include <cstdint>
#include <string_view>

namespace aoc::sim {

/// Phases executed in order each turn. The order matters for determinism
/// and correct data flow (e.g., production before trade, trade before monetary).
enum class TurnPhase : uint8_t {
    PlayerInput,          ///< Human player commits actions
    AIDecisions,          ///< AI players compute and commit actions
    ResourceProduction,   ///< Production chains execute (topological DAG order)
    TradeExecution,       ///< Trade routes deliver goods
    MonetaryUpdate,       ///< Inflation, interest, fiscal policy effects
    CityGrowth,           ///< Population growth, production queue progress
    UnitMaintenance,      ///< Heal fortified units, consume upkeep
    DiplomacyDecay,       ///< Relation modifiers tick down over time
    TechProgress,         ///< Research advances toward current tech/civic
    Cleanup,              ///< End-of-turn bookkeeping

    Count
};

static constexpr uint8_t TURN_PHASE_COUNT = static_cast<uint8_t>(TurnPhase::Count);

[[nodiscard]] constexpr std::string_view turnPhaseName(TurnPhase phase) {
    switch (phase) {
        case TurnPhase::PlayerInput:        return "PlayerInput";
        case TurnPhase::AIDecisions:        return "AIDecisions";
        case TurnPhase::ResourceProduction: return "ResourceProduction";
        case TurnPhase::TradeExecution:     return "TradeExecution";
        case TurnPhase::MonetaryUpdate:     return "MonetaryUpdate";
        case TurnPhase::CityGrowth:         return "CityGrowth";
        case TurnPhase::UnitMaintenance:    return "UnitMaintenance";
        case TurnPhase::DiplomacyDecay:     return "DiplomacyDecay";
        case TurnPhase::TechProgress:       return "TechProgress";
        case TurnPhase::Cleanup:            return "Cleanup";
        default:                            return "Unknown";
    }
}

} // namespace aoc::sim
