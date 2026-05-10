#pragma once

/**
 * @file AIConstants.hpp
 * @brief Named building/unit IDs used by AI scoring code.
 *
 * Per Rule of Three: only IDs that recur in three or more AI sites are
 * promoted here. Sparse uses keep their literals — the goal is to remove
 * scattered magic numbers, not to mirror the entire content table.
 *
 * The values must match `BUILDING_DEFS` (District.hpp) and `UNIT_TYPE_DEFS`
 * (UnitTypes.hpp). A static_assert on every constant lives next to its
 * use site in the .cpp where the underlying table is also visible.
 */

#include "aoc/core/Types.hpp"

namespace aoc::sim::ai {

// ---------------------------------------------------------------------------
// Building IDs — only IDs referenced by 3+ AI sites are promoted here.
// ---------------------------------------------------------------------------

/// Mint — coins from copper/silver/gold ore. Capital builds it before
/// anything else when monetary system is still Barter. AI sites: capital
/// production gate (AIController), Mint context flag (AIController), tech
/// economic-bias check (AIResearchPlanner, UtilityScoring).
inline constexpr BuildingId BUILDING_MINT{24};

// ---------------------------------------------------------------------------
// Unit type IDs — only IDs referenced by 3+ AI sites are promoted here.
// ---------------------------------------------------------------------------

/// Settler — civilian unit that founds a new city. AI sites: settler
/// production candidate cost (AIController, BehaviorTree), settler purchase
/// cost (AIController::considerPurchases).
inline constexpr UnitTypeId UNIT_SETTLER{3};

/// Builder — civilian unit that improves tiles. AI sites: builder production
/// candidate cost (AIController, BehaviorTree), builder filtering
/// (AIController::countPlayerUnits, AIBuilderController), comments in same.
inline constexpr UnitTypeId UNIT_BUILDER{5};

} // namespace aoc::sim::ai
