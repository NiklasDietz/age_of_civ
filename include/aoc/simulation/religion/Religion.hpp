#pragma once

/**
 * @file Religion.hpp
 * @brief Religion definitions, beliefs, and per-player faith tracking.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aoc::game { class GameState; }
namespace aoc::game { class Player; class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

using ReligionId = uint8_t;
inline constexpr ReligionId NO_RELIGION = 255;
inline constexpr uint8_t MAX_RELIGIONS = 8;

// ============================================================================
// Religion names (players choose from these when founding)
// ============================================================================

inline constexpr std::array<std::string_view, 12> RELIGION_NAMES = {{
    "Christianity", "Islam", "Hinduism", "Buddhism",
    "Judaism", "Sikhism", "Confucianism", "Taoism",
    "Zoroastrianism", "Shinto", "Paganism", "Hellenism"
}};

// ============================================================================
// Beliefs
// ============================================================================

enum class BeliefType : uint8_t {
    Founder,    ///< Bonus to the founding player (gold/science per follower city)
    Follower,   ///< Bonus to cities following this religion (amenities, food)
    Worship,    ///< Unlocks a unique worship building
    Enhancer,   ///< Spreads religion faster or further
    Count
};

struct BeliefDef {
    uint8_t          id;
    std::string_view name;
    BeliefType       type;
    std::string_view description;
    // Effect values (interpreted based on type)
    float goldPerFollowerCity    = 0.0f;  ///< Founder
    float sciencePerFollowerCity = 0.0f;  ///< Founder
    float amenityBonus           = 0.0f;  ///< Follower
    float foodBonus              = 0.0f;  ///< Follower
    float faithBonus             = 0.0f;  ///< Various
    float spreadStrength         = 0.0f;  ///< Enhancer (multiplier on spread)
};

inline constexpr uint8_t BELIEF_COUNT = 16;
[[nodiscard]] const std::array<BeliefDef, BELIEF_COUNT>& allBeliefs();

// ============================================================================
// Religion definition (created when a player founds a religion)
// ============================================================================

struct ReligionDef {
    ReligionId       id = NO_RELIGION;
    std::string      name;
    PlayerId         founder = INVALID_PLAYER;
    uint8_t          founderBelief = 255;   ///< Index into allBeliefs()
    uint8_t          followerBelief = 255;
    uint8_t          worshipBelief = 255;
    uint8_t          enhancerBelief = 255;
};

// ============================================================================
// Per-player faith state (ECS component)
// ============================================================================

struct PlayerFaithComponent {
    PlayerId   owner;
    float      faith = 0.0f;           ///< Accumulated faith points
    ReligionId foundedReligion = NO_RELIGION; ///< ID of religion this player founded (NO_RELIGION if none)
    bool       hasPantheon = false;     ///< Has a pantheon belief (precursor to full religion)
    uint8_t    pantheonBelief = 255;    ///< Belief chosen for pantheon
};

// ============================================================================
// Per-city religion state (ECS component)
// ============================================================================

struct CityReligionComponent {
    /// Religious pressure per religion. Index = ReligionId, value = pressure points.
    std::array<float, MAX_RELIGIONS> pressure = {};

    /// The dominant religion in this city (highest pressure, or NO_RELIGION if none).
    [[nodiscard]] ReligionId dominantReligion() const {
        ReligionId best = NO_RELIGION;
        float bestPressure = 0.0f;
        for (uint8_t i = 0; i < MAX_RELIGIONS; ++i) {
            if (this->pressure[i] > bestPressure) {
                bestPressure = this->pressure[i];
                best = i;
            }
        }
        return best;
    }

    /// Add religious pressure from a missionary/apostle.
    void addPressure(ReligionId religion, float amount) {
        if (religion < MAX_RELIGIONS) {
            this->pressure[religion] += amount;
        }
    }
};

// ============================================================================
// Global religion tracker (one per game, like GlobalWonderTracker)
// ============================================================================

struct GlobalReligionTracker {
    std::array<ReligionDef, MAX_RELIGIONS> religions;
    uint8_t religionsFoundedCount = 0;

    [[nodiscard]] bool canFoundReligion() const {
        return this->religionsFoundedCount < MAX_RELIGIONS;
    }

    /// Found a new religion. Returns the new ReligionId.
    ReligionId foundReligion(const std::string& name, PlayerId founder) {
        ReligionId id = this->religionsFoundedCount;
        this->religions[id].id = id;
        this->religions[id].name = name;
        this->religions[id].founder = founder;
        ++this->religionsFoundedCount;
        return id;
    }
};

// ============================================================================
// System functions
// ============================================================================

/// Accumulate faith per turn from tiles, buildings, and natural wonders.
void accumulateFaith(aoc::game::Player& player, const aoc::map::HexGrid& grid);

/// Process religious pressure: cities with holy sites spread to neighbors.
void processReligiousSpread(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid);

/// Apply religion bonuses (founder beliefs give gold/science, follower beliefs give amenities).
void applyReligionBonuses(aoc::game::Player& player);

/// Check religious victory: one religion is dominant in all civilizations' cities.
[[nodiscard]] bool checkReligiousVictory(const aoc::game::GameState& gameState, PlayerId& outWinner);

/**
 * @brief Automatically found pantheons and religions for AI players who have
 *        accumulated enough faith but are not human-controlled.
 *
 * Human players choose beliefs manually through the UI. AI players skip
 * the choice screen and auto-select beliefs by index so the game progresses.
 * Called once per turn after accumulateFaith for all players.
 */
void processAIReligionFounding(aoc::game::GameState& gameState);

/// Cost to found a pantheon.
inline constexpr float PANTHEON_FAITH_COST = 25.0f;

/// Cost to found a religion (must have pantheon first).
/// Lowered from 100 to 50 so religions are founded within the first 50 turns.
inline constexpr float RELIGION_FAITH_COST = 50.0f;

/// Cost to purchase a missionary.
inline constexpr float MISSIONARY_FAITH_COST = 150.0f;

/// Cost to purchase an apostle.
inline constexpr float APOSTLE_FAITH_COST = 250.0f;

} // namespace aoc::sim
