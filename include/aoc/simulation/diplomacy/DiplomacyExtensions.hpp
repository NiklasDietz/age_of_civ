#pragma once

/**
 * @file DiplomacyExtensions.hpp
 * @brief Era dedications, emergency system, and expanded World Congress.
 *
 * === Era Dedications ===
 * At the start of each era (every 30 turns), players choose a dedication
 * that provides a specific bonus for the era's duration:
 *   - Free Inquiry: +100% Great Scientist points
 *   - Pen/Brush/Voice: +100% Great Artist/Writer/Musician points
 *   - Exodus of the Evangelists: +2 religious spread charges
 *   - Monumentality: Builders/Settlers cost 30% less (Golden Age only)
 *   - Reform the Coinage: +4 gold per trade route
 *   - Heartbeat of Steam: +10% production for Industrial buildings
 *   - To Arms!: +4 combat strength for all military units
 *   - Wish You Were Here: +100% Tourism (Golden Age only)
 *
 * === Emergency System ===
 * When a player becomes too dominant (conquers a capital, uses nuclear weapons,
 * converts a holy city, or achieves a wonder monopoly), other players can
 * form an Emergency alliance to counter them:
 *   - Military Emergency: Target must defend against joint declaration
 *   - Religious Emergency: Target religion has been too aggressive
 *   - Nuclear Emergency: Response to nuclear weapon use
 * Emergencies last 30 turns. If the target is stopped, participants get bonus VP.
 *
 * === Expanded World Congress Resolutions (20+) ===
 * Additional resolution types beyond the base 6.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string_view>

namespace aoc::game { class GameState; }

namespace aoc::sim {

// ============================================================================
// Era Dedications
// ============================================================================

enum class EraDedication : uint8_t {
    None,
    FreeInquiry,       ///< +100% Great Scientist points
    PenBrushVoice,     ///< +100% Great Artist points
    ExodusEvangelists, ///< +2 religious spread charges
    Monumentality,     ///< Builders/Settlers -30% cost (Golden Age only)
    ReformCoinage,     ///< +4 gold per trade route
    HeartbeatSteam,    ///< +10% industrial production
    ToArms,            ///< +4 combat strength
    WishYouWereHere,   ///< +100% Tourism (Golden Age only)

    Count
};

struct EraDedicationDef {
    EraDedication    type;
    std::string_view name;
    std::string_view description;
    bool             goldenAgeOnly;  ///< Only available during Golden Ages
};

inline constexpr EraDedicationDef ERA_DEDICATION_DEFS[] = {
    {EraDedication::None,              "None",                "No dedication chosen.", false},
    {EraDedication::FreeInquiry,       "Free Inquiry",        "+100% Great Scientist points", false},
    {EraDedication::PenBrushVoice,     "Pen, Brush & Voice",  "+100% Great Artist points", false},
    {EraDedication::ExodusEvangelists, "Exodus of Evangelists","+2 religious spread charges", false},
    {EraDedication::Monumentality,     "Monumentality",       "Builders/Settlers -30% cost", true},
    {EraDedication::ReformCoinage,     "Reform the Coinage",  "+4 gold per trade route", false},
    {EraDedication::HeartbeatSteam,    "Heartbeat of Steam",  "+10% industrial production", false},
    {EraDedication::ToArms,            "To Arms!",            "+4 combat strength for all units", false},
    {EraDedication::WishYouWereHere,   "Wish You Were Here",  "+100% Tourism", true},
};

/// Per-player era dedication state (stored in VictoryTracker or separate component).
struct PlayerEraDedicationComponent {
    PlayerId      owner = INVALID_PLAYER;
    EraDedication activeDedication = EraDedication::None;
    int32_t       turnsRemaining = 0;
};

// ============================================================================
// Emergency System
// ============================================================================

enum class EmergencyType : uint8_t {
    Military,    ///< Triggered by conquering an original capital
    Religious,   ///< Triggered by converting 3+ cities to foreign religion in 10 turns
    Nuclear,     ///< Triggered by nuclear weapon use

    Count
};

struct EmergencyDef {
    EmergencyType    type;
    std::string_view name;
    int32_t          duration;       ///< Turns the emergency lasts
    int32_t          vpReward;       ///< VP for participants if emergency succeeds
};

inline constexpr EmergencyDef EMERGENCY_DEFS[] = {
    {EmergencyType::Military,  "Military Emergency",  30, 10},
    {EmergencyType::Religious, "Religious Emergency",  30,  5},
    {EmergencyType::Nuclear,   "Nuclear Emergency",   30, 15},
};

/// Active emergency state.
struct ActiveEmergency {
    EmergencyType type;
    PlayerId      target;               ///< The player being targeted
    PlayerId      participants[8] = {}; ///< Players who joined the emergency
    int32_t       participantCount = 0;
    int32_t       turnsRemaining = 0;
    bool          resolved = false;     ///< True if the target was successfully stopped
};

/// Global emergency tracker.
struct GlobalEmergencyTracker {
    ActiveEmergency emergencies[4] = {};
    int32_t emergencyCount = 0;
};

/**
 * @brief Trigger an emergency against a player.
 */
void triggerEmergency(aoc::game::GameState& gameState, GlobalEmergencyTracker& tracker,
                      EmergencyType type, PlayerId target);

/**
 * @brief Process emergencies per turn (tick timers, check resolution).
 */
void processEmergencies(aoc::game::GameState& gameState, GlobalEmergencyTracker& tracker);

// ============================================================================
// Extended World Congress Resolutions
// ============================================================================

/// Additional resolution types (extending the base 6 from WorldCongress).
enum class ExtendedResolution : uint8_t {
    // Base 6 already exist in WorldCongress.hpp
    // These are additional:
    CurrencyStabilityPact = 6,  ///< Prohibits currency devaluation
    FreedomOfNavigation   = 7,  ///< All naval units get +1 movement
    OpenSkies             = 8,  ///< Air units can fly over neutral territory
    GlobalHealthInitiative = 9, ///< All cities get +1 amenity
    SpaceRace             = 10, ///< +10% science for top 3 GDP players
    DeforestationBan      = 11, ///< Cannot remove forests/jungles
    FairTrade             = 12, ///< Tariffs capped at 10%
    IntellectualProperty  = 13, ///< Tech spillover from trade reduced 50%
    CollectiveSecurity    = 14, ///< All players must join military emergencies
    ClimateAccord         = 15, ///< CO2 limits, penalties for exceeding
    NonProliferation      = 16, ///< Nuclear weapons banned (violators = max grievance)
    HumanRights           = 17, ///< Cities with < 0 amenities trigger sanctions
    FreedomOfPress        = 18, ///< +25% espionage success rate
    GlobalMinimumTax      = 19, ///< Tax rate floor of 10% for all players
    MaritimeLaw           = 20, ///< Piracy penalties (trade route plundering costs more)

    Count
};

} // namespace aoc::sim
