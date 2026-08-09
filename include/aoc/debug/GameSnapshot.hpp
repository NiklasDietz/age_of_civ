#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aoc::game {
class GameState;
}
namespace aoc::sim {
class TurnManager;
}

namespace aoc::debug {

struct UnitSnapshot {
    int32_t ownerId;
    int32_t q;
    int32_t r;
    std::string typeName;
    int32_t hitPoints;
    int32_t maxHitPoints;
    int32_t movementRemaining;
    int32_t maxMovement;
    int32_t combatStrength;
    int32_t rangedStrength;
    std::string state; // "Idle" | "Moving" | "Fortified" | "Sleeping"
};

struct ProductionItemSnapshot {
    std::string name;
    float progress;
    float totalCost;
};

struct CitySnapshot {
    int32_t ownerId;
    std::string name;
    int32_t q;
    int32_t r;
    int32_t population;
    float foodSurplus;
    std::vector<ProductionItemSnapshot> productionQueue;
};

struct PlayerSnapshot {
    int32_t id;
    bool isHuman;
    int32_t civId;
    int64_t treasury;
    int64_t incomePerTurn;
    int32_t currentResearchTechId; // -1 if no research active
    float researchProgress;
    std::vector<UnitSnapshot> units;
    std::vector<CitySnapshot> cities;
};

struct GameSnapshot {
    int32_t turnNumber;
    std::string phase; // e.g. "PlayerInput", "AIDecisions"
    int32_t activePlayerId;
    std::vector<PlayerSnapshot> players;
};

/// Build a snapshot from the current game state. Pure function — no I/O,
/// no side effects. Safe to call in unit tests with a hand-built GameState.
GameSnapshot buildGameSnapshot(const aoc::game::GameState&, const aoc::sim::TurnManager&);

/// Serialize a full snapshot to JSON.
std::string toJson(const GameSnapshot&);
/// Serialize one player to JSON.
std::string toJson(const PlayerSnapshot&);
/// Serialize a unit list to a JSON array.
std::string toJson(const std::vector<UnitSnapshot>&);
/// Serialize a city list to a JSON array.
std::string toJson(const std::vector<CitySnapshot>&);

} // namespace aoc::debug
