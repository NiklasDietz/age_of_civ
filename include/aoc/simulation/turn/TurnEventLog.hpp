#pragma once

/**
 * @file TurnEventLog.hpp
 * @brief Mid-turn event logging for ML training data.
 *
 * During a turn, many sub-events occur: combat, city founding, tech research,
 * war declarations, trade establishment, etc. A human player sees and reacts
 * to these events in real-time. For ML training, we need to capture this
 * temporal granularity so the model can learn:
 *   - "I lost because I didn't react to that war declaration fast enough"
 *   - "Building a settler right after the enemy moved away was optimal"
 *
 * Events are collected per-turn, then flushed to CSV after the turn completes.
 * Each event has a sub-step index within the turn, an event type, the acting
 * player, and type-specific data fields.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aoc::sim {

/// Event types that occur within a single turn.
enum class TurnEventType : uint8_t {
    CombatResolved,      ///< A melee or ranged combat was resolved
    CityFounded,         ///< A new city was founded
    TechResearched,      ///< A technology was completed
    CivicCompleted,      ///< A civic was completed
    UnitProduced,        ///< A unit finished production
    BuildingCompleted,   ///< A building was completed
    WarDeclared,         ///< War was declared between two players
    PeaceMade,           ///< Peace treaty signed
    TradeEstablished,    ///< New trade route opened
    PlayersMet,          ///< Two players discovered each other
    UnitKilled,          ///< A unit was destroyed
    CityLost,            ///< A city was conquered or razed
    GoldPurchase,        ///< Item purchased with gold
    RevolutionAchieved,  ///< Industrial revolution milestone
    NaturalDisaster,     ///< Volcano, flood, etc.
};

/// A single mid-turn event.
struct TurnEvent {
    int32_t       subStep;    ///< Order within the turn (0, 1, 2, ...)
    TurnEventType type;
    PlayerId      player;     ///< Primary player involved
    PlayerId      otherPlayer = INVALID_PLAYER; ///< Secondary player (for war/peace/meet)
    int32_t       value1 = 0; ///< Event-specific (e.g., damage dealt, gold spent)
    int32_t       value2 = 0; ///< Event-specific (e.g., unit type, building id)
    std::string   detail;     ///< Human-readable description
};

/**
 * @brief Accumulates events within a single turn, then flushes to CSV.
 *
 * Thread-safe: all events are appended sequentially during turn processing
 * (which runs single-threaded). Flushed after the turn completes.
 */
class TurnEventLog {
public:
    /// Clear all events (call at start of each turn).
    void clear() {
        this->m_events.clear();
        this->m_nextSubStep = 0;
    }

    /// Record an event. Sub-step is auto-incremented.
    void record(TurnEventType type, PlayerId player, PlayerId otherPlayer = INVALID_PLAYER,
                int32_t value1 = 0, int32_t value2 = 0, std::string detail = "") {
        TurnEvent event{};
        event.subStep    = this->m_nextSubStep++;
        event.type       = type;
        event.player     = player;
        event.otherPlayer = otherPlayer;
        event.value1     = value1;
        event.value2     = value2;
        event.detail     = std::move(detail);
        this->m_events.push_back(std::move(event));
    }

    /// All events recorded this turn.
    [[nodiscard]] const std::vector<TurnEvent>& events() const { return this->m_events; }

    /// Number of events this turn.
    [[nodiscard]] int32_t eventCount() const {
        return static_cast<int32_t>(this->m_events.size());
    }

    /// Event type name for CSV output.
    [[nodiscard]] static const char* eventTypeName(TurnEventType type) {
        switch (type) {
            case TurnEventType::CombatResolved:     return "Combat";
            case TurnEventType::CityFounded:        return "CityFounded";
            case TurnEventType::TechResearched:      return "TechResearched";
            case TurnEventType::CivicCompleted:      return "CivicCompleted";
            case TurnEventType::UnitProduced:        return "UnitProduced";
            case TurnEventType::BuildingCompleted:   return "BuildingCompleted";
            case TurnEventType::WarDeclared:         return "WarDeclared";
            case TurnEventType::PeaceMade:           return "PeaceMade";
            case TurnEventType::TradeEstablished:    return "TradeEstablished";
            case TurnEventType::PlayersMet:          return "PlayersMet";
            case TurnEventType::UnitKilled:          return "UnitKilled";
            case TurnEventType::CityLost:            return "CityLost";
            case TurnEventType::GoldPurchase:        return "GoldPurchase";
            case TurnEventType::RevolutionAchieved:  return "RevolutionAchieved";
            case TurnEventType::NaturalDisaster:     return "NaturalDisaster";
        }
        return "Unknown";
    }

private:
    std::vector<TurnEvent> m_events;
    int32_t m_nextSubStep = 0;
};

} // namespace aoc::sim
