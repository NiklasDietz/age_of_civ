#pragma once

/**
 * @file CityActions.hpp
 * @brief The human's city actions as validated requests shared by the City
 *        Detail screen, the REST routes and the MCP tools: purchase with gold
 *        or faith, faith-rush the queue head, set the citizen focus, lock and
 *        toggle worked tiles, remove a queue entry, queue a city project.
 *        Until 2026-09-05 purchase and projects were AI-only or had no caller
 *        and the worked-tile toggle ignored the citizen count (Civ VI plan 2.2).
 */

#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/DistrictAdjacency.hpp"
#include "aoc/simulation/city/Governor.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace aoc::game { class GameState; class City; class Player; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

/// Faith price of a religious unit (Civ VI: Missionary 100, Apostle 200,
/// Inquisitor 100), scaled by the game pace; 0 for any other unit. The unit
/// table carries no production cost for them, so this is the only price.
[[nodiscard]] float religiousUnitFaithCost(UnitTypeId unitId);

/// Buy a unit or building with gold. The item must pass the same tech, civic
/// and district gates as production (`grid` may be null: spatial gates skip).
[[nodiscard]] ErrorCode requestPurchase(aoc::game::GameState& gameState,
                                        const aoc::map::HexGrid* grid, PlayerId player,
                                        hex::AxialCoord cityAt, ProductionItemType type,
                                        uint16_t itemId);

/// Buy a religious unit (Missionary 19, Apostle 20, Inquisitor 21) with faith;
/// the player must have founded a religion. Cost: religiousUnitFaithCost.
[[nodiscard]] ErrorCode requestFaithPurchase(aoc::game::GameState& gameState, PlayerId player,
                                             hex::AxialCoord cityAt, UnitTypeId unitId);

/// Finish the building at the head of the queue with faith (once per city per turn).
[[nodiscard]] ErrorCode requestFaithRush(aoc::game::GameState& gameState, PlayerId player,
                                         hex::AxialCoord cityAt);

/// Set the citizen focus and re-assign the worked tiles for it.
[[nodiscard]] ErrorCode requestSetCityFocus(aoc::game::GameState& gameState,
                                            const aoc::map::HexGrid& grid, PlayerId player,
                                            hex::AxialCoord cityAt, CityFocus focus);

/// Pin or unpin a tile against re-assignment (pinning also works it when a citizen is free).
[[nodiscard]] ErrorCode requestToggleTileLock(aoc::game::GameState& gameState, PlayerId player,
                                              hex::AxialCoord cityAt, hex::AxialCoord tile);

/// Work or stop working a tile: owned by the player, within the work radius,
/// passable, and a citizen must be free to start working it.
[[nodiscard]] ErrorCode requestToggleWorkedTile(aoc::game::GameState& gameState,
                                                const aoc::map::HexGrid& grid, PlayerId player,
                                                hex::AxialCoord cityAt, hex::AxialCoord tile);

/// Drop queue entry `index` (its progress is lost).
[[nodiscard]] ErrorCode requestRemoveQueueItem(aoc::game::GameState& gameState, PlayerId player,
                                               hex::AxialCoord cityAt, int32_t index);

/// Queue a repeatable city project; the city needs the project's district.
[[nodiscard]] ErrorCode requestQueueProject(aoc::game::GameState& gameState, PlayerId player,
                                            hex::AxialCoord cityAt, CityProjectType project);

/// True when `city` holds the district `project` needs.
[[nodiscard]] bool cityProjectAvailable(const aoc::game::City& city, CityProjectType project);

/// The WorkerFocus the tile scorer uses for a CityFocus.
[[nodiscard]] WorkerFocus workerFocusFor(CityFocus focus);

/// Civ VI amenity tier for a happiness value: Ecstatic, Happy, Content,
/// Displeased, Unhappy, Unrest.
[[nodiscard]] std::string_view amenityTierName(float happiness);

/// Turns until the queue head completes at `productionPerTurn`; -1 when it
/// never will (no production or empty queue).
[[nodiscard]] int32_t turnsToComplete(const aoc::game::City& city, float productionPerTurn);

/// "Monument 6t" or "idle": the queue head with its turns to complete.
[[nodiscard]] std::string cityProductionSummary(const aoc::game::City& city, float productionPerTurn);

/// One-line banner text under a city name: "Pop 4 | Monument 6t" or
/// "Pop 4 | idle" (the map banner and the city list share it).
[[nodiscard]] std::string cityBannerSummary(const aoc::game::City& city, float productionPerTurn);

/// Food stored over food needed for the next citizen, clamped to [0, 1].
[[nodiscard]] float cityGrowthFraction(const aoc::game::City& city);

} // namespace aoc::sim
