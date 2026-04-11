#pragma once

/**
 * @file CommunicationSpeed.hpp
 * @brief Communication speed limits empire cohesion.
 *
 * The fundamental constraint on empire size is how fast orders,
 * information, and cultural influence can travel from the capital to
 * the periphery. This creates a soft cap on empire size that scales
 * with communication technology:
 *
 * Communication tiers (tiles per turn that messages can travel):
 *   1. Foot Messenger:   1 tile/turn (Ancient, no tech)
 *   2. Horse Relay:      3 tiles/turn (Horseback Riding tech)
 *   3. Road Network:     5 tiles/turn (requires Road infrastructure)
 *   4. Railway Mail:    10 tiles/turn (requires Railway)
 *   5. Telegraph:       Instant to connected cities (Electricity tech)
 *   6. Radio:           Instant to all cities (Radio tech)
 *   7. Internet:        Instant + bonus loyalty/science (Computers tech)
 *
 * "Communication distance" from capital is measured in message-turns:
 *   commDist = hexDistance / commSpeed
 *
 * Effects of high communication distance:
 *   - Loyalty penalty: -2 per message-turn from capital
 *   - Corruption increase: +1% per message-turn
 *   - Production penalty: -3% per message-turn (coordination overhead)
 *   - Science penalty: -2% per message-turn (knowledge flows slowly)
 *
 * Mitigations (how to have a large low-tech empire):
 *   - Regional capitals: build a Governor's Palace in a distant city.
 *     That city becomes a regional communication hub, reducing effective
 *     distance for nearby cities. Costs gold maintenance.
 *   - Military garrisons: units stationed in a city reduce loyalty penalty
 *     by 50% (rule by force -- historically common).
 *   - Road/railway network: infrastructure between cities directly
 *     increases communication speed along that path.
 *   - Cultural homogeneity: cities of the same religion/culture have
 *     reduced communication penalty (shared values = less oversight needed).
 *
 * This means:
 *   - A small ancient empire (4-5 cities) works fine on foot messengers.
 *   - A 10-city classical empire needs horse relay or roads.
 *   - A 20-city industrial empire needs railways or telegraph.
 *   - A global modern empire needs radio/internet.
 *   - BUT a large ancient empire IS possible -- just expensive in loyalty,
 *     garrisons, and regional capitals.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

// ============================================================================
// Communication tiers
// ============================================================================

enum class CommTier : uint8_t {
    FootMessenger = 0,  ///< 1 tile/turn
    HorseRelay    = 1,  ///< 3 tiles/turn
    RoadNetwork   = 2,  ///< 5 tiles/turn
    RailwayMail   = 3,  ///< 10 tiles/turn
    Telegraph     = 4,  ///< Instant to connected cities
    Radio         = 5,  ///< Instant to all cities
    Internet      = 6,  ///< Instant + bonuses

    Count
};

/// Tiles per turn that messages travel at each tier.
/// Tiers 4+ are "instant" (represented as a very high number).
[[nodiscard]] constexpr int32_t commSpeedTilesPerTurn(CommTier tier) {
    switch (tier) {
        case CommTier::FootMessenger: return 1;
        case CommTier::HorseRelay:    return 3;
        case CommTier::RoadNetwork:   return 5;
        case CommTier::RailwayMail:   return 10;
        case CommTier::Telegraph:     return 100;  // Effectively instant
        case CommTier::Radio:         return 200;
        case CommTier::Internet:      return 500;
        default:                      return 1;
    }
}

/// Required tech for each communication tier (TechId value, INVALID = always available).
[[nodiscard]] constexpr TechId commTierRequiredTech(CommTier tier) {
    switch (tier) {
        case CommTier::FootMessenger: return TechId{};   // Always available
        case CommTier::HorseRelay:    return TechId{3};  // Horseback Riding
        case CommTier::RoadNetwork:   return TechId{1};  // Masonry (roads)
        case CommTier::RailwayMail:   return TechId{7};  // Steel (railways)
        case CommTier::Telegraph:     return TechId{10}; // Electricity
        case CommTier::Radio:         return TechId{14}; // Radio
        case CommTier::Internet:      return TechId{16}; // Computers
        default:                      return TechId{};
    }
}

// ============================================================================
// Per-player communication state (ECS component)
// ============================================================================

struct PlayerCommunicationComponent {
    PlayerId owner = INVALID_PLAYER;
    CommTier currentTier = CommTier::FootMessenger;

    /// Per-city communication distance in "message-turns" from capital.
    /// Stored as a flat map: cityEntityIndex -> commDistance.
    /// Updated once per turn.
    static constexpr int32_t MAX_TRACKED_CITIES = 64;
    struct CityCommData {
        EntityId cityEntity = NULL_ENTITY;
        int32_t  hexDistance = 0;     ///< Raw hex distance from capital
        float    commDistance = 0.0f; ///< Effective message-turns (hexDist / commSpeed)
        bool     isRegionalCapital = false;
        bool     hasGarrison = false;
    };
    CityCommData cities[MAX_TRACKED_CITIES] = {};
    int32_t cityCount = 0;
};

// ============================================================================
// Communication penalties
// ============================================================================

/// Loyalty penalty per message-turn of communication distance.
constexpr float COMM_LOYALTY_PENALTY_PER_TURN = 2.0f;

/// Corruption increase per message-turn (fraction).
constexpr float COMM_CORRUPTION_PER_TURN = 0.01f;

/// Production penalty per message-turn (fraction).
constexpr float COMM_PRODUCTION_PENALTY_PER_TURN = 0.03f;

/// Science penalty per message-turn (fraction).
constexpr float COMM_SCIENCE_PENALTY_PER_TURN = 0.02f;

/// Garrison reduces loyalty penalty by this fraction.
constexpr float GARRISON_LOYALTY_REDUCTION = 0.50f;

/// Regional capital reduces effective communication distance to nearby cities.
/// Cities within 5 hexes of a regional capital use it as the hub instead.
constexpr int32_t REGIONAL_CAPITAL_RADIUS = 5;

// ============================================================================
// Functions
// ============================================================================

/**
 * @brief Determine a player's communication tier from their researched techs.
 *
 * Returns the highest tier for which the player has the required tech.
 *
 * @param world   ECS world.
 * @param player  Player to check.
 * @return Current communication tier.
 */
[[nodiscard]] CommTier determineCommTier(const aoc::game::GameState& gameState, PlayerId player);

/**
 * @brief Compute communication distances for all of a player's cities.
 *
 * Updates the PlayerCommunicationComponent with hex distances from
 * capital (or nearest regional capital) and effective message-turn distances.
 *
 * @param world   ECS world.
 * @param grid    Hex grid.
 * @param player  Player to compute for.
 */
void updateCommunicationDistances(aoc::game::GameState& gameState,
                                   const aoc::map::HexGrid& grid,
                                   PlayerId player);

/**
 * @brief Get the communication-based modifiers for a specific city.
 *
 * @param commData  The city's communication data.
 * @return Struct with loyalty penalty, corruption, production modifier, science modifier.
 */
struct CityCommModifiers {
    float loyaltyPenalty = 0.0f;     ///< Flat loyalty subtracted per turn
    float corruptionAdd = 0.0f;      ///< Added to base corruption rate
    float productionMultiplier = 1.0f; ///< Multiplied with city production
    float scienceMultiplier = 1.0f;    ///< Multiplied with city science
};

[[nodiscard]] CityCommModifiers computeCityCommModifiers(
    const PlayerCommunicationComponent::CityCommData& commData);

/**
 * @brief Process communication for all players (call once per turn).
 *
 * Determines each player's comm tier, computes city distances,
 * and applies modifiers.
 *
 * @param world  ECS world.
 * @param grid   Hex grid.
 */
void processCommunication(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid);

} // namespace aoc::sim
