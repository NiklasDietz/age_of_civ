/**
 * @file DistrictPlacement.hpp
 * @brief Which tile a district stands on, and what that costs the tile.
 *
 * Until now every district was stamped on the city-centre hex, so the
 * adjacency rules in DistrictAdjacency.hpp all measured the same tile and the
 * placement puzzle that defines Civ VI city building did not exist. A district
 * now takes a real tile inside its city: `bestDistrictTile` picks it by the
 * adjacency score (deterministic argmax, ties by lowest tile index) and
 * `placeDistrictOnTile` occupies it -- the improvement there is removed and no
 * citizen works it any more.
 *
 * The human picks the tile himself from Phase 3.1 commit 2; this header is the
 * rule set both paths share.
 */

#pragma once

#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/DistrictAdjacency.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace aoc::game {
class City;
class GameState;
} // namespace aoc::game
namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

/// Why a tile cannot hold a district. `Ok` is the only accepting value.
enum class DistrictTileReason : uint8_t {
    Ok,
    OffMap,     ///< Outside the grid
    NotOwned,   ///< The city's owner does not own the tile
    TooFar,     ///< Beyond CITY_WORK_RADIUS of the city centre
    CityCenter, ///< The centre hex belongs to the City Center district
    Occupied,   ///< Another district already stands there
    NeedsWater, ///< Harbor: must sit on water next to the city centre
    NeedsLand,  ///< Every other district: land only
    Impassable, ///< Mountain, ice or anything else nothing can enter
};

[[nodiscard]] std::string_view districtTileReasonName(DistrictTileReason reason);

/// Any district of any player standing on `at`.
[[nodiscard]] bool tileHasDistrict(const aoc::game::GameState& gameState, hex::AxialCoord at);

/// Whether `city` may put a district of `type` on `at`, and why not.
[[nodiscard]] DistrictTileReason districtTileReason(const aoc::game::GameState& gameState,
                                                    const aoc::map::HexGrid& grid,
                                                    const aoc::game::City& city, DistrictType type,
                                                    hex::AxialCoord at);

/// Every tile `city` could put a district of `type` on, in spiral order.
[[nodiscard]] std::vector<hex::AxialCoord>
districtCandidateTiles(const aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                       const aoc::game::City& city, DistrictType type);

/// Score for the AI's choice and the human's preview: the adjacency yields the
/// tile would grant, minus what the tile currently yields, so a district does
/// not pave the best farmland while a barren neighbour is free.
[[nodiscard]] float districtTileScore(const aoc::game::GameState& gameState,
                                      const aoc::map::HexGrid& grid, DistrictType type,
                                      hex::AxialCoord at);

/// Same score against a prebuilt index. The GameState overload builds a
/// throwaway index per call, which walks every city in the world; prefer this
/// one when scoring a whole candidate list.
[[nodiscard]] float districtTileScore(const DistrictIndex& districts, const aoc::map::HexGrid& grid,
                                      DistrictType type, hex::AxialCoord at);

/// Best candidate by `districtTileScore`, ties broken by the lowest tile index
/// so the choice is stable across runs. Returns the city centre when the city
/// is boxed in, which keeps the pre-3.1 behaviour as the fallback.
[[nodiscard]] hex::AxialCoord bestDistrictTile(const aoc::game::GameState& gameState,
                                               const aoc::map::HexGrid& grid,
                                               const aoc::game::City& city, DistrictType type);

/// The human's choice of tile for a district his city is already building. The
/// tile is stored on the queued item and used at completion while it is still
/// legal; otherwise the scorer takes over. EntityNotFound for a missing player
/// or city, InvalidUnitAction with the tile reason when the tile is not
/// allowed, InvalidState when the city is not building that district (queue it
/// first: the queue owns the cost and the tech gate).
ErrorCode requestPlaceDistrict(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                               PlayerId player, hex::AxialCoord cityAt, DistrictType type,
                               hex::AxialCoord tile);

/// Put the district on `at`: the improvement there is removed (and its pillage
/// flag cleared) and the tile stops being worked. Returns the placed district.
CityDistrictsComponent::PlacedDistrict& placeDistrictOnTile(aoc::map::HexGrid& grid,
                                                            aoc::game::City& city,
                                                            DistrictType type, hex::AxialCoord at);

} // namespace aoc::sim
