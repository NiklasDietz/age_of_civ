/**
 * @file CityGrowth.cpp
 * @brief City population growth logic.
 */

#include "aoc/simulation/city/CityGrowth.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

float foodForGrowth(int32_t currentPopulation) {
    float pop = static_cast<float>(currentPopulation);
    float base = 15.0f + 6.0f * pop + std::pow(pop, 1.3f);
    return base * GamePace::instance().growthMultiplier;
}

// ECS version removed -- all callers now use GameState-native processCityGrowth(Player&, ...).

// ============================================================================
// Auto-assign workers to best available tiles (still ECS-based, used by city UI)
// ============================================================================

void autoAssignWorkers(CityComponent& city, const aoc::map::HexGrid& grid,
                        WorkerFocus focus) {
    // Keep locked tiles and the city center, remove everything else
    std::vector<aoc::hex::AxialCoord> kept;
    kept.push_back(city.location);  // Center always worked (free)
    for (const aoc::hex::AxialCoord& tile : city.workedTiles) {
        if (tile == city.location) { continue; }
        if (city.isTileLocked(tile)) {
            kept.push_back(tile);
        }
    }
    city.workedTiles = kept;

    // How many citizens can still be assigned
    int32_t slotsAvailable = city.population - (static_cast<int32_t>(city.workedTiles.size()) - 1);
    if (slotsAvailable <= 0) { return; }

    // Score all owned tiles within 3 hexes
    struct TileScore {
        aoc::hex::AxialCoord coord;
        float score;
    };
    std::vector<TileScore> candidates;

    std::vector<aoc::hex::AxialCoord> nearby;
    nearby.reserve(40);
    aoc::hex::spiral(city.location, 3, std::back_inserter(nearby));

    for (const aoc::hex::AxialCoord& tile : nearby) {
        if (!grid.isValid(tile)) { continue; }
        int32_t idx = grid.toIndex(tile);
        if (grid.owner(idx) != city.owner) { continue; }
        if (grid.movementCost(idx) == 0) { continue; }
        if (tile == city.location) { continue; }

        // Skip already worked tiles
        bool alreadyWorked = false;
        for (const aoc::hex::AxialCoord& wt : city.workedTiles) {
            if (wt == tile) { alreadyWorked = true; break; }
        }
        if (alreadyWorked) { continue; }

        aoc::map::TileYield yields = grid.tileYield(idx);
        float score = 0.0f;
        switch (focus) {
            case WorkerFocus::Food:
                score = static_cast<float>(yields.food) * 3.0f
                      + static_cast<float>(yields.production) * 1.0f
                      + static_cast<float>(yields.gold) * 0.5f
                      + static_cast<float>(yields.science) * 0.5f;
                break;
            case WorkerFocus::Production:
                score = static_cast<float>(yields.food) * 1.0f
                      + static_cast<float>(yields.production) * 3.0f
                      + static_cast<float>(yields.gold) * 0.5f
                      + static_cast<float>(yields.science) * 0.5f;
                break;
            case WorkerFocus::Gold:
                score = static_cast<float>(yields.food) * 0.5f
                      + static_cast<float>(yields.production) * 0.5f
                      + static_cast<float>(yields.gold) * 3.0f
                      + static_cast<float>(yields.science) * 1.0f;
                break;
            case WorkerFocus::Science:
                score = static_cast<float>(yields.food) * 0.5f
                      + static_cast<float>(yields.production) * 1.0f
                      + static_cast<float>(yields.gold) * 0.5f
                      + static_cast<float>(yields.science) * 3.0f;
                break;
            case WorkerFocus::Balanced:
            default:
                score = static_cast<float>(yields.food) * 2.0f
                      + static_cast<float>(yields.production) * 1.5f
                      + static_cast<float>(yields.gold) * 1.0f
                      + static_cast<float>(yields.science) * 1.0f;
                break;
        }
        // Bonus for tiles with resources
        if (grid.resource(idx).isValid()) { score += 2.0f; }

        candidates.push_back({tile, score});
    }

    // Sort by score descending
    std::sort(candidates.begin(), candidates.end(),
        [](const TileScore& a, const TileScore& b) { return a.score > b.score; });

    // Assign top tiles
    for (const TileScore& ts : candidates) {
        if (slotsAvailable <= 0) { break; }
        city.workedTiles.push_back(ts.coord);
        --slotsAvailable;
    }
}

// ============================================================================
// GameState-native city growth (Phase 3 migration)
// ============================================================================

static void processSingleCityGrowth(aoc::game::City& city,
                                     const aoc::map::HexGrid& grid,
                                     bool hasFeudalismCivic,
                                     float cityHappiness) {
    // Calculate food from worked tiles
    float totalFood = 0.0f;
    for (const aoc::hex::AxialCoord& tileCoord : city.workedTiles()) {
        if (!grid.isValid(tileCoord)) {
            continue;
        }
        int32_t tileIndex = grid.toIndex(tileCoord);
        aoc::map::TileYield yield = grid.tileYield(tileIndex);
        float tileFood = static_cast<float>(yield.food);
        // City center always yields at least 2 food (Civ 6 guarantee)
        if (tileCoord == city.location() && tileFood < 2.0f) {
            tileFood = 2.0f;
        }
        // Farm triangle adjacency bonus: +1 food if 2+ adjacent farms (Feudalism civic)
        if (hasFeudalismCivic) {
            tileFood += static_cast<float>(computeFarmAdjacencyBonus(grid, tileIndex));
        }
        totalFood += tileFood;
    }

    // Food consumption: 2 per citizen
    float consumption = static_cast<float>(city.population()) * 2.0f;
    float surplus = totalFood - consumption;

    // Celebration growth (rapture): very happy cities get +50% food surplus
    if (cityHappiness >= 3.0f && surplus > 0.0f) {
        surplus *= 1.5f;
    }

    // --- Food as tradeable good ---
    // Positive surplus: grows the city, but excess above 2x growth need
    // is converted to stockpiled Wheat goods (can be sold on the market).
    // Negative surplus: before starvation, consume Wheat from city stockpile.
    constexpr uint16_t WHEAT_GOOD_ID = 40;
    constexpr float FOOD_TO_GOODS_RATIO = 0.5f;  // 2 surplus food → 1 Wheat good
    constexpr float GOODS_TO_FOOD_RATIO = 1.5f;   // 1 Wheat good → 1.5 food (processing loss)

    float needed = foodForGrowth(city.population());

    if (surplus > 0.0f) {
        // Cap: city only benefits from up to 2x what it needs for growth.
        // Excess is converted to Wheat goods and added to stockpile.
        const float maxUsableSurplus = needed * 2.0f;
        if (surplus > maxUsableSurplus) {
            const float excessFood = surplus - maxUsableSurplus;
            const int32_t goodsProduced = static_cast<int32_t>(excessFood * FOOD_TO_GOODS_RATIO);
            if (goodsProduced > 0) {
                city.stockpile().addGoods(WHEAT_GOOD_ID, goodsProduced);
            }
            surplus = maxUsableSurplus;
        }
    } else if (surplus < 0.0f) {
        // Deficit: consume Wheat from stockpile to offset food shortage.
        // This prevents starvation for cities that import food via trade.
        const int32_t wheatAvailable = city.stockpile().getAmount(WHEAT_GOOD_ID);
        if (wheatAvailable > 0) {
            const float foodNeeded = -surplus;
            const int32_t wheatToConsume = std::min(
                wheatAvailable,
                static_cast<int32_t>(foodNeeded / GOODS_TO_FOOD_RATIO) + 1);
            const float foodRecovered = static_cast<float>(wheatToConsume) * GOODS_TO_FOOD_RATIO;
            [[maybe_unused]] bool consumed =
                city.stockpile().consumeGoods(WHEAT_GOOD_ID, wheatToConsume);
            surplus += foodRecovered;  // May become positive
        }
    }

    city.setFoodSurplus(city.foodSurplus() + surplus);
    if (city.foodSurplus() >= needed) {
        // Granary (BuildingId{15}): preserves 50% of food stock after growth
        bool hasGranary = city.hasBuilding(BuildingId{15});
        if (hasGranary) {
            city.setFoodSurplus(city.foodSurplus() * 0.5f);
        } else {
            city.setFoodSurplus(city.foodSurplus() - needed);
        }
        city.growPopulation(1);

        // Auto-assign new citizen to best unworked tile within radius 3 (37 tiles)
        // This matches Civ 6 city workable area of 3 hex rings.
        aoc::hex::AxialCoord center = city.location();
        std::vector<aoc::hex::AxialCoord> candidates;
        candidates.reserve(37);
        aoc::hex::spiral(center, 3, std::back_inserter(candidates));

        float bestYieldValue = -1.0f;
        aoc::hex::AxialCoord bestTile = center;
        bool foundNew = false;

        for (const aoc::hex::AxialCoord& tile : candidates) {
            if (!grid.isValid(tile)) { continue; }
            if (tile == center) { continue; }  // Center is already worked (free)

            bool alreadyWorked = false;
            for (const aoc::hex::AxialCoord& worked : city.workedTiles()) {
                if (worked == tile) {
                    alreadyWorked = true;
                    break;
                }
            }
            if (alreadyWorked) { continue; }

            int32_t idx = grid.toIndex(tile);
            {
                const aoc::map::TerrainType nTerrain = grid.terrain(idx);
                if (aoc::map::isWater(nTerrain)) { continue; }
                if (aoc::map::isImpassable(nTerrain)) {
                    // Mountain tiles with mountain-mineable metals are workable,
                    // everything else impassable (deep ocean, shallow water) is not.
                    const ResourceId mRes = grid.resource(idx);
                    const bool workableMountain =
                        (nTerrain == aoc::map::TerrainType::Mountain
                         && mRes.isValid()
                         && aoc::sim::isMountainMetal(mRes.value));
                    if (!workableMountain) {
                        continue;
                    }
                }
            }
            // Only work tiles owned by this city's player
            if (grid.owner(idx) != city.owner()) { continue; }

            aoc::map::TileYield yield = grid.tileYield(idx);
            float value = static_cast<float>(yield.food) * 2.0f
                        + static_cast<float>(yield.production)
                        + static_cast<float>(yield.gold) * 0.5f
                        + static_cast<float>(yield.science) * 0.5f;
            // Resource tiles get a priority bonus: they feed production chains
            // (copper ore → coins, iron ore → ingots, etc.) and without this bonus
            // cities deprioritise them in favour of pure food tiles, starving the
            // economy of raw materials.  Matches the +5 bonus applied at founding.
            if (grid.resource(idx).isValid()) {
                value += 3.0f;
                // Minting ores get an extra +8 so they are worked before high-food
                // tiles like cattle (food=4, base=12) and are assigned by pop=2.
                const uint16_t resId = grid.resource(idx).value;
                if (resId == aoc::sim::goods::COPPER_ORE
                    || resId == aoc::sim::goods::SILVER_ORE) {
                    value += 8.0f;
                }
                // Mountain metal tiles have zero terrain yield, so without a boost
                // they would never beat food tiles. Keep them competitive.
                if (grid.terrain(idx) == aoc::map::TerrainType::Mountain
                    && aoc::sim::isMountainMetal(resId)) {
                    value += 6.0f;
                }
            }
            if (value > bestYieldValue) {
                bestYieldValue = value;
                bestTile = tile;
                foundNew = true;
            }
        }

        if (foundNew) {
            city.assignWorker(bestTile);
        }

        LOG_INFO("%s grew to pop %d", city.name().c_str(), city.population());
    }

    // Clamp surplus
    if (city.foodSurplus() < -50.0f) {
        city.setFoodSurplus(-50.0f);
    }

    // Starvation
    if (city.foodSurplus() < -30.0f && city.population() > 1) {
        city.growPopulation(-1);
        city.setFoodSurplus(0.0f);
        if (city.workedTiles().size() > 1) {
            city.removeWorker(city.workedTiles().back());
        }
        LOG_WARN("%s lost population (starvation), now %d",
                 city.name().c_str(), city.population());
    }
}

void processCityGrowth(aoc::game::Player& player, const aoc::map::HexGrid& grid) {
    // Check if player has researched Feudalism civic (CivicId{6}) for farm adjacency bonus
    bool hasFeudalismCivic = player.civics().hasCompleted(CivicId{6});

    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        // Happiness for celebration growth: read from CityHappinessComponent (synced from ECS).
        // Uses previous turn's happiness since happiness is computed after growth.
        float cityHappiness = city->happiness().happiness;
        processSingleCityGrowth(*city, grid, hasFeudalismCivic, cityHappiness);
    }
}

} // namespace aoc::sim
