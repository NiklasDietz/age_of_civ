/**
 * @file CityGrowth.cpp
 * @brief City population growth logic.
 */

#include "aoc/simulation/city/CityGrowth.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace aoc::sim {

namespace {

/// Food resource IDs counted for the diet-diversity growth bonus.
/// Interchained trade intent: stockpiled goods also count, so civs that
/// import unfamiliar foods (via trade) benefit alongside civs that have
/// them on native tiles.
constexpr std::array<uint16_t, 6> kFoodGoodIds = {
    goods::WHEAT, goods::CATTLE, goods::FISH,
    goods::RICE,  goods::SUGAR,  goods::PROCESSED_FOOD,
};

/// Count distinct food sources available to a city.
/// Sources:
///   1. Food-resource tiles the city is currently working.
///   2. Food goods with positive amount in the city stockpile.
int32_t countDistinctFoodSources(const aoc::game::City& city,
                                   const aoc::map::HexGrid& grid) {
    std::array<bool, kFoodGoodIds.size()> present{};
    auto markPresent = [&](uint16_t gid) {
        for (std::size_t i = 0; i < kFoodGoodIds.size(); ++i) {
            if (kFoodGoodIds[i] == gid) { present[i] = true; return; }
        }
    };
    for (const aoc::hex::AxialCoord& tile : city.workedTiles()) {
        if (!grid.isValid(tile)) { continue; }
        const ResourceId r = grid.resource(grid.toIndex(tile));
        if (r.isValid()) { markPresent(r.value); }
    }
    for (std::size_t i = 0; i < kFoodGoodIds.size(); ++i) {
        if (present[i]) { continue; }
        if (city.stockpile().getAmount(kFoodGoodIds[i]) > 0) { present[i] = true; }
    }
    int32_t count = 0;
    for (bool p : present) { if (p) { ++count; } }
    return count;
}

} // namespace

float foodForGrowth(int32_t currentPopulation) {
    float pop = static_cast<float>(currentPopulation);
    float base = 15.0f + 6.0f * pop + std::pow(pop, 1.3f);
    return base * GamePace::instance().growthMultiplier;
}

int32_t computeCityHousing(const aoc::game::City& city, const aoc::map::HexGrid& grid) {
    int32_t housing = 4;
    if (city.hasBuilding(BuildingId{15})) { housing += 2; }  // Granary
    if (city.hasBuilding(BuildingId{22})) { housing += 4; }  // Hospital
    if (city.hasBuilding(BuildingId{42})) { housing += 4; }  // Aqueduct
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
    housing += std::min(farmCount / 2, 4);
    return housing;
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

static float computeWorkedFood(const aoc::game::City& city,
                               const aoc::game::Player& player,
                               const aoc::map::HexGrid& grid,
                               bool hasFeudalismCivic,
                               float climateFoodMult = 1.0f) {
    float total = 0.0f;
    const aoc::sim::CivilizationDef& civSpec = aoc::sim::civDef(player.civId());
    const int32_t foodFromRiver = civSpec.modifiers.foodFromRiver;
    for (const aoc::hex::AxialCoord& tileCoord : city.workedTiles()) {
        if (!grid.isValid(tileCoord)) { continue; }
        int32_t tileIndex = grid.toIndex(tileCoord);
        // WP-G adjacency cluster bonuses fold into the base tile yield.
        aoc::map::TileYield yield = effectiveTileYield(grid, tileIndex);
        float tileFood = static_cast<float>(yield.food);
        if (tileCoord == city.location() && tileFood < 2.0f) {
            tileFood = 2.0f;
        }
        if (hasFeudalismCivic) {
            tileFood += static_cast<float>(computeFarmAdjacencyBonus(grid, tileIndex));
        }
        // Conditional foodFromRiver: any of 6 river edges qualifies.
        if (foodFromRiver > 0 && grid.riverEdges(tileIndex) != 0u) {
            tileFood += static_cast<float>(foodFromRiver);
        }
        total += tileFood;
    }
    // Climate food penalty: late-stage CO2 reduces yield (≥ industrial).
    return total * climateFoodMult;
}

static void processSingleCityGrowth(aoc::game::City& city,
                                     const aoc::game::Player& player,
                                     const aoc::map::HexGrid& grid,
                                     bool hasFeudalismCivic,
                                     float cityHappiness,
                                     float climateFoodMult) {
    // Deficit-triggered reassignment. Workers locked on resource tiles at
    // founding (silver/copper/mountain metal bonuses) stay there even after
    // pop growth outstrips food supply, producing chronic starvation yo-yos
    // (2835 starvation events / 5 sims before this gate). When worked food
    // falls below 85% of consumption, rebalance in Food focus so existing
    // citizens can migrate off resources onto farms.
    {
        float totalFoodPre = computeWorkedFood(city, player, grid, hasFeudalismCivic, climateFoodMult);
        float consumption  = static_cast<float>(city.population()) * 2.0f;
        if (consumption > 0.0f && totalFoodPre < consumption * 0.85f) {
            city.autoAssignWorkers(grid, aoc::sim::WorkerFocus::Food, &player);
        }
    }

    // Calculate food from worked tiles (post-reassignment)
    float totalFood = computeWorkedFood(city, player, grid, hasFeudalismCivic, climateFoodMult);

    // Food consumption: 2 per citizen
    float consumption = static_cast<float>(city.population()) * 2.0f;
    float surplus = totalFood - consumption;

    // Diet diversity: cities with multiple food sources (native tiles +
    // imported goods) grow faster. Encourages interchained trade, since
    // importing unfamiliar foods from partners boosts your own growth.
    // +5% surplus per extra source, cap at +20% (5 distinct sources).
    if (surplus > 0.0f) {
        const int32_t foodVariety = countDistinctFoodSources(city, grid);
        if (foodVariety >= 2) {
            const float bonus = 0.05f
                * static_cast<float>(std::min(foodVariety - 1, 4));
            surplus *= (1.0f + bonus);
        }
    }

    // Celebration growth (rapture): very happy cities get +50% food surplus
    if (cityHappiness >= 3.0f && surplus > 0.0f) {
        surplus *= 1.5f;
    }

    // Housing cap: independent of food. Above housing, growth throttles;
    // 4 past housing, growth stops. Models Civ6 housing mechanic without
    // a separate resource — reuses building presence + nearby farms.
    const int32_t housing = computeCityHousing(city, grid);
    {
        const int32_t excess = city.population() - housing;
        if (excess >= 4) {
            surplus = 0.0f;
        } else if (excess >= 1) {
            // 1 → *0.75, 2 → *0.50, 3 → *0.25
            surplus *= (1.0f - 0.25f * static_cast<float>(excess));
        }
    }

    // C33: consumer-goods/wheat shortfall penalty. EconomySimulation set
    // foodShortfallRatio from unmet WHEAT demand this turn. Scales surplus
    // down (ratio 1.0 = full starvation, no growth). Force small deficit at
    // full shortfall so repeated starvation eventually shrinks the city.
    {
        const float shortfall = city.foodShortfallRatio();
        if (shortfall > 0.0f) {
            if (surplus > 0.0f) {
                surplus *= std::max(0.0f, 1.0f - shortfall);
            }
            if (shortfall >= 0.8f) {
                surplus -= 4.0f;  // fast decay into starvation clamp
            }
        }
    }

    // --- Food as tradeable good ---
    // Positive surplus: grows the city, but excess above 2x growth need
    // is converted to stockpiled Wheat goods (can be sold on the market).
    // Negative surplus: before starvation, consume Wheat from city stockpile.
    constexpr uint16_t WHEAT_GOOD_ID = 40;
    // WP-Q: 0.5 was too tight given WP-P military food consumption (foot 1/turn,
    // cavalry 2/turn, armor 3/turn). Bump to 3.0 so cities export ~6x more
    // wheat into stockpile → armies can be fed without farm tile spam.
    constexpr float FOOD_TO_GOODS_RATIO = 3.0f;

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
    if (city.foodSurplus() >= needed && city.population() < housing) {
        // H4.1: housing cap binds population (not just throttles surplus).
        // Growth blocked when already at or above cap so the prior turn's
        // excess surplus can't push pop past housing by 1 on the next tick.
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
            // Deficit-aware food weighting: when worked tiles can't feed the
            // post-growth pop, crank up food weight so the new citizen is
            // placed on a food tile. Cities repeatedly starved into pop=2
            // yo-yos (2835 starvation events / 5 sims) because resource tiles
            // outscored pure food even under deficit.
            const float consumptionNow =
                static_cast<float>(city.population()) * 2.0f;
            const bool foodDeficit = (totalFood < consumptionNow);
            const float foodWeight = foodDeficit ? 6.0f : 2.0f;
            float value = static_cast<float>(yield.food) * foodWeight
                        + static_cast<float>(yield.production)
                        + static_cast<float>(yield.gold) * 0.5f
                        + static_cast<float>(yield.science) * 0.5f;
            // Resource tiles get a priority bonus: they feed production chains
            // (copper ore → coins, iron ore → ingots, etc.) and without this bonus
            // cities deprioritise them in favour of pure food tiles, starving the
            // economy of raw materials.  Matches the +5 bonus applied at founding.
            // Under food deficit we damp the resource bonus so starving cities
            // don't chase ore/metal tiles and collapse their own pop.
            if (grid.resource(idx).isValid()) {
                const float resScale = foodDeficit ? 0.25f : 1.0f;
                value += 3.0f * resScale;
                // Minting ores get an extra +8 so they are worked before high-food
                // tiles like cattle (food=4, base=12) and are assigned by pop=2.
                const uint16_t resId = grid.resource(idx).value;
                if (resId == aoc::sim::goods::COPPER_ORE
                    || resId == aoc::sim::goods::SILVER_ORE) {
                    value += 8.0f * resScale;
                }
                // Mountain metal tiles have zero terrain yield, so without a boost
                // they would never beat food tiles. Keep them competitive.
                if (grid.terrain(idx) == aoc::map::TerrainType::Mountain
                    && aoc::sim::isMountainMetal(resId)) {
                    value += 6.0f * resScale;
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

void processCityGrowth(aoc::game::Player& player, const aoc::map::HexGrid& grid,
                       float climateFoodMult) {
    // Check if player has researched Feudalism civic (CivicId{6}) for farm adjacency bonus
    bool hasFeudalismCivic = player.civics().hasCompleted(CivicId{6});

    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        // Happiness for celebration growth: read from CityHappinessComponent (synced from ECS).
        // Uses previous turn's happiness since happiness is computed after growth.
        float cityHappiness = city->happiness().happiness;
        processSingleCityGrowth(*city, player, grid, hasFeudalismCivic,
                                cityHappiness, climateFoodMult);
    }
}

} // namespace aoc::sim
