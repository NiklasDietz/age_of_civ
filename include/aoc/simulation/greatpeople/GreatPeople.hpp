#pragma once

/// @file GreatPeople.hpp
/// @brief Great Person types, definitions, point accumulation, and activation.

#include "aoc/core/Types.hpp"
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
    Count
};

struct GreatPersonDef {
    uint8_t          id;
    std::string_view name;
    GreatPersonType  type;
    std::string_view abilityDescription;
};

/// Total number of great person definitions.
inline constexpr uint8_t GREAT_PERSON_COUNT = 18;

/// Get all great person definitions.
[[nodiscard]] const std::array<GreatPersonDef, GREAT_PERSON_COUNT>& allGreatPersonDefs();

/// ECS component for a recruited Great Person (one-use, activated by player).
struct GreatPersonComponent {
    PlayerId owner = INVALID_PLAYER;
    uint8_t  defId = 0;        ///< Index into allGreatPersonDefs()
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

/// Activate a Great Person's one-time ability.
void activateGreatPerson(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                          aoc::game::Unit& gpUnit);

} // namespace aoc::sim
