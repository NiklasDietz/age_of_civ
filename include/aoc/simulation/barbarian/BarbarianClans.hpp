#pragma once

/**
 * @file BarbarianClans.hpp
 * @brief Barbarian clan system: scaling difficulty, named clans, diplomacy.
 *
 * Barbarian encampments belong to named clans. Clans can be:
 *   - Fought: destroy encampments for gold/XP reward
 *   - Bribed: pay gold to make them passive for 20 turns
 *   - Allied: pay gold + luxury resource, they become a city-state
 *   - Hired: pay gold, they attack a target player for 10 turns
 *
 * Scaling difficulty:
 *   - Turn 1-30: Warriors only
 *   - Turn 31-60: Spearmen, Archers
 *   - Turn 61-100: Swordsmen, Horsemen
 *   - Turn 100+: Era-appropriate units matching leading player's tech
 *
 * Clan types affect behavior:
 *   - Raiders: prioritize attacking improvements and trade routes
 *   - Warband: prioritize attacking units and cities
 *   - Pirates: spawn on coast, attack naval trade
 *   - Nomads: spawn on plains/grassland, move faster
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

// ============================================================================
// Clan types and definitions
// ============================================================================

enum class BarbarianClanType : uint8_t {
    Raiders,  ///< Target improvements and trade routes
    Warband,  ///< Target units and cities
    Pirates,  ///< Coastal, target naval trade
    Nomads,   ///< Fast movement, plains/grassland

    Count
};

struct BarbarianClanDef {
    uint8_t              id;
    std::string_view     name;
    BarbarianClanType    type;
};

inline constexpr BarbarianClanDef BARBARIAN_CLAN_DEFS[] = {
    { 0, "Wolf Clan",     BarbarianClanType::Warband},
    { 1, "Sand Raiders",  BarbarianClanType::Raiders},
    { 2, "Sea Wolves",    BarbarianClanType::Pirates},
    { 3, "Horse Lords",   BarbarianClanType::Nomads},
    { 4, "Iron Horde",    BarbarianClanType::Warband},
    { 5, "Shadow Blades", BarbarianClanType::Raiders},
    { 6, "Storm Fleet",   BarbarianClanType::Pirates},
    { 7, "Wind Riders",   BarbarianClanType::Nomads},
};

inline constexpr int32_t BARBARIAN_CLAN_COUNT = 8;

// ============================================================================
// Extended encampment component
// ============================================================================

struct BarbarianClanComponent {
    uint8_t           clanId = 0;
    BarbarianClanType clanType = BarbarianClanType::Warband;
    int32_t           strength = 1;       ///< Scales with game turn
    bool              isBribed = false;    ///< Currently passive (paid off)
    int32_t           bribeTurnsLeft = 0;
    PlayerId          hiredBy = INVALID_PLAYER; ///< If hired, attack this player's enemies
    PlayerId          hiredTarget = INVALID_PLAYER;
    int32_t           hireTurnsLeft = 0;
};

// ============================================================================
// Clan interactions
// ============================================================================

/// Bribe cost scales with clan strength.
[[nodiscard]] constexpr int32_t bribeCost(int32_t clanStrength) {
    return 50 + clanStrength * 25;
}

/// Hire cost to make clan attack a target.
[[nodiscard]] constexpr int32_t hireCost(int32_t clanStrength) {
    return 100 + clanStrength * 40;
}

/// Cost to convert clan to city-state (requires luxury resource + gold).
[[nodiscard]] constexpr int32_t convertToCityStateCost(int32_t clanStrength) {
    return 200 + clanStrength * 50;
}

/**
 * @brief Bribe a barbarian clan to be passive for 20 turns.
 *
 * @param clanIndex  Index into GameState::barbarianClans().
 */
[[nodiscard]] ErrorCode bribeClan(aoc::game::GameState& gameState,
                                   std::size_t clanIndex,
                                   PlayerId player);

/**
 * @brief Hire a barbarian clan to attack a target player for 10 turns.
 *
 * @param clanIndex  Index into GameState::barbarianClans().
 */
[[nodiscard]] ErrorCode hireClan(aoc::game::GameState& gameState,
                                  std::size_t clanIndex,
                                  PlayerId hirer,
                                  PlayerId target);

/**
 * @brief Determine what unit type a barbarian encampment should spawn
 * based on the current turn number (difficulty scaling).
 *
 * H5.7: leadingEra floors the unit choice. Pass -1 to disable era scaling.
 * When leadingEra >= given threshold, barbs spawn Tanks/Infantry/etc. even
 * if turn count alone would keep them at Musketmen.
 */
[[nodiscard]] UnitTypeId barbarianSpawnUnit(int32_t turnNumber, int32_t leadingEra = -1);

/**
 * @brief Gold and XP reward for destroying a barbarian encampment.
 */
[[nodiscard]] int32_t encampmentDestroyReward(int32_t clanStrength);

} // namespace aoc::sim
