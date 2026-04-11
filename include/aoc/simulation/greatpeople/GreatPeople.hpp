#pragma once

/// @file GreatPeople.hpp
/// @brief Great Person types, definitions, point accumulation, and activation.

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace aoc::game { class GameState; }
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
    PlayerId owner;
    uint8_t  defId;            ///< Index into allGreatPersonDefs()
    hex::AxialCoord position;
    bool     isActivated = false;
};

/// ECS component on player entities tracking Great Person point accumulation.
struct PlayerGreatPeopleComponent {
    PlayerId owner;
    std::array<float, static_cast<std::size_t>(GreatPersonType::Count)> points = {};
    std::array<int32_t, static_cast<std::size_t>(GreatPersonType::Count)> recruited = {};

    /// Threshold for next GP of this type: 60 + 40 * already_recruited
    [[nodiscard]] float threshold(GreatPersonType type) const {
        const std::size_t idx = static_cast<std::size_t>(type);
        return 60.0f + 40.0f * static_cast<float>(this->recruited[idx]);
    }
};

/// Add GP points based on districts/buildings. Called each turn.
void accumulateGreatPeoplePoints(aoc::game::GameState& gameState, PlayerId player);

/// Check if any GP thresholds are met and recruit. Called each turn.
void checkGreatPeopleRecruitment(aoc::game::GameState& gameState, PlayerId player);

/// Activate a Great Person's one-time ability.
void activateGreatPerson(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                          EntityId greatPersonEntity);

} // namespace aoc::sim
