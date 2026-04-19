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
    nearby.reserve(64);
    aoc::hex::spiral(city.location, CITY_WORK_RADIUS, std::back_inserter(nearby));

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
                                     const aoc::game::Player& player,
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

    // Housing cap: independent of food. Above housing, growth throttles;
    // 4 past housing, growth stops. Models Civ6 housing mechanic without
    // a separate resource — reuses building presence + nearby farms.
    // Base 4 (city center). Granary +2, Hospital +4.
    // Farms in city radius contribute 0.5 housing each (capped at +4).
    {
        int32_t housing = 4;
        if (city.hasBuilding(BuildingId{15})) { housing += 2; }  // Granary
        if (city.hasBuilding(BuildingId{22})) { housing += 4; }  // Hospital
        if (city.hasBuilding(BuildingId{42})) { housing += 4; }  // Aqueduct
        // Count farm improvements inside the city's workable radius.
        int32_t farmCount = 0;
        std::vector<aoc::hex::AxialCoord> nearby;
        nearby.reserve(64);
        aoc::hex::spiral(city.location(), CITY_WORK_RADIUS, std::back_inserter(nearby));
        for (const aoc::hex::AxialCoord& t : nearby) {
            if (!grid.isValid(t)) { continue; }
            const int32_t ti = grid.toIndex(t);
            if (grid.owner(ti) != city.owner()) { continue; }
            if (grid.improvement(ti) == aoc::map::ImprovementType::Farm) {
                ++farmCount;
            }
        }
        const int32_t farmHousing = std::min(farmCount / 2, 4);  // 2 farms → +1 housing, cap +4
        housing += farmHousing;
        const int32_t excess = city.population() - housing;
        if (excess >= 4) {
            surplus = 0.0f;
        } else if (excess >= 1) {
            // 1 → *0.75, 2 → *0.50, 3 → *0.25
            surplus *= (1.0f - 0.25f * static_cast<float>(excess));
        }
    }

    // --- Food as tradeable good ---
    // Positive surplus: grows the city, but excess above 2x growth need
    // is converted to stockpiled Wheat goods (can be sold on the market).
    // Negative surplus: before starvation, consume Wheat from city stockpile.
    constexpr uint16_t WHEAT_GOOD_ID = 40;
    constexpr float FOOD_TO_GOODS_RATIO = 0.5f;  // 2 surplus food → 1 Wheat good

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
        // Deficit: consume any edible good from the city stockpile to offset
        // food shortage. Processed food is most nutritious per unit (2.0x);
        // raw staples fall back to 1.5x. Ordered most-valuable-first so raw
        // staples are held in reserve when processed food is available.
        struct FoodGood { uint16_t id; float ratio; };
        constexpr FoodGood kFoods[] = {
            {uint16_t{70},  2.0f},   // PROCESSED_FOOD
            {uint16_t{40},  1.5f},   // WHEAT
            {uint16_t{41},  1.5f},   // CATTLE
            {uint16_t{42},  1.5f},   // FISH
            {uint16_t{45},  1.5f},   // RICE
        };
        for (const FoodGood& fg : kFoods) {
            if (surplus >= 0.0f) { break; }
            const int32_t avail = city.stockpile().getAmount(fg.id);
            if (avail <= 0) { continue; }
            const float deficit = -surplus;
            const int32_t toConsume = std::min(
                avail,
                static_cast<int32_t>(deficit / fg.ratio) + 1);
            if (toConsume <= 0) { continue; }
            const float recovered = static_cast<float>(toConsume) * fg.ratio;
            [[maybe_unused]] bool consumed = city.stockpile().consumeGoods(fg.id, toConsume);
            surplus += recovered;
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

        // Stage promotion ladder:
        //   Hamlet  -> Village at pop 3
        //   Village -> Town    at pop 6 AND Granary built (BuildingId{15})
        //   Town    -> City    at pop 11 AND any Tier-2 bldg (Library 16 / Market 11 / Temple 37)
        const int32_t pop = city.population();
        switch (city.stage()) {
            case aoc::game::CitySize::Hamlet:
                if (pop >= 3) { city.setStage(aoc::game::CitySize::Village); }
                break;
            case aoc::game::CitySize::Village:
                if (pop >= 6 && city.hasBuilding(BuildingId{15})) {
                    city.setStage(aoc::game::CitySize::Town);
                }
                break;
            case aoc::game::CitySize::Town:
                if (pop >= 11
                    && (city.hasBuilding(BuildingId{16})
                     || city.hasBuilding(BuildingId{11})
                     || city.hasBuilding(BuildingId{37}))) {
                    city.setStage(aoc::game::CitySize::City);
                }
                break;
            case aoc::game::CitySize::City:
                break;
        }

        // Auto-assign new citizen to the best unworked owned tile. No fixed
        // radius: scan all player-owned tiles up to a sanity cap, honour the
        // per-tile city override if present, else nearest-city rule.
        constexpr int32_t GROWTH_SANITY_CAP = 12;
        aoc::hex::AxialCoord center = city.location();

        float bestYieldValue = -1.0f;
        aoc::hex::AxialCoord bestTile = center;
        bool foundNew = false;

        const int32_t totalTiles = grid.tileCount();
        for (int32_t idx = 0; idx < totalTiles; ++idx) {
            if (grid.owner(idx) != city.owner()) { continue; }
            const aoc::hex::AxialCoord tile = grid.toAxial(idx);
            if (tile == center) { continue; }
            if (grid.distance(tile, center) > GROWTH_SANITY_CAP) { continue; }

            // Already worked?
            bool alreadyWorked = false;
            for (const aoc::hex::AxialCoord& worked : city.workedTiles()) {
                if (worked == tile) {
                    alreadyWorked = true;
                    break;
                }
            }
            if (alreadyWorked) { continue; }

            // Terrain viability (matches the old check).
            const aoc::map::TerrainType nTerrain = grid.terrain(idx);
            if (aoc::map::isWater(nTerrain)) { continue; }
            if (aoc::map::isImpassable(nTerrain)) {
                const ResourceId mRes = grid.resource(idx);
                const bool workableMountain =
                    (nTerrain == aoc::map::TerrainType::Mountain
                     && mRes.isValid()
                     && aoc::sim::isMountainMetal(mRes.value));
                if (!workableMountain) { continue; }
            }

            // City-assignment filter: override wins; else nearest city wins.
            const aoc::hex::AxialCoord* override = player.tileCityOverride(idx);
            if (override != nullptr) {
                if (*override != center) { continue; }
            } else {
                int32_t bestDist = std::numeric_limits<int32_t>::max();
                aoc::hex::AxialCoord bestLoc{};
                for (const std::unique_ptr<aoc::game::City>& other : player.cities()) {
                    if (other == nullptr) { continue; }
                    const int32_t d = grid.distance(tile, other->location());
                    if (d < bestDist) {
                        bestDist = d;
                        bestLoc = other->location();
                    }
                }
                if (bestLoc != center) { continue; }
            }

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
        processSingleCityGrowth(*city, player, grid, hasFeudalismCivic, cityHappiness);
    }
}

} // namespace aoc::sim
