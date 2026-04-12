#pragma once

/**
 * @file CityStateQuest.hpp
 * @brief City-state quest system and expanded city-state definitions.
 *
 * City-states offer quests to nearby civilizations. Completing a quest
 * awards 2 envoys with that city-state. Quests are generated based on
 * the city-state type and the current game state.
 *
 * 24 city-states (8 types x 3 each) with unique suzerain bonuses.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <cstdint>
#include <string_view>

namespace aoc::game { class GameState; }

namespace aoc::sim {

// ============================================================================
// Quest types
// ============================================================================

enum class CityStateQuestType : uint8_t {
    BuildWonder,       ///< Build any wonder
    TrainUnit,         ///< Train a unit of a specific class
    ResearchTech,      ///< Research a specific tech
    SendTradeRoute,    ///< Establish a trade route with the city-state
    ConvertToReligion, ///< Convert the city-state to your religion
    DefeatBarbarian,   ///< Kill a barbarian unit near the city-state

    Count
};

struct CityStateQuest {
    CityStateQuestType type = CityStateQuestType::BuildWonder;
    PlayerId assignedTo = INVALID_PLAYER;
    bool     isCompleted = false;
    int32_t  turnsRemaining = 30;  ///< Quest expires after this many turns
    int32_t  envoyReward = 2;      ///< Envoys awarded on completion
};

// ============================================================================
// Expanded city-state definitions (24 total)
// ============================================================================

struct CityStateFullDef {
    uint8_t          id;
    std::string_view name;
    uint8_t          type;     ///< CityStateType value from CityState.hpp
    std::string_view suzerainBonus;
};

/// 24 city-states with historical names and types.
/// Types: 0=Militaristic, 1=Scientific, 2=Cultural, 3=Trade, 4=Religious, 5=Industrial
inline constexpr CityStateFullDef CITY_STATE_FULL_DEFS[] = {
    // Militaristic (0)
    { 0, "Kabul",      0, "+5 combat strength for units near this city-state"},
    { 1, "Valletta",   0, "+2 envoy defenders when at war"},
    { 2, "Akkad",      0, "Free melee unit when suzerainty gained"},
    // Scientific (1)
    { 3, "Geneva",     1, "+15% science in capital"},
    { 4, "Hattusa",    1, "Free copy of each strategic resource you have revealed"},
    { 5, "Bologna",    1, "+1 Great Scientist point per turn"},
    // Cultural (2)
    { 6, "Kumasi",     2, "+2 culture for trade routes to this city-state"},
    { 7, "Nan Madol",  2, "+2 culture for each coastal district"},
    { 8, "Vilnius",    2, "+50% Great Person points during Golden Age"},
    // Trade (3)
    { 9, "Zanzibar",   3, "+4 gold per trade route"},
    {10, "Lisbon",     3, "Traders can't be plundered"},
    {11, "Hunza",      3, "+1 trade route capacity"},
    // Religious (4)
    {12, "Jerusalem",  4, "Automatic conversion to your religion"},
    {13, "Yerevan",    4, "Choose apostle promotions"},
    {14, "Chinguetti", 4, "+2 faith per turn per envoy"},
    // Industrial (5)
    {15, "Brussels",   5, "+15% production toward wonders"},
    {16, "Singapore",  5, "+2 production in all cities"},
    {17, "Hong Kong",  5, "+20% production for projects"},
    // Additional mixed types
    {18, "Carthage",   3, "Free trader unit when suzerainty gained"},
    {19, "Mohenjo-Daro", 1, "Full housing from freshwater for all cities"},
    {20, "Preslav",    0, "Encampment buildings provide +1 Great General point"},
    {21, "Armagh",     4, "Builders can construct monasteries"},
    {22, "Auckland",   5, "Shallow water tiles provide +1 production"},
    {23, "Antananarivo", 2, "+2% culture per active trade partner"},
};

inline constexpr int32_t CITY_STATE_FULL_COUNT = 24;

/**
 * @brief Generate a quest for a city-state aimed at a nearby player.
 *
 * @param cityStateIndex  Index into GameState::cityStates().
 */
void generateCityStateQuest(aoc::game::GameState& gameState,
                            std::size_t cityStateIndex,
                            PlayerId targetPlayer);

/**
 * @brief Check quest completion for all city-states.
 */
void checkCityStateQuests(aoc::game::GameState& gameState);

} // namespace aoc::sim
