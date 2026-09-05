#pragma once

/**
 * @file GreatWorks.hpp
 * @brief Great Works: the works of art, writing and music that Theatre buildings
 *        house. A building's `greatWorksSlots` is capacity; a placed `GreatWork` is
 *        what tourism counts. Until 2026-09-05 tourism counted the empty slots as if
 *        filled and nothing ever created a work.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace aoc::game {
class City;
class Player;
} // namespace aoc::game
namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

enum class GreatWorkType : uint8_t {
    Art,      ///< Great Artist
    Writing,  ///< Great Writer
    Music,    ///< Great Musician
    Artifact, ///< Dug from an antiquity site
    Count
};

[[nodiscard]] constexpr std::string_view greatWorkTypeName(GreatWorkType type) {
    switch (type) {
    case GreatWorkType::Art:
        return "Art";
    case GreatWorkType::Writing:
        return "Writing";
    case GreatWorkType::Music:
        return "Music";
    case GreatWorkType::Artifact:
        return "Artifact";
    default:
        return "Unknown";
    }
}

/// One placed work. `namedId` is the named Great Person who made it (0xFF when
/// nobody in particular, e.g. an artifact); the name comes from the roster.
struct GreatWork {
    GreatWorkType type  = GreatWorkType::Art;
    PlayerId creator    = INVALID_PLAYER;
    uint8_t namedId     = 0xFF;
    int32_t createdTurn = 0;
};

/// Per-city housed works; capacity is the sum of the city's building slots.
struct CityGreatWorksComponent {
    std::vector<GreatWork> works;
};

/// Sum of `greatWorksSlots` over the city's buildings.
[[nodiscard]] int32_t greatWorkCapacity(const aoc::game::City& city);

/// Capacity minus placed works, never negative.
[[nodiscard]] int32_t freeGreatWorkSlots(const aoc::game::City& city);

/// Place `work` in `city` if a slot is free; false (and nothing changes) otherwise.
bool placeGreatWork(aoc::game::City& city, const GreatWork& work);

/// The owner's city with a free slot nearest to `near`, or nullptr.
[[nodiscard]] aoc::game::City* cityWithFreeGreatWorkSlot(aoc::game::Player& owner,
                                                         const aoc::map::HexGrid& grid,
                                                         hex::AxialCoord near);

/// Works placed across all of `owner`'s cities, and the capacity they sit in.
struct GreatWorkTally {
    int32_t works    = 0;
    int32_t capacity = 0;
};
[[nodiscard]] GreatWorkTally tallyGreatWorks(const aoc::game::Player& owner);

} // namespace aoc::sim
