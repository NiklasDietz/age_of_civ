#pragma once

/**
 * @file AIEconomicStrategy.hpp
 * @brief AI decision-making for all economic, financial, and infrastructure systems.
 *
 * Covers AI behavior for:
 *   - Bond buying/selling/dumping
 *   - Sanctions (when to impose, when to lift)
 *   - Currency devaluation decisions
 *   - Commodity hoarding and futures trading
 *   - Insurance purchasing
 *   - Immigration policy
 *   - Power grid management (which plants to build)
 *   - Railway/highway construction priorities
 *   - Colonial economic zone management
 *   - Crisis response (what to do during bank run, hyperinflation, default)
 *   - Industrial revolution preparation (stockpile required resources)
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::ecs { class World; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

class Market;
class DiplomacyManager;

/**
 * @brief Run all AI economic strategy decisions for one turn.
 *
 * Called after basic economy management. Handles the advanced economic
 * systems that the base AI doesn't touch.
 *
 * @param world      ECS world.
 * @param grid       Hex grid.
 * @param market     Market for price data.
 * @param diplomacy  Diplomacy state.
 * @param player     AI player to run decisions for.
 * @param difficulty AI difficulty level (0=easy, 1=normal, 2=hard).
 */
void aiEconomicStrategy(aoc::ecs::World& world,
                        aoc::map::HexGrid& grid,
                        const Market& market,
                        DiplomacyManager& diplomacy,
                        PlayerId player,
                        int32_t difficulty);

/**
 * @brief AI decides which buildings to prioritize for power grid.
 *
 * Logic:
 *   - If energy demand > supply: build cheapest available power plant
 *   - Prefer clean energy if available (5th industrial revolution)
 *   - Prefer hydroelectric if city has river
 *   - Avoid nuclear unless AI is desperate for power (hard difficulty only)
 */
void aiManagePowerGrid(aoc::ecs::World& world,
                       const aoc::map::HexGrid& grid,
                       PlayerId player);

/**
 * @brief AI decides where to build railways and highways.
 *
 * Priority:
 *   1. Railway between capital and largest cities
 *   2. Railway to resource-rich cities
 *   3. Highway upgrade on busiest trade routes
 */
void aiManageInfrastructure(aoc::ecs::World& world,
                            aoc::map::HexGrid& grid,
                            PlayerId player);

/**
 * @brief AI responds to active currency crisis.
 *
 * Bank Run: sell gold reserves, raise taxes, cut spending
 * Hyperinflation: raise interest rates, stop printing money
 * Default: request bond restructuring, cut spending drastically
 */
void aiCrisisResponse(aoc::ecs::World& world, PlayerId player);

/**
 * @brief AI prepares for the next industrial revolution.
 *
 * Stockpiles required resources, prioritizes prerequisite techs.
 */
void aiPrepareIndustrialRevolution(aoc::ecs::World& world,
                                    const Market& market,
                                    PlayerId player);

} // namespace aoc::sim
