/**
 * @file SpaceRace.cpp
 * @brief Space Race progress processor for Science Victory.
 *
 * Each turn, for every non-eliminated player with at least one Campus
 * district, accumulates production toward the next uncompleted project
 * if the project's required tech is researched. Progress is funded by
 * the player's current science output (scaled down) to avoid duplicating
 * tech-tree spend: think of it as a parallel R&D track gated by both
 * science capacity and prerequisite tech.
 */

#include "aoc/simulation/victory/SpaceRace.hpp"

#include "aoc/balance/BalanceParams.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/core/Log.hpp"

#include <memory>

namespace aoc::sim {

namespace {

constexpr float SCIENCE_TO_PROGRESS = 0.2f;

// WP-B2 Mars Colony resource gate. Totals consumed on completion.
// Audit 2026-04 second pass: with 48 Lunar / 0 Mars at 1000t the He3 + Semi
// gate still bottlenecks. Cut further: Ti 3->2, He3 10->5, Semi 15->8.
// Civs that complete Lunar should now reasonably reach Mars within 200t.
constexpr int32_t MARS_TITANIUM_COST       = 2;
constexpr int32_t MARS_HELIUM3_COST        = 5;
constexpr int32_t MARS_SEMICONDUCTORS_COST = 8;

[[nodiscard]] bool playerHasCampus(const aoc::game::Player& player) {
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (city->districts().hasDistrict(DistrictType::Campus)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] int32_t totalStock(const aoc::game::Player& player, uint16_t goodId) {
    int32_t total = 0;
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        total += city->stockpile().getAmount(goodId);
    }
    return total;
}

/// Drain @p amount of @p goodId across the player's cities greedily.
/// Returns true iff the full amount was drained; on false the stockpiles are
/// left unchanged.
[[nodiscard]] bool drainGoods(aoc::game::Player& player, uint16_t goodId,
                              int32_t amount) {
    if (totalStock(player, goodId) < amount) {
        return false;
    }
    int32_t remaining = amount;
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (remaining <= 0) { break; }
        const int32_t avail = city->stockpile().getAmount(goodId);
        if (avail <= 0) { continue; }
        const int32_t take = std::min(avail, remaining);
        (void)city->stockpile().consumeGoods(goodId, take);
        remaining -= take;
    }
    return true;
}

} // namespace

void processSpaceRace(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        aoc::game::Player& player = *playerPtr;
        if (player.victoryTracker().isEliminated) { continue; }

        PlayerSpaceRaceComponent& race = player.spaceRace();
        if (race.allCompleted()) { continue; }

        if (!playerHasCampus(player)) { continue; }

        const SpaceProjectId next = race.nextProject();
        if (next == SpaceProjectId::Count) { continue; }

        const int32_t idx = static_cast<int32_t>(next);
        const SpaceProjectDef& def = SPACE_PROJECT_DEFS[static_cast<std::size_t>(idx)];

        if (!player.hasResearched(def.requiredTech)) { continue; }

        const float science = computePlayerScience(player, grid);
        race.progress[idx] += science * SCIENCE_TO_PROGRESS;

        // Scale the nominal cost by the balance multiplier so the balance GA
        // can tune game length without touching SPACE_PROJECT_DEFS.
        const float effectiveCost = def.productionCost
                                  * aoc::balance::params().spaceRaceCostMult;

        if (race.progress[idx] >= effectiveCost) {
            // WP-B2 Mars gate: require stockpiled Titanium + He3 +
            // Semiconductors. Titanium only flows once Lunar Colony is
            // complete (EconomySimulation), so civs that skipped Lunar
            // Colony cannot launch Mars even with enough production.
            if (next == SpaceProjectId::MarsColony) {
                if (totalStock(player, goods::TITANIUM)       < MARS_TITANIUM_COST
                 || totalStock(player, goods::HELIUM_3)       < MARS_HELIUM3_COST
                 || totalStock(player, goods::SEMICONDUCTORS) < MARS_SEMICONDUCTORS_COST) {
                    race.progress[idx] = effectiveCost;
                    continue;
                }
                (void)drainGoods(player, goods::TITANIUM,       MARS_TITANIUM_COST);
                (void)drainGoods(player, goods::HELIUM_3,       MARS_HELIUM3_COST);
                (void)drainGoods(player, goods::SEMICONDUCTORS, MARS_SEMICONDUCTORS_COST);
                LOG_INFO("Player %u Mars Colony consumed %d Ti + %d He3 + %d Semi",
                         static_cast<unsigned>(player.id()),
                         MARS_TITANIUM_COST, MARS_HELIUM3_COST,
                         MARS_SEMICONDUCTORS_COST);
            }

            race.completed[idx] = true;
            race.progress[idx] = effectiveCost;
            LOG_INFO("Player %u [SpaceRace.cpp:processSpaceRace] completed '%.*s' (%d/%d projects)",
                     static_cast<unsigned>(player.id()),
                     static_cast<int>(def.name.size()), def.name.data(),
                     race.completedCount(), SPACE_PROJECT_COUNT);
        }
    }
}

} // namespace aoc::sim
