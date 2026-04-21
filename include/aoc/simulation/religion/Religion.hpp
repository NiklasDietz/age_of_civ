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
namespace aoc::game { class Player; class GameState; class City; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim { class DiplomacyManager; }

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
    PlayerId   owner = INVALID_PLAYER;
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
///
/// When `diplomacy` is supplied, cross-owner spread is blocked between players
/// who are at war (hostile enemies don't passively convert each other).  The
/// founder's enhancer belief (e.g. Missionary Zeal, Religious Texts) scales the
/// outbound pressure.
void processReligiousSpread(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                            const DiplomacyManager* diplomacy = nullptr);

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

// ============================================================================
// Religion-vs-education science curve
// ============================================================================
//
// Per-city "Devotion" = sum of faith-building investment (Shrine, Temple,
// Cathedral, Holy Site district, dominant religion presence).  Per-city
// "Education" = sum of campus-building investment (Library, University,
// Research Lab).  Net Devotion = max(0, Devotion - Education).
//
// Net Devotion is multiplied by an era-dependent science coefficient:
//   Era 0-1 (Ancient/Classical):  +0.50  -- monastic literacy boost
//   Era 2   (Medieval):            0.00
//   Era 3   (Renaissance):        -0.30  -- friction with empirical inquiry
//   Era 4+  (Industrial onward):  -0.70  -- strong secularisation drag
//
// Each Renaissance-or-later tech researched adds an additional -0.05 kick to
// the per-devotion coefficient, so civilisations that sprint through the tech
// tree feel the conflict sooner even within a single era.
//
// Net Devotion also contributes loyalty in the early eras: +0.3 per net
// devotion in eras 0-2, 0 afterward (religious authority fades with
// modernity).
//
// Result: religion is a free early-game stabiliser, neutral at medieval,
// progressively harmful to science at renaissance+ unless the city invests in
// education buildings to cancel the drain.

/// Compute a city's raw Devotion score.
[[nodiscard]] float computeCityDevotion(const aoc::game::City& city);

/// Compute a city's Education score.
[[nodiscard]] float computeCityEducation(const aoc::game::City& city);

/// Compute the net Devotion after education cancellation.
[[nodiscard]] inline float computeCityNetDevotion(const aoc::game::City& city) {
    const float devotion  = computeCityDevotion(city);
    const float education = computeCityEducation(city);
    return (devotion > education) ? (devotion - education) : 0.0f;
}

/// Derive the player's effective era from the highest-era tech researched.
/// Robust workaround for the fact that PlayerEraComponent::currentEra is not
/// reliably updated as research completes.
[[nodiscard]] EraId effectiveEraFromTech(const aoc::game::Player& player);

/// Count completed techs of era >= 3 (Renaissance onward).
[[nodiscard]] int32_t countRenaissancePlusTechs(const aoc::game::Player& player);

/// Per-net-devotion science coefficient at the given era and tech count.
/// techsResearchedRenaissancePlus counts completed techs of era >= 3.
[[nodiscard]] float religionScienceCoefficient(EraId era, int32_t techsResearchedRenaissancePlus);

/// Per-net-devotion loyalty bonus at the given era.  Positive early, zero late.
[[nodiscard]] float religionLoyaltyCoefficient(EraId era);

} // namespace aoc::sim
