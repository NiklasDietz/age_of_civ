#pragma once

/**
 * @file BuilderActions.hpp
 * @brief The human's builder orders as validated requests shared by the unit
 *        action panel, the REST routes and the MCP tools: place a chosen
 *        improvement, chop a feature or harvest a bonus resource for an
 *        era-scaled yield to the nearest city, and the Military Engineer's
 *        roads, railways and forts. Until 2026-09-05 one Improve button
 *        auto-picked, ~25 of 41 improvements were unreachable, nothing could be
 *        chopped and only traders laid roads (Civ VI plan Phase 2.4).
 */

#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/HexGrid.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game { class GameState; class Unit; class City; }
namespace aoc::sim { struct PlayerTechComponent; }

namespace aoc::sim {

/// The Military Engineer's row in UNIT_TYPE_DEFS (roads, railways, forts).
inline constexpr UnitTypeId MILITARY_ENGINEER_ID{64};

/// Charges a Military Engineer starts with (a Builder has 3).
inline constexpr int32_t MILITARY_ENGINEER_CHARGES = 2;

/// Cities within this distance of a chop or harvest receive its yield.
inline constexpr int32_t BUILDER_YIELD_RANGE = 3;

[[nodiscard]] bool isMilitaryEngineer(const aoc::game::Unit& unit);

/// Production a chop yields at era `era` (an EraId value: 20 + 10 per era); harvest food uses the same curve.
[[nodiscard]] int32_t builderYield(uint16_t era);

/// The improvements this unit may place on `tileIndex` right now: a Builder
/// gets every terrain-legal, tech-known type except roads, railways and forts;
/// a Military Engineer gets exactly those three.
[[nodiscard]] std::vector<aoc::map::ImprovementType> placeableImprovements(
    const aoc::map::HexGrid& grid, int32_t tileIndex, const aoc::game::Unit& unit,
    const PlayerTechComponent& tech);

/// Place `type` with the civilian unit standing on `at` (a Railway may replace
/// a Road; any other tile must be empty). Spends one charge.
[[nodiscard]] ErrorCode requestPlaceImprovement(aoc::game::GameState& gameState,
                                                aoc::map::HexGrid& grid, PlayerId player,
                                                hex::AxialCoord at, aoc::map::ImprovementType type);

/// Remove the Forest (Mining), Jungle (Bronze Working) or Marsh on the
/// Builder's tile; the nearest own city within BUILDER_YIELD_RANGE that is
/// building something receives builderYield(era) production. Spends one charge.
[[nodiscard]] ErrorCode requestChop(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                                    PlayerId player, hex::AxialCoord at);

/// Remove the bonus resource on the Builder's tile; the nearest own city within
/// BUILDER_YIELD_RANGE receives builderYield(era) food. Spends one charge.
[[nodiscard]] ErrorCode requestHarvest(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                                       PlayerId player, hex::AxialCoord at);

/// True when `at` holds a feature a Builder can chop.
[[nodiscard]] bool canChopAt(const aoc::map::HexGrid& grid, int32_t tileIndex);

/// True when `at` holds a bonus resource a Builder can harvest.
[[nodiscard]] bool canHarvestAt(const aoc::map::HexGrid& grid, int32_t tileIndex);

} // namespace aoc::sim
