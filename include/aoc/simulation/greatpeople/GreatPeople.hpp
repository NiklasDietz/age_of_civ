#pragma once

/// @file GreatPeople.hpp
/// @brief Great Person types, definitions, point accumulation, and activation.

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/map/HexCoord.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

namespace aoc::game { class GameState; }
namespace aoc::game { class Unit; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

enum class GreatPersonType : uint8_t {
    Scientist,
    Engineer,
    General,
    Artist,
    Merchant,
    Admiral,   ///< Naval counterpart to the General. Maps to the roster's
               ///< Admiral category, which already held twelve named admirals.
    Prophet,   ///< Faith and the founding of a religion.
    Writer,    ///< A Great Work of Writing.
    Musician,  ///< A Great Work of Music.
    Count
};

/// What a named great person does when activated. `TypeDefault` runs the
/// behaviour shared by everyone of that type; the others replace it entirely.
/// The plan's second stage for great people: after magnitudes were made
/// per-person, a few figures needed an effect of a different KIND, not merely
/// a different size. Each of these reuses machinery the game already has
/// rather than inventing a system.
enum class GreatPersonEffect : uint8_t {
    TypeDefault = 0, ///< The type's shared behaviour.
    Eureka,          ///< Bank eureka boosts for whatever is being researched.
    TrainTroops,     ///< Experience to nearby friendly units instead of healing.
    Pilgrimage,      ///< Faith instead of gold.
};

struct GreatPersonDef {
    uint8_t          id;
    std::string_view name;
    GreatPersonType  type;
    std::string_view abilityDescription;

    /// How big this person's effect is. Every person of a type used to run the
    /// same hard-coded numbers, so Marco Polo and Mansa Musa both handed over
    /// exactly 200 gold and the ability text promising otherwise was decoration.
    /// The field a person uses depends on its type; the rest stay at their
    /// defaults and are ignored.
    ///
    ///  Scientist: `researchFraction` of the current tech, or a
    ///             `pulseAmount`-per-turn pulse for `pulseTurns` in a city with
    ///             a science building.
    ///  Engineer:  `production` hammers into the nearest city's queue.
    ///  Merchant:  `gold` into the treasury.
    ///  Prophet:   `faith` into the pool.
    ///  General / Admiral: heal, no magnitude of their own yet.
    ///  Artist / Writer / Musician: place a work, no magnitude of their own yet.
    float   researchFraction = 0.5f;   ///< Share of the current tech's cost.
    float   pulseAmount      = 8.0f;   ///< Science per turn during a pulse.
    int32_t pulseTurns       = 20;     ///< Length of that pulse.
    float   production       = 100.0f; ///< Hammers into the nearest city.
    int64_t gold             = 200;    ///< Gold into the treasury.
    float   faith            = 300.0f; ///< Faith into the pool.

    /// Which behaviour runs. Most people take their type's default.
    GreatPersonEffect effect = GreatPersonEffect::TypeDefault;

    /// Experience handed to each nearby unit by `TrainTroops`.
    int32_t experience = 30;
};

/// Total number of great person definitions.
inline constexpr uint8_t GREAT_PERSON_COUNT = 30;

/// Get all great person definitions.
[[nodiscard]] const std::array<GreatPersonDef, GREAT_PERSON_COUNT>& allGreatPersonDefs();

/// Combat strength a friendly, still-unactivated Great General (land units) or
/// Great Admiral (naval units) lends to `unit` from within GP_AURA_RADIUS.
/// Zero when no such person is near. Stacking is deliberately not allowed:
/// two generals side by side are worth one.
[[nodiscard]] float greatPersonAuraBonus(const aoc::game::GameState& gameState,
                                         const aoc::map::HexGrid& grid,
                                         const aoc::game::Unit& unit);

/// How far a Great General's or Admiral's presence is felt, in hexes.
inline constexpr int32_t GP_AURA_RADIUS = 2;

/// Strength the aura adds.
inline constexpr float GP_AURA_STRENGTH = 5.0f;

/// Dismiss `player`'s unactivated great person standing at `at`, taking a
/// lump of gold and era score instead of its one-shot ability.
[[nodiscard]] ErrorCode requestRetireGreatPerson(aoc::game::GameState& gameState, PlayerId player,
                                                 hex::AxialCoord at);

/// Gold a retirement pays.
inline constexpr int64_t GP_RETIRE_GOLD = 150;

/// Era score a retirement pays.
inline constexpr int32_t GP_RETIRE_ERA_SCORE = 3;

/// ECS component for a recruited Great Person (one-use, activated by player).
struct GreatPersonComponent {
    PlayerId owner = INVALID_PLAYER;
    uint8_t  defId = 0;        ///< Index into allGreatPersonDefs()
    /// Index into the 108-entry named roster (`allNamedGreatPeople()`), assigned
    /// at recruitment so each person has a historical name. Not serialized: the
    /// save writes this list as count 0 and skips it on read.
    uint8_t  namedId = 0;
    hex::AxialCoord position;
    bool     isActivated = false;
};

/// Hard cap on Great Persons recruited per type. H3.9: without a cap, the
/// `60 + 40 * recruited` threshold grows linearly and a tall empire can still
/// hit it forever, producing unbounded memory use at turn 300+.
inline constexpr int32_t MAX_GP_PER_TYPE = 12;

/// ECS component on player entities tracking Great Person point accumulation.
struct PlayerGreatPeopleComponent {
    PlayerId owner = INVALID_PLAYER;
    std::array<float, static_cast<std::size_t>(GreatPersonType::Count)> points = {};
    std::array<int32_t, static_cast<std::size_t>(GreatPersonType::Count)> recruited = {};
    /// Set once the type's roster is exhausted (all historical figures recruited,
    /// or MAX_GP_PER_TYPE reached). Accumulation and recruitment short-circuit
    /// for exhausted types so points cannot silently drain forever (H3.8).
    std::array<bool, static_cast<std::size_t>(GreatPersonType::Count)> exhausted = {};

    /// WP-A3 permanent effects:
    ///  - extraTradeSlots: +1 per used Great Merchant. Added to monetary cap in
    ///    TradeRouteSystem route-count check.
    ///  - pulseScienceAmount/Turns: Great Scientist activated in a Research
    ///    Lab city grants a sustained flat science bonus for N turns instead
    ///    of a one-shot progress jolt. Decremented in TurnProcessor.
    int32_t extraTradeSlots      = 0;
    float   pulseScienceAmount   = 0.0f;
    int32_t pulseScienceTurns    = 0;

    /// Threshold for next GP of this type: 60 + 40 * already_recruited.
    /// Returns +inf once the type is exhausted, so the recruitment check
    /// never fires for that slot again.
    [[nodiscard]] float threshold(GreatPersonType type) const {
        const std::size_t idx = static_cast<std::size_t>(type);
        if (this->exhausted[idx]) {
            return std::numeric_limits<float>::infinity();
        }
        return 60.0f + 40.0f * static_cast<float>(this->recruited[idx]);
    }
};

/// Add GP points based on districts/buildings. Called each turn.
void accumulateGreatPeoplePoints(aoc::game::GameState& gameState, PlayerId player);

/// Check if any GP thresholds are met and recruit. Called each turn.
void checkGreatPeopleRecruitment(aoc::game::GameState& gameState, PlayerId player);

/// Activate a Great Person's one-time ability. Checks nothing beyond
/// `isActivated`; callers go through `requestGreatPersonActivation`.
void activateGreatPerson(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                          aoc::game::Unit& gpUnit);

/// Validate and activate the Great Person `owner` has standing on `unitAt`, where it
/// stands (its recorded position is refreshed first, so a moved person can act).
/// The one action behind the screen button, the unit panel, the right-click and the
/// debug route. @return Ok; InvalidArgument (unknown player); InvalidUnitAction (no
/// unit there, not a Great Person, or already used).
[[nodiscard]] ErrorCode requestGreatPersonActivation(aoc::game::GameState& gameState,
                                                     aoc::map::HexGrid& grid, PlayerId owner,
                                                     hex::AxialCoord unitAt);

} // namespace aoc::sim
