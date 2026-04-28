#pragma once

/// @file CityState.hpp
/// @brief City-state definitions, ECS components, quest state, and per-turn
///        processing. All city-state code shares this header — a separate
///        CityStateQuest.hpp used to exist but was folded in to avoid
///        include-order fragility between the component (which embeds the
///        quest struct) and the quest definitions.

#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/map/HexCoord.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace aoc::game {
class GameState;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

// ============================================================================
// Specializations, definitions, and id ranges
// ============================================================================

/// City-state specialization type.
enum class CityStateType : uint8_t {
    Militaristic,
    Scientific,
    Cultural,
    Trade,
    Religious,
    Industrial,
    Count
};

/// Static definition for a city-state.
struct CityStateDef {
    uint8_t        id;
    std::string_view name;
    CityStateType  type;
};

/// Total number of defined city-states.
inline constexpr uint8_t CITY_STATE_COUNT = 8;

/// All city-state definitions.
inline constexpr std::array<CityStateDef, CITY_STATE_COUNT> CITY_STATE_DEFS = {{
    {0, "Kabul",     CityStateType::Militaristic},
    {1, "Valletta",  CityStateType::Militaristic},
    {2, "Geneva",    CityStateType::Scientific},
    {3, "Seoul",     CityStateType::Scientific},
    {4, "Kumasi",    CityStateType::Cultural},
    {5, "Zanzibar",  CityStateType::Cultural},
    {6, "Lisbon",    CityStateType::Trade},
    {7, "Singapore", CityStateType::Trade},
}};

/// Special player ID range for city-states (CITY_STATE_PLAYER_BASE..+).
/// GameState::player(id) dispatches IDs >= base into the CS slot vector.
inline constexpr PlayerId CITY_STATE_PLAYER_BASE = 200;

// ============================================================================
// Quest types
// ============================================================================

enum class CityStateQuestType : uint8_t {
    BuildWonder,        ///< Build any wonder
    TrainUnit,          ///< Train a unit of a specific class
    ResearchTech,       ///< Research a specific tech
    SendTradeRoute,     ///< Establish a trade route with the city-state
    ConvertToReligion,  ///< Convert the city-state to your religion
    DefeatBarbarian,    ///< Kill a barbarian unit near the city-state
    Count
};

/// One active quest per city-state. Regenerated after completion or timeout.
struct CityStateQuest {
    CityStateQuestType type = CityStateQuestType::BuildWonder;
    PlayerId assignedTo     = INVALID_PLAYER;
    bool     isCompleted    = false;
    bool     isActive       = false;   ///< False if slot empty / consumed
    int32_t  turnsRemaining = 30;      ///< Expires after this many turns
    int32_t  envoyReward    = 2;       ///< Envoys awarded on completion
    /// Snapshot of the assignee's objective-relevant counter at generation
    /// time; completion compares live counter vs snapshot.
    int32_t  snapshot       = 0;
};

/// Per-assignee streak: consecutive quests completed by the same player
/// scale the reward: streak 0 → 2 envoys, 1 → 3, 2 → 4, 3+ → 5 + 25 gold.
struct CityStateQuestStreak {
    PlayerId player = INVALID_PLAYER;
    int32_t  streak = 0;
};

// ============================================================================
// Component
// ============================================================================

static_assert(MAX_PLAYERS <= 32,
              "CityStateComponent::metMask is a uint32_t bitfield; "
              "MAX_PLAYERS must stay <= 32.");

/// ECS component for a city-state entity.
struct CityStateComponent {
    uint8_t       defId;
    CityStateType type;
    hex::AxialCoord location;
    std::array<int8_t, MAX_PLAYERS> envoys{}; ///< Envoy count per player
    PlayerId      suzerain = INVALID_PLAYER;

    /// Bit i set = player i has met this city-state.
    uint32_t      metMask = 0;

    /// One active quest per city-state. Completing it awards envoys to the
    /// `assignedTo` player. Regenerated after completion or timeout.
    CityStateQuest activeQuest{};

    /// Quest-completion streak for the most recent assignee. Each
    /// consecutive completion increases envoy reward + adds gold bonus.
    CityStateQuestStreak questStreak{};

    /// Player whose forces are currently levied (military units
    /// temporarily granted by the suzerain). INVALID_PLAYER = none.
    PlayerId      levyPlayer      = INVALID_PLAYER;
    int32_t       levyTurnsLeft   = 0;

    /// Turns since last bully attempt — bully is rate-limited.
    int32_t       turnsSinceBully = 0;

    [[nodiscard]] bool hasMet(PlayerId p) const {
        if (p >= MAX_PLAYERS) { return false; }
        return (this->metMask & (1u << p)) != 0;
    }
    void setMet(PlayerId p) {
        if (p >= MAX_PLAYERS) { return; }
        this->metMask |= (1u << p);
    }

    /// Add envoys to a given player, clamping at 127 to fit int8_t.
    void addEnvoys(PlayerId p, int32_t amount) {
        if (p >= MAX_PLAYERS) { return; }
        const int32_t updated =
            static_cast<int32_t>(this->envoys[p]) + amount;
        if (updated < 0)  { this->envoys[p] = 0;   return; }
        if (updated > 127){ this->envoys[p] = 127; return; }
        this->envoys[p] = static_cast<int8_t>(updated);
    }

    /// Player with the most envoys (>=3). INVALID_PLAYER if none qualifies.
    [[nodiscard]] PlayerId computeSuzerain() const {
        PlayerId best = INVALID_PLAYER;
        int8_t bestCount = 0;
        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            if (this->envoys[i] > bestCount) {
                bestCount = this->envoys[i];
                best = static_cast<PlayerId>(i);
            }
        }
        if (bestCount < 3) { return INVALID_PLAYER; }
        return best;
    }
};

// ============================================================================
// Turn processing
// ============================================================================

/// Spawn city-states during map generation.
void spawnCityStates(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                      int32_t count, aoc::Random& rng);

/// Apply per-turn suzerain/envoy bonuses for a given player.
void processCityStateBonuses(aoc::game::GameState& gameState, PlayerId player);

/// Per-turn city-state diplomacy tick: meet-check, passive envoy accrual,
/// suzerain recomputation, levy expiry. Server-authoritative.
void processCityStateDiplomacy(aoc::game::GameState& gameState,
                                const aoc::map::HexGrid& grid,
                                int32_t currentTurn);

/// Lightweight CS AI. Queues defender units when production empty and
/// advances the per-turn production queue for each city-state (CS players
/// are not in allPlayers, so the standard tick never runs for them).
/// Explicitly never queues settlers/wonders/districts, so CS cannot spread.
void processCityStateAI(aoc::game::GameState& gameState,
                          const aoc::map::HexGrid& grid);

/// Generate a fresh quest for city-state `cityStateIndex`, aimed at
/// `targetPlayer`. Called by checkCityStateQuests when a slot is empty.
void generateCityStateQuest(aoc::game::GameState& gameState,
                              std::size_t cityStateIndex,
                              PlayerId targetPlayer);

/// Tick all quests: generate when empty, award envoys on completion, expire.
void checkCityStateQuests(aoc::game::GameState& gameState);

// ============================================================================
// Player actions
// ============================================================================

/// `bullyCityState`: major player coerces CS for gold; costs envoys at the
/// CS and generates a BulliedCityState grievance with every other major that
/// has envoys there. Blocked if CS has another suzerain (protectorate).
[[nodiscard]] bool bullyCityState(aoc::game::GameState& gameState,
                                    PlayerId player,
                                    std::size_t cityStateIndex);

/// `levyCityStateMilitary`: suzerain temporarily commands CS military units.
/// Costs gold. Reverts after N turns.
[[nodiscard]] bool levyCityStateMilitary(aoc::game::GameState& gameState,
                                           PlayerId player,
                                           std::size_t cityStateIndex);

} // namespace aoc::sim
