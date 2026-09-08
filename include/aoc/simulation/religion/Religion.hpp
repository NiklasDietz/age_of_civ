#pragma once

/**
 * @file Religion.hpp
 * @brief Religion definitions, beliefs, and per-player faith tracking.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/map/HexCoord.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aoc::game { class GameState; }
namespace aoc::game { class Unit; }
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

inline constexpr uint8_t BELIEF_COUNT = 40;
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

    /// The city the faith was founded in. A religion had no seat at all, so
    /// there was nothing for a rival to take and nothing for the faithful to
    /// look toward. Set when the religion is founded; the holy city radiates
    /// stronger pressure and keeps doing so for whoever holds it, which is what
    /// makes capturing one worth doing.
    hex::AxialCoord  holyCity{};
    bool             hasHolyCity = false;
};

/// Pressure the holy city adds to its own faith every turn, on top of ordinary
/// spread. Enough to matter, not enough to make the faith unshiftable.
inline constexpr float HOLY_CITY_PRESSURE = 4.0f;

/// Share of a religion's pressure in a city that fades each turn when nothing
/// reinforces it. Without decay, pressure only ever climbed: a faith that
/// reached a city once held it for the rest of the game and religion could
/// never recede, only advance.
inline constexpr float PRESSURE_DECAY_PER_TURN = 0.02f;

/// Below this, a trace of pressure is dropped entirely rather than lingering
/// forever as a rounding artefact.
inline constexpr float PRESSURE_FLOOR = 0.5f;

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

/// WP-A1: spend faith to complete the building currently at the head of a
/// city's production queue. Enforces one rush per city per turn via
/// `ProductionQueueComponent.lastFaithRushTurn`. Cost scales with building
/// tier (estimated from buildingDef.productionCost).
///
/// Returns:
///   - InvalidArgument  : queue empty / head is not a Building / owner is
///                         not the player owning the city / already rushed
///                         this turn.
///   - InsufficientResources : player lacks required faith.
///   - Ok                : progress advanced to totalCost, faith deducted.
[[nodiscard]] ErrorCode rushBuildingWithFaith(aoc::game::Player& player,
                                               aoc::game::City& city,
                                               int32_t currentTurn);

/**
 * @brief Automatically found pantheons and religions for AI players who have
 *        accumulated enough faith but are not human-controlled.
 *
 * Human players choose beliefs manually through the UI. AI players skip
 * the choice screen and auto-select beliefs by index so the game progresses.
 * Called once per turn after accumulateFaith for all players.
 */
void processAIReligionFounding(aoc::game::GameState& gameState);

/// First belief of `type` no founded religion and no pantheon has claimed yet,
/// or 255 when the type is exhausted. Beliefs are exclusive, as in Civ VI.
[[nodiscard]] uint8_t firstFreeBelief(const aoc::game::GameState& gameState, BeliefType type);

/// Found a pantheon for `player` (PANTHEON_FAITH_COST, first free follower
/// belief). Shared by the AI and the Religion screen. False when the player has
/// one already or cannot afford it.
bool foundPantheonFor(aoc::game::GameState& gameState, PlayerId player);

/// The human's choice (Civ VI plan Phase 2.7, 2026-09-05): found a pantheon
/// with `belief`, which must be a free Follower belief. InvalidArgument for a
/// wrong or taken belief, InvalidState with a pantheon already,
/// InsufficientResources without the faith.
[[nodiscard]] ErrorCode requestFoundPantheon(aoc::game::GameState& gameState, PlayerId player,
                                             uint8_t belief);

/// Found a religion with chosen founder / worship / enhancer beliefs (each a
/// free belief of its type; the pantheon is the follower belief). Same
/// preconditions as foundReligionFor; the new ReligionId is written to `outId`.
[[nodiscard]] ErrorCode requestFoundReligion(aoc::game::GameState& gameState, PlayerId player,
                                             uint8_t founder, uint8_t worship, uint8_t enhancer,
                                             ReligionId* outId = nullptr);

/// Found the religion with these beliefs for `player` (no validation; the two
/// callers above validate). Returns the new id.
[[nodiscard]] ReligionId foundReligionWith(aoc::game::GameState& gameState, aoc::game::Player& player,
                                           uint8_t founder, uint8_t worship, uint8_t enhancer);

/// True when `belief` is of `type` and no religion or pantheon has claimed it.
[[nodiscard]] bool beliefIsFree(const aoc::game::GameState& gameState, uint8_t belief,
                                BeliefType type);

/// Found a religion for `player` (RELIGION_FAITH_COST): the next name, the first
/// free founder / worship / enhancer beliefs, the pantheon as follower belief,
/// and +5 pressure in every own city. Shared by the AI and the Religion screen.
/// Returns NO_RELIGION when the player has no pantheon, already founded one,
/// cannot afford it, or no slot is left.
[[nodiscard]] ReligionId foundReligionFor(aoc::game::GameState& gameState, PlayerId player);

/// Cost to found a pantheon.
/// Pay the founder of each religion for the cities that follow it: gold and
/// science per follower city, from its Founder belief. Those two fields sat on
/// `BeliefDef` unread until 2026-09-07, so choosing a founder belief changed
/// only the text on the religion screen.
void processFounderBeliefs(aoc::game::GameState& gameState);

/// One religious unit contests another. Apostles fight, Missionaries and
/// Inquisitors can be fought; nobody else is involved. The loser is removed and
/// the winner's faith gains ground in the nearest city, which is the whole
/// point: theology is fought over cities, not over open ground.
///
/// Religious units were barred from combat entirely -- `requestAttack` requires
/// a military attacker -- so the Apostle's combat and ranged strength sat in the
/// unit table unusable and two faiths could walk through each other.
[[nodiscard]] ErrorCode requestTheologicalCombat(aoc::game::GameState& gameState,
                                                 aoc::Random& rng,
                                                 const aoc::map::HexGrid& grid, PlayerId player,
                                                 hex::AxialCoord from, hex::AxialCoord to);

/// Strength a religious unit brings to a theological contest. An Apostle is
/// built for it; a Missionary is not and an Inquisitor only defends its own.
[[nodiscard]] float theologicalStrength(const aoc::game::Unit& unit);

/// Pressure the winner's faith gains in the nearest city.
inline constexpr float THEOLOGICAL_WIN_PRESSURE = 30.0f;

/// Fade every religion's grip a little, then let each holy city renew its own.
/// Run once per turn, before the spread pass.
void processHolyCityAndDecay(aoc::game::GameState& gameState);

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

/// Per-net-devotion loyalty bonus at the given era.
///
/// Positive throughout, strongest early. It used to fall to exactly zero from
/// the Renaissance on, which switched religion's hold on an empire off at
/// precisely the point empires get large enough to need holding. Secular
/// institutions taking over is a reason for the effect to WEAKEN, not vanish.
[[nodiscard]] float religionLoyaltyCoefficient(EraId era);

/// How a city's faith pulls on its loyalty to `owner`, per point of net
/// devotion: +1 when the city follows the owner's own religion, -1 when it
/// follows a rival's, 0 when it follows none.
///
/// The devotion loyalty bonus was faith-AGNOSTIC: a city devoutly following a
/// rival's religion propped up its occupier's loyalty exactly as much as one
/// following its owner's. A rival's church in your city is a liability, not an
/// asset, and this is the sign that says so.
[[nodiscard]] float religionLoyaltyAlignment(const aoc::game::City& city,
                                             const aoc::game::Player& owner);

} // namespace aoc::sim
