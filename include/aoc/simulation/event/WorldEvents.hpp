#pragma once

/**
 * @file WorldEvents.hpp
 * @brief Narrative world events with player choices.
 *
 * Events trigger based on game state conditions and present the player
 * with a choice that has trade-offs. This creates emergent stories.
 *
 * Event structure:
 *   - Trigger condition (checked each turn)
 *   - Title and description text
 *   - 2-3 choices, each with different consequences
 *
 * Events are one-shot per game (each can only fire once).
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <cstdint>
#include <string_view>

namespace aoc::game { class GameState; }

namespace aoc::sim {

enum class WorldEventId : uint8_t {
    Plague,              ///< Disease outbreak in capital
    GoldRush,            ///< Gold discovered in territory
    ArtisticRenaissance, ///< Cultural movement begins
    FamineWarning,       ///< Food shortage looming
    ScientificBreakthrough,///< Major discovery
    TradeDisruption,     ///< Piracy/storm disrupts trade routes
    ReligiousSchism,     ///< Religious division in your cities
    MigrantWave,         ///< Refugees from neighboring war
    IndustrialAccident,  ///< Factory explosion
    DiplomaticIncident,  ///< Spy caught / border skirmish
    EconomicBoom,        ///< Unexpected prosperity
    VolcanicFertility,   ///< Post-eruption fertile soil

    Count
};

struct EventChoice {
    std::string_view name;
    std::string_view description;
    // Effects encoded as simple modifiers
    int32_t goldChange = 0;
    int32_t productionChange = 0;   ///< % modifier for N turns
    int32_t scienceChange = 0;
    int32_t cultureChange = 0;
    float   happinessChange = 0.0f;
    float   loyaltyChange = 0.0f;
    int32_t populationChange = 0;   ///< Direct population gain/loss in capital
    int32_t effectDuration = 0;     ///< Turns the effect lasts (0 = instant)
};

struct WorldEventDef {
    WorldEventId     id;
    std::string_view title;
    std::string_view description;
    EventChoice      choices[3];
    int32_t          choiceCount;
};

/// Get all event definitions.
[[nodiscard]] const WorldEventDef& worldEventDef(WorldEventId id);

/// Total number of events.
inline constexpr int32_t WORLD_EVENT_COUNT = static_cast<int32_t>(WorldEventId::Count);

/// Per-player event state (tracks which events have fired).
struct PlayerEventComponent {
    PlayerId owner = INVALID_PLAYER;
    bool     eventFired[WORLD_EVENT_COUNT] = {};
    WorldEventId pendingEvent = static_cast<WorldEventId>(255); ///< 255 = none
    int32_t  pendingChoice = -1; ///< Player's selected choice (-1 = not chosen yet)

    // Active effect from a previous choice
    int32_t  activeProductionMod = 0;
    int32_t  activeScienceMod = 0;
    int32_t  activeEffectTurns = 0;
};

/**
 * @brief Check event triggers and generate pending events.
 *
 * Scans game state for conditions that trigger each event type.
 * If triggered and not already fired, sets it as pending for the player.
 *
 * @param world       ECS world.
 * @param player      Player to check.
 * @param turnNumber  Current turn.
 */
void checkWorldEvents(aoc::game::GameState& gameState, PlayerId player, int32_t turnNumber);

/**
 * @brief Apply the player's choice for a pending event.
 *
 * @param world   ECS world.
 * @param player  Player making the choice.
 * @param choice  Index of the chosen option (0-2).
 */
[[nodiscard]] ErrorCode resolveWorldEvent(aoc::game::GameState& gameState,
                                          PlayerId player, int32_t choice);

/**
 * @brief Tick active event effects (decrement durations, remove expired).
 */
void tickWorldEvents(aoc::game::GameState& gameState);

} // namespace aoc::sim
