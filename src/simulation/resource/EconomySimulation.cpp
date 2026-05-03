/**
 * @file EconomySimulation.cpp
 * @brief Per-turn economic simulation implementation.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/balance/BalanceParams.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/economy/ResourceCurse.hpp"
#include "aoc/simulation/economy/InternalTrade.hpp"
#include "aoc/simulation/economy/EnvironmentModifier.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/simulation/monetary/FiscalPolicy.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/economy/AdvancedEconomics.hpp"
#include "aoc/simulation/monetary/CentralBank.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/CurrencyWar.hpp"
#include "aoc/simulation/monetary/ForexMarket.hpp"
#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/simulation/economy/Sanctions.hpp"
#include "aoc/simulation/economy/Speculation.hpp"
#include "aoc/simulation/economy/ColonialEconomics.hpp"
#include "aoc/simulation/economy/EconomicDepth.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/simulation/production/ProductionEfficiency.hpp"
#include "aoc/simulation/production/BuildingCapacity.hpp"
#include "aoc/simulation/production/QualityTier.hpp"
#include "aoc/simulation/production/Waste.hpp"
#include "aoc/simulation/production/PowerGrid.hpp"
#include "aoc/simulation/production/Automation.hpp"
#include "aoc/map/HexGrid.hpp"

#include <algorithm>
#include <unordered_set>

namespace aoc::sim {

EconomySimulation::EconomySimulation() = default;

static inline uint64_t makePreferenceKey(PlayerId owner, uint32_t cityLocHash, uint16_t buildingId) {
    return (static_cast<uint64_t>(owner) << 48)
         | (static_cast<uint64_t>(cityLocHash) << 16)
         | static_cast<uint64_t>(buildingId);
}

void EconomySimulation::setRecipePreference(PlayerId owner, uint32_t cityLocHash,
                                             uint16_t buildingId, uint16_t recipeId) {
    const uint64_t key = makePreferenceKey(owner, cityLocHash, buildingId);
    if (recipeId == 0xFFFFu) {
        this->m_recipePreference.erase(key);
    } else {
        this->m_recipePreference[key] = recipeId;
    }
}

uint16_t EconomySimulation::recipePreference(PlayerId owner, uint32_t cityLocHash,
                                              uint16_t buildingId) const {
    const uint64_t key = makePreferenceKey(owner, cityLocHash, buildingId);
    const auto it = this->m_recipePreference.find(key);
    return (it == this->m_recipePreference.end()) ? 0xFFFFu : it->second;
}

void EconomySimulation::initialize() {
    this->m_productionChain.build();
    this->m_market.initialize();

    LOG_INFO("Initialized: %zu recipes in production chain, %u goods on market",
             this->m_productionChain.executionOrder().size(),
             static_cast<unsigned>(goodCount()));
}

void EconomySimulation::executeTurn(aoc::game::GameState& gameState, aoc::map::HexGrid& grid) {
    this->harvestResources(gameState, grid);
    this->applyResourceDepletion(gameState, grid);
    this->processInternalTradeForAllPlayers(gameState, grid);
    this->consumeBuildingFuel(gameState, grid);

    // C40: stamp resource-curse modifiers onto each player before production
    // so manufacturingPenalty is live when executeProduction runs.
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        const ResourceCurseModifiers mods = computeResourceCurse(gameState, playerPtr->id());
        applyResourceCurseEffects(gameState, playerPtr->id(), mods);
    }

    this->executeProduction(gameState, grid);

    // WP-O: stockpile soft-cap → auto-commit surplus to export buffer.
    // Frees stockpile space so production keeps running. Buffer drains
    // when traders pick goods up. Stale buffer entries (no pickup for
    // N turns) trickle back to stockpile up to cap; the rest is lost
    // to warehouse spoilage.
    {
        const aoc::balance::BalanceParams& bal = aoc::balance::params();
        const int32_t cap = bal.stockpileSoftCap;
        constexpr int32_t BUFFER_STALE_TURNS = 30;
        // Skip food + late-game strategics (preserve Mars chain).
        auto isExempt = [](uint16_t gid) {
            return gid == goods::WHEAT || gid == goods::CATTLE
                || gid == goods::FISH  || gid == goods::RICE
                || gid == goods::PROCESSED_FOOD
                || gid == goods::LITHIUM || gid == goods::RARE_EARTH
                || gid == goods::TITANIUM || gid == goods::HELIUM_3;
        };
        for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
            if (p == nullptr) { continue; }
            for (const std::unique_ptr<aoc::game::City>& c : p->cities()) {
                if (c == nullptr) { continue; }
                CityStockpileComponent& sp = c->stockpile();
                // Commit surplus to export buffer.
                for (auto& kv : sp.goods) {
                    if (kv.second <= cap) { continue; }
                    if (isExempt(kv.first)) { continue; }
                    const int32_t excess = kv.second - cap;
                    sp.exportBuffer[kv.first] += excess;
                    sp.exportBufferIdleTurns[kv.first] = 0;
                    kv.second = cap;
                }
                // Tick idle counter for buffer entries that didn't move.
                // Stale entries: spill back to stockpile (capped); lose excess.
                std::vector<uint16_t> drained;
                for (auto& kv : sp.exportBuffer) {
                    sp.exportBufferIdleTurns[kv.first] += 1;
                    if (sp.exportBufferIdleTurns[kv.first] >= BUFFER_STALE_TURNS) {
                        const int32_t free = std::max(0, cap - sp.getAmount(kv.first));
                        const int32_t back = std::min(kv.second, free);
                        if (back > 0) {
                            sp.goods[kv.first] += back;
                        }
                        // Anything not returned is lost (spoilage).
                        kv.second = 0;
                        sp.exportBufferIdleTurns[kv.first] = 0;
                        drained.push_back(kv.first);
                    }
                }
                for (uint16_t gid : drained) {
                    sp.exportBuffer.erase(gid);
                    sp.exportBufferIdleTurns.erase(gid);
                }
            }
        }
    }

    this->reportToMarket(gameState);
    this->computePlayerNeeds(gameState);
    this->m_market.updatePrices();
    this->executeTradeRoutes(gameState);
    this->settleTradeInCoins(gameState);
    this->updateCoinReservesFromStockpiles(gameState);
    this->tickMonetaryMechanics(gameState);
    this->processCrisisAndBonds(gameState);
    this->processEconomicZonesAndSpeculation(gameState, grid);
    this->executeMonetaryPolicy(gameState);
}

// ============================================================================
// Step 1: Harvest raw resources from worked tiles into city stockpiles
// ============================================================================

void EconomySimulation::harvestResources(aoc::game::GameState& gameState,
                                          aoc::map::HexGrid& grid) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }

            CityStockpileComponent& stockpile = cityPtr->stockpile();

            for (const aoc::hex::AxialCoord& tileCoord : cityPtr->workedTiles()) {
                if (!grid.isValid(tileCoord)) {
                    continue;
                }
                int32_t tileIndex = grid.toIndex(tileCoord);

                aoc::map::ImprovementType imp = grid.improvement(tileIndex);
                uint16_t cultivatedGood = 0;
                bool isCultivated = false;
                switch (imp) {
                    case aoc::map::ImprovementType::Vineyard:    cultivatedGood = goods::WINE;   isCultivated = true; break;
                    case aoc::map::ImprovementType::SilkFarm:    cultivatedGood = goods::SILK;   isCultivated = true; break;
                    case aoc::map::ImprovementType::SpiceFarm:   cultivatedGood = goods::SPICES; isCultivated = true; break;
                    case aoc::map::ImprovementType::DyeWorks:    cultivatedGood = goods::DYES;   isCultivated = true; break;
                    case aoc::map::ImprovementType::CottonField: cultivatedGood = goods::COTTON; isCultivated = true; break;
                    default: break;
                }
                if (isCultivated) {
                    stockpile.addGoods(cultivatedGood, 2);
                }

                // WP-C4 Greenhouse production: planted-crop output at 50%.
                // Encoded as 1 unit every 2 turns via `(turn & 1) == 0`. Turn
                // parity keyed off the city's location hash so greenhouses
                // in different cities stagger their output. This gives
                // cold-climate civs a way to grow tropical crops they got
                // through trade.
                if (imp == aoc::map::ImprovementType::Greenhouse) {
                    const uint16_t planted = grid.greenhouseCrop(tileIndex);
                    if (planted != 0xFFFFu && planted < goodCount()) {
                        const uint32_t parity =
                            static_cast<uint32_t>(gameState.currentTurn())
                          ^ (static_cast<uint32_t>(cityPtr->location().q) * 73u)
                          ^ (static_cast<uint32_t>(cityPtr->location().r) * 31u);
                        if ((parity & 1u) == 0u) {
                            stockpile.addGoods(planted, 1);
                        }
                    }
                }

                ResourceId resId = grid.resource(tileIndex);
                if (!resId.isValid()) {
                    continue;
                }

                uint16_t goodId = resId.value;
                if (goodId >= goodCount()) {
                    continue;
                }

                TechId revealTech = resourceRevealTech(goodId);
                if (revealTech.isValid()) {
                    if (!playerPtr->tech().hasResearched(revealTech)) {
                        continue;
                    }
                }

                int32_t yield = 1;
                if (imp == aoc::map::ImprovementType::Mine
                    || imp == aoc::map::ImprovementType::MountainMine) {
                    yield = 2;
                } else if (imp == aoc::map::ImprovementType::Plantation
                           || imp == aoc::map::ImprovementType::Camp
                           || imp == aoc::map::ImprovementType::Pasture) {
                    yield = 2;
                }

                // Per-good yield bumps so tile extraction reflects real-world
                // density asymmetry.  A seam of coal yields more per worked
                // tile than wood for charcoal production; gold/silver yield
                // little because ore veins are thin.  Charcoal is not a tile
                // resource — it's a processed good from recipe 38 — so its
                // effective per-turn rate is already bounded by 3 Wood tiles.
                if (imp == aoc::map::ImprovementType::Mine
                    || imp == aoc::map::ImprovementType::MountainMine) {
                    switch (goodId) {
                        case goods::COAL:       yield = 3; break;  // thick seams
                        case goods::IRON_ORE:   yield = 2; break;
                        case goods::COPPER_ORE: yield = 2; break;
                        case goods::STONE:      yield = 3; break;  // quarry
                        case goods::GOLD_ORE:   yield = 1; break;  // veins thin
                        case goods::SILVER_ORE: yield = 1; break;
                        case goods::NITER:      yield = 1; break;
                        case goods::URANIUM:    yield = 1; break;  // trace
                        default: break;
                    }
                }

                int16_t currentReserves = grid.reserves(tileIndex);
                if (currentReserves >= 0) {
                    int32_t actualYield = std::min(yield, static_cast<int32_t>(currentReserves));
                    if (actualYield <= 0) {
                        continue;
                    }
                    grid.setReserves(tileIndex, static_cast<int16_t>(currentReserves - actualYield));
                    if (grid.reserves(tileIndex) <= 0) {
                        grid.setResource(tileIndex, ResourceId{});
                        grid.setReserves(tileIndex, 0);
                        LOG_INFO("Resource exhausted at tile (%d,%d): %.*s depleted after extraction",
                                 tileCoord.q, tileCoord.r,
                                 static_cast<int>(goodDef(goodId).name.size()),
                                 goodDef(goodId).name.data());
                    }
                    stockpile.addGoods(goodId, actualYield);
                } else {
                    stockpile.addGoods(goodId, yield);
                }
            }
        }
    }
}

// ============================================================================
// Step 1b: Consume ongoing fuel for buildings
// ============================================================================

void EconomySimulation::consumeBuildingFuel(aoc::game::GameState& gameState,
                                             const aoc::map::HexGrid& grid) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }

            const CityDistrictsComponent& districts = cityPtr->districts();
            CityStockpileComponent&       stockpile  = cityPtr->stockpile();

            // WP-B2/B3 Lunar Colony mining stream — independent of Fusion
            // Reactor. Audit 2026-04 found the Ti/He3 delivery was gated on
            // Fusion Reactor (TechId 28 = Fusion Power, rarely reached in
            // 1000t sims), so Mars gate never cleared. Decoupled: any city
            // with a Semiconductor Fab (refines lunar ore) ships Ti + Rare
            // Earth post-Lunar-Colony, and He3 flows to any city once Moon
            // Landing completes so Mars Colony stockpile can accumulate.
            {
                const aoc::sim::PlayerSpaceRaceComponent& sr = playerPtr->spaceRace();
                const bool moonLanded =
                    sr.completed[static_cast<int32_t>(aoc::sim::SpaceProjectId::MoonLanding)];
                const bool lunarColony =
                    sr.completed[static_cast<int32_t>(aoc::sim::SpaceProjectId::LunarColony)];
                if (moonLanded) {
                    stockpile.addGoods(goods::HELIUM_3, lunarColony ? 3 : 1);
                }
                // Lunar mining delivers Titanium + Rare Earth to any city
                // post-Lunar-Colony. Originally gated on Semi Fab only, but
                // audit showed Semi Fabs are too sparse (avg 1.4/sim) for
                // Mars to progress. Now every city receives the shipment.
                if (lunarColony) {
                    stockpile.addGoods(goods::TITANIUM, 1);
                    stockpile.addGoods(goods::RARE_EARTH, 1);
                }
            }

            // Fusion Reactor fuel supply. He3 / Ti / RARE_EARTH delivery
            // handled above — this branch only covers the Deuterium
            // fallback for pre-Moon-Landing Fusion Reactor cities.
            if (districts.hasBuilding(BuildingId{35})) {
                const aoc::sim::PlayerSpaceRaceComponent& sr = playerPtr->spaceRace();
                const bool moonLanded =
                    sr.completed[static_cast<int32_t>(aoc::sim::SpaceProjectId::MoonLanding)];
                if (moonLanded) {
                    // He3 already added in outer lunar block; nothing to do.
                } else {
                    bool isCoastal = false;
                    std::array<aoc::hex::AxialCoord, 6> neighbors =
                        aoc::hex::neighbors(cityPtr->location());
                    for (const aoc::hex::AxialCoord& nbr : neighbors) {
                        if (grid.isValid(nbr)) {
                            int32_t nbrIdx = grid.toIndex(nbr);
                            if (aoc::map::isWater(grid.terrain(nbrIdx))) {
                                isCoastal = true;
                                break;
                            }
                        }
                    }
                    if (isCoastal) {
                        stockpile.addGoods(goods::DEUTERIUM, 1);
                    }
                }
            }

            // C43: during an oil shock, oil-burning buildings fall back to
            // coal at 1.5x consumption if coal is available. Without this,
            // the peak-oil flag was a tag with no mechanical effect — civs
            // simply stopped powering oil plants and carried on.
            //
            // C42: when the local stockpile lacks fuel, fall back to any
            // sibling city in the same empire. Before this, each city's
            // buildings stalled the moment its own stockpile ran dry even
            // if the neighbouring city was sitting on surplus -- in effect
            // the empire was strip-mined city-by-city without any internal
            // logistics. The fallback models intra-empire transport.
            const bool inShock = playerPtr->energy().inOilShock;
            for (const CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
                for (BuildingId bid : district.buildings) {
                    const BuildingDef& bdef = buildingDef(bid);
                    if (!bdef.needsFuel()) { continue; }

                    const int32_t needed = bdef.ongoingFuelPerTurn;
                    const int32_t local  = stockpile.getAmount(bdef.ongoingFuelGoodId);
                    if (local >= needed) {
                        [[maybe_unused]] bool ok =
                            stockpile.consumeGoods(bdef.ongoingFuelGoodId, needed);
                        continue;
                    }

                    // Check empire-wide pool across sibling cities before
                    // consuming anything. All-or-nothing semantics keep the
                    // existing "stalled = no fuel burned" contract intact.
                    int32_t sibling = 0;
                    for (const std::unique_ptr<aoc::game::City>& donorPtr : playerPtr->cities()) {
                        if (donorPtr == nullptr || donorPtr.get() == cityPtr.get()) {
                            continue;
                        }
                        sibling += donorPtr->stockpile().getAmount(bdef.ongoingFuelGoodId);
                    }
                    if (local + sibling >= needed) {
                        if (local > 0) {
                            [[maybe_unused]] bool ok =
                                stockpile.consumeGoods(bdef.ongoingFuelGoodId, local);
                        }
                        int32_t remaining = needed - local;
                        for (const std::unique_ptr<aoc::game::City>& donorPtr : playerPtr->cities()) {
                            if (remaining <= 0) { break; }
                            if (donorPtr == nullptr || donorPtr.get() == cityPtr.get()) {
                                continue;
                            }
                            CityStockpileComponent& donorStock = donorPtr->stockpile();
                            const int32_t donorAvail =
                                donorStock.getAmount(bdef.ongoingFuelGoodId);
                            if (donorAvail <= 0) { continue; }
                            const int32_t take = std::min(donorAvail, remaining);
                            [[maybe_unused]] bool ok =
                                donorStock.consumeGoods(bdef.ongoingFuelGoodId, take);
                            remaining -= take;
                        }
                        continue;
                    }

                    // True shortage: apply the oil-shock coal substitute.
                    if (inShock && bdef.ongoingFuelGoodId == goods::OIL) {
                        const int32_t coalNeeded = (needed * 3 + 1) / 2;
                        if (stockpile.getAmount(goods::COAL) >= coalNeeded) {
                            [[maybe_unused]] bool ok =
                                stockpile.consumeGoods(goods::COAL, coalNeeded);
                        }
                    }
                }
            }
        }
    }
}

// ============================================================================
// Step 1c: Compute per-player resource needs
// ============================================================================

void EconomySimulation::computePlayerNeeds(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        PlayerEconomyComponent& econ = playerPtr->economy();
        econ.totalNeeds.clear();

        // Aggregate stockpile across all cities
        std::unordered_map<uint16_t, int32_t> totalStock;
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            for (const std::pair<const uint16_t, int32_t>& entry : cityPtr->stockpile().goods) {
                totalStock[entry.first] += entry.second;
            }
        }

        // Recipe input needs
        for (const ProductionRecipe& recipe : allRecipes()) {
            bool hasBuildingSomewhere = false;
            for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
                if (cityPtr == nullptr) { continue; }
                if (cityPtr->districts().hasBuilding(recipe.requiredBuilding)) {
                    hasBuildingSomewhere = true;
                    break;
                }
            }
            if (!hasBuildingSomewhere) { continue; }

            for (const RecipeInput& input : recipe.inputs) {
                int32_t have = 0;
                std::unordered_map<uint16_t, int32_t>::iterator it = totalStock.find(input.goodId);
                if (it != totalStock.end()) { have = it->second; }
                int32_t deficit = input.amount - have;
                if (deficit > 0) {
                    econ.totalNeeds[input.goodId] += deficit;
                }
            }
        }

        // Building fuel needs
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            for (const CityDistrictsComponent::PlacedDistrict& d : cityPtr->districts().districts) {
                for (BuildingId bid : d.buildings) {
                    const BuildingDef& bdef = buildingDef(bid);
                    if (bdef.needsFuel()) {
                        int32_t have = 0;
                        std::unordered_map<uint16_t, int32_t>::iterator it =
                            totalStock.find(bdef.ongoingFuelGoodId);
                        if (it != totalStock.end()) { have = it->second; }
                        if (have < bdef.ongoingFuelPerTurn) {
                            econ.totalNeeds[bdef.ongoingFuelGoodId] +=
                                (bdef.ongoingFuelPerTurn - have);
                        }
                    }
                }
            }
        }

        // Population-based consumption: citizens consume goods each turn.
        // This is the core demand driver that makes production meaningful.
        // Without it, goods pile up in stockpiles with no purpose.
        {
            const int32_t totalPop = playerPtr->totalPopulation();

            // Real interest rate scales discretionary demand. Necessities
            // (wheat, clothing, processed food) stay inelastic; consumer
            // goods and advanced consumer goods respond to monetary policy.
            const float luxuryMult = realRateConsumptionMultiplier(
                realInterestRate(playerPtr->monetary()));
            const auto scaleLuxury = [luxuryMult](int32_t n) {
                return std::max(0, static_cast<int32_t>(
                    static_cast<float>(n) * luxuryMult));
            };

            // Food: 1 Wheat per 3 citizens (supplementing tile food yields)
            econ.totalNeeds[goods::WHEAT] += totalPop / 3;

            // Consumer Goods: modern citizens expect manufactured products
            if (totalPop > 3) {
                econ.totalNeeds[goods::CONSUMER_GOODS]
                    += scaleLuxury((totalPop - 3) / 3 + 1);
            }

            // Processed Food: larger cities need processed food, not just raw wheat
            if (totalPop > 8) {
                econ.totalNeeds[goods::PROCESSED_FOOD] += (totalPop - 8) / 4 + 1;
            }

            // Clothing: all citizens need clothing
            econ.totalNeeds[goods::CLOTHING] += totalPop / 5 + 1;

            // Advanced Consumer Goods: wealthy large populations
            if (totalPop > 15) {
                econ.totalNeeds[goods::ADV_CONSUMER_GOODS]
                    += scaleLuxury((totalPop - 15) / 5 + 1);
            }

            // Actually consume these goods from stockpiles each turn
            // (not just register as demand — actually deplete them). C33:
            // partial-consume so unmet wheat demand maps to foodShortfallRatio
            // (consumed downstream by CityGrowth for starvation penalty).
            for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
                if (cityPtr == nullptr) { continue; }
                CityStockpileComponent& stock = cityPtr->stockpile();
                const int32_t cityPop = cityPtr->population();

                auto partialConsume = [&stock](uint16_t gid, int32_t want) -> int32_t {
                    if (want <= 0) { return 0; }
                    const int32_t have = stock.getAmount(gid);
                    const int32_t take = std::min(want, have);
                    if (take > 0) {
                        [[maybe_unused]] bool ok = stock.consumeGoods(gid, take);
                    }
                    return want - take;  // shortfall
                };

                const int32_t wheatWant = cityPop / 3;
                const int32_t wheatShort = partialConsume(goods::WHEAT, wheatWant);
                partialConsume(goods::CONSUMER_GOODS, (cityPop > 3) ? (cityPop - 3) / 4 : 0);
                partialConsume(goods::PROCESSED_FOOD, (cityPop > 5) ? (cityPop - 5) / 5 : 0);
                partialConsume(goods::CLOTHING, cityPop / 6);

                const float ratio = (wheatWant > 0)
                    ? static_cast<float>(wheatShort) / static_cast<float>(wheatWant)
                    : 0.0f;
                cityPtr->setFoodShortfallRatio(std::clamp(ratio, 0.0f, 1.0f));
            }
        }

        // Count unique luxuries
        constexpr uint16_t RAW_LUXURY_IDS[] = {
            goods::WINE, goods::SPICES, goods::SILK, goods::IVORY, goods::GEMS,
            goods::DYES, goods::FURS, goods::INCENSE, goods::SUGAR,
            goods::PEARLS, goods::TEA, goods::COFFEE, goods::TOBACCO
        };
        int32_t uniqueCount = 0;
        for (uint16_t luxId : RAW_LUXURY_IDS) {
            std::unordered_map<uint16_t, int32_t>::iterator it = totalStock.find(luxId);
            if (it != totalStock.end() && it->second > 0) {
                ++uniqueCount;
            } else {
                econ.totalNeeds[luxId] += 1;
            }
        }
        econ.uniqueLuxuryCount = uniqueCount;
    }
}

// ============================================================================
// Step 2: Execute production recipes
// ============================================================================

void EconomySimulation::executeProduction(aoc::game::GameState& gameState,
                                          aoc::map::HexGrid& grid) {
    // City is the authority for all subsystems: automation, quality, experience,
    // building levels, power, and strike are all owned by City objects.

    // --- Pass 1: per-city power + automation setup ---
    // Use a pointer-keyed map so we can correlate cities across both passes.
    std::unordered_map<aoc::game::City*, float> cityPowerEfficiency;

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }

            CityPowerComponent power = computeCityPower(gameState, grid, *cityPtr);
            power.energyDemand += cityPtr->automation().robotEnergyDemand();
            cityPowerEfficiency[cityPtr.get()] = power.powerEfficiency();
            // Cache for next turn's production penalty + UI readout.
            cityPtr->power() = power;

            if (power.hasNuclear) {
                // Use player id + city pointer address as a stable turn hash seed.
                uint32_t turnHash = this->m_depletionTurnCounter * 7919u
                    + static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cityPtr.get()));
                checkNuclearMeltdown(gameState, grid, *cityPtr, turnHash);
            }

            // Update robot automation directly on the city's owned components.
            CityStockpileComponent& stockpile    = cityPtr->stockpile();
            CityAutomationComponent& automation  = cityPtr->automation();
            int32_t robotsAvailable = stockpile.getAmount(ROBOT_WORKERS_GOOD);
            if (robotsAvailable <= 0) {
                automation.robotWorkers = 0;
            } else {
                automation.robotWorkers = robotsAvailable;
                ++automation.turnsSinceLastMaintenance;
                if (automation.turnsSinceLastMaintenance >= ROBOT_MAINTENANCE_INTERVAL) {
                    [[maybe_unused]] bool consumed = stockpile.consumeGoods(ROBOT_WORKERS_GOOD, 1);
                    automation.turnsSinceLastMaintenance = 0;
                    --automation.robotWorkers;
                }
            }
        }
    }

    // --- Pass 2: recipe execution ---
    struct CityProductionState {
        int32_t totalRecipesExecuted = 0;
        std::unordered_map<uint16_t, int32_t> buildingBatchesUsed;
    };
    std::unordered_map<aoc::game::City*, CityProductionState> cityState;

    // C1: rank recipes by market profitability so high-margin recipes win slot
    // budget and price signals actually drive production. Primary key:
    // profitability = (outputPrice * outputQty) / max(1, Σ inputPrice * inputQty).
    // Tie-break on slot cost (cheaper slots first). Topological order from
    // executionOrder() is used as the final stable fallback so same-profit
    // recipes still respect dep order where possible.
    struct RankedRecipe {
        const ProductionRecipe* recipe;
        float profitability;
        int32_t topoIndex;
    };
    std::vector<RankedRecipe> rankedRecipes;
    {
        const std::vector<const ProductionRecipe*>& order = this->m_productionChain.executionOrder();
        rankedRecipes.reserve(order.size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            const ProductionRecipe* r = order[i];
            if (r == nullptr) { continue; }
            const int32_t outPrice = this->m_market.price(r->outputGoodId);
            float revenue = static_cast<float>(outPrice) * static_cast<float>(r->outputAmount);
            float cost = 0.0f;
            for (const RecipeInput& input : r->inputs) {
                if (!input.consumed) { continue; }
                const int32_t ip = this->m_market.price(input.goodId);
                cost += static_cast<float>(ip) * static_cast<float>(input.amount);
            }
            float profit = revenue / std::max(1.0f, cost);
            rankedRecipes.push_back({r, profit, static_cast<int32_t>(i)});
        }
        std::stable_sort(rankedRecipes.begin(), rankedRecipes.end(),
            [](const RankedRecipe& a, const RankedRecipe& b) {
                if (a.profitability != b.profitability) {
                    return a.profitability > b.profitability;
                }
                if (a.recipe->workerSlots != b.recipe->workerSlots) {
                    return a.recipe->workerSlots < b.recipe->workerSlots;
                }
                return a.topoIndex < b.topoIndex;
            });
    }

    for (const RankedRecipe& ranked : rankedRecipes) {
        const ProductionRecipe* recipe = ranked.recipe;
        for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
            if (playerPtr == nullptr) { continue; }

            for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
                if (cityPtr == nullptr) { continue; }

                aoc::game::City* city = cityPtr.get();
                CityProductionState& state = cityState[city];

                if (city->strike().isOnStrike) {
                    continue;
                }

                int32_t robotSlots = city->automation().bonusRecipeSlots();
                // 2026-05-03: Industrial Revolution tier grants free robot
                // capacity. Singapore model — a tech-rich small civ can
                // out-produce a populous low-tech one without needing to
                // mass-produce Robot Worker goods first.
                //   IR #3 Digital Age: +3 free slots per city
                //   IR #4 Information Age: +6 (cumulative)
                //   IR #5 Post-Industrial: +10
                {
                    const auto rev = playerPtr->industrial().currentRevolution;
                    if (rev >= IndustrialRevolutionId::Third)        { robotSlots += 3; }
                    if (rev >= IndustrialRevolutionId::Fourth)       { robotSlots += 3; }
                    if (rev >= IndustrialRevolutionId::Fifth)        { robotSlots += 4; }
                }
                const int32_t maxSlots = totalWorkerCapacity(city->population(), robotSlots);
                // Each recipe consumes workerSlots (1 for basic, 2-3 for advanced).
                // A city can only run recipes whose total slots fit within capacity.
                if (state.totalRecipesExecuted + recipe->workerSlots > maxSlots) {
                    continue;
                }

                // Tech gate: skip recipe if the player hasn't researched the required tech
                if (recipe->requiredTech.isValid()
                    && !playerPtr->tech().hasResearched(recipe->requiredTech)) {
                    continue;
                }

                const CityDistrictsComponent& districts = city->districts();
                if (!districts.hasBuilding(recipe->requiredBuilding)) {
                    continue;
                }

                // Per-building recipe preference override: if the player
                // has locked this building to a specific recipe, skip any
                // candidate recipe whose id does not match.  Default (no
                // entry) falls through to the profit-ranked auto loop.
                {
                    const aoc::hex::AxialCoord loc = city->location();
                    const uint32_t locHash =
                        (static_cast<uint32_t>(static_cast<uint16_t>(loc.q)) << 16)
                      | static_cast<uint32_t>(static_cast<uint16_t>(loc.r));
                    const uint16_t pref = this->recipePreference(
                        city->owner(), locHash, recipe->requiredBuilding.value);
                    if (pref != 0xFFFFu && pref != recipe->recipeId) {
                        continue;
                    }
                }

                const CityBuildingLevelsComponent& levels = city->buildingLevels();
                int32_t buildingCap   = levels.capacity(recipe->requiredBuilding);
                int32_t buildingLevel = levels.getLevel(recipe->requiredBuilding);
                int32_t buildingUsed  = state.buildingBatchesUsed[recipe->requiredBuilding.value];
                if (buildingUsed >= buildingCap) {
                    continue;
                }

                CityStockpileComponent& stockpile = city->stockpile();

                bool hasAllInputs = true;
                for (const RecipeInput& input : recipe->inputs) {
                    if (stockpile.getAmount(input.goodId) < input.amount) {
                        hasAllInputs = false;
                        break;
                    }
                }
                if (!hasAllInputs) {
                    continue;
                }

                float inputQualitySum = 0.0f;
                int32_t inputCount    = 0;
                CityQualityComponent& quality = city->quality();

                for (const RecipeInput& input : recipe->inputs) {
                    if (input.consumed) {
                        [[maybe_unused]] bool ok = stockpile.consumeGoods(input.goodId, input.amount);
                        float q = quality.consumeGoods(input.goodId, input.amount);
                        inputQualitySum += q;
                        ++inputCount;
                    }
                }

                float avgInputQuality = (inputCount > 0)
                    ? inputQualitySum / static_cast<float>(inputCount)
                    : 0.0f;

                // Compute infrastructure bonus inline (mirrors computeInfrastructureBonus).
                constexpr float BONUS_PER_INFRA = 0.05f;
                constexpr float MAX_INFRA_BONUS  = 1.5f;
                float infraBonus = 1.0f;
                for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
                    if (grid.isValid(tile) && grid.hasRoad(grid.toIndex(tile))) {
                        infraBonus += BONUS_PER_INFRA;
                    }
                }
                if (districts.hasDistrict(DistrictType::Harbor))  { infraBonus += BONUS_PER_INFRA; }
                if (districts.hasBuilding(BuildingId{23}))         { infraBonus += BONUS_PER_INFRA; }
                if (districts.hasBuilding(BuildingId{6}))          { infraBonus += BONUS_PER_INFRA; }
                if (districts.hasBuilding(BuildingId{20}))         { infraBonus += BONUS_PER_INFRA; }
                infraBonus = std::min(infraBonus, MAX_INFRA_BONUS);

                const float envModifier = computeEnvironmentModifier(
                    grid, city->location(), recipe->requiredBuilding);

                float powerEff = 1.0f;
                std::unordered_map<aoc::game::City*, float>::iterator powerIt =
                    cityPowerEfficiency.find(city);
                if (powerIt != cityPowerEfficiency.end()) {
                    powerEff = powerIt->second;
                }

                CityProductionExperienceComponent& experience = city->productionExperience();
                float expMultiplier = experience.efficiencyMultiplier(recipe->recipeId);
                experience.addExperience(recipe->recipeId);

                const float revMultiplier = playerPtr->industrial().cumulativeProductionMultiplier();

                // C37: supply-chain health throttles output. Critical goods
                // cut off -> productionMultiplier drops from 1.0 toward 0.5.
                // Aligns recipes with broader import-dependency model.
                const float supplyMultiplier = playerPtr->supplyChain().productionMultiplier();

                // C40: Dutch-disease penalty on processed/advanced goods.
                // Raw extraction (RawStrategic/RawLuxury) unaffected so the
                // curse squeezes manufacturing, not extraction.
                float curseMultiplier = 1.0f;
                {
                    const GoodDef& outDef = goodDef(recipe->outputGoodId);
                    if (outDef.category != GoodCategory::RawStrategic
                        && outDef.category != GoodCategory::RawLuxury) {
                        curseMultiplier = playerPtr->resourceCurse().manufacturingPenalty;
                    }
                }

                // Tool efficiency: industrial buildings need Tools (good 63) to
                // operate at full capacity. Without tools, output is reduced to 60%.
                // This creates demand for the tools supply chain and makes the
                // Forge→Tools production path economically important.
                float toolEff = 1.0f;
                if (recipe->requiredBuilding.value <= 14  // Industrial buildings (0-14)
                    && recipe->requiredBuilding.value != 6  // Not Market
                    && recipe->requiredBuilding.value != 7) { // Not Library
                    constexpr uint16_t TOOLS_GOOD_ID = 63;
                    if (stockpile.getAmount(TOOLS_GOOD_ID) > 0) {
                        // Consume 1 tool per 3 recipe batches (tools wear out)
                        if (state.totalRecipesExecuted % 3 == 0) {
                            [[maybe_unused]] bool toolConsumed =
                                stockpile.consumeGoods(TOOLS_GOOD_ID, 1);
                        }
                    } else {
                        toolEff = 0.60f;  // No tools = 60% efficiency
                    }
                }

                // Chain output multiplier from BalanceParams: applied only to
                // gateway-chain recipes (OIL→FUEL/PLASTICS, ELECTRONICS,
                // CONSUMER_GOODS, ADV_CONSUMER_GOODS) so the GA can widen or
                // narrow the economic value of the manufacturing tree.
                float chainMult = 1.0f;
                switch (recipe->recipeId) {
                    case 4: case 5: case 10: case 11: case 12: case 13:
                    case 29:
                        chainMult = aoc::balance::params().chainOutputMult;
                        break;
                    default: break;
                }

                // DataCenter synergy: Software recipes (24 Platform, 60
                // Bootstrap) get +50% output per worked DataCenter tile,
                // capped at +200%.  Answers the "resource-poor civ should
                // still be able to export software" design question — a
                // Research Lab city with 3 Data Centers produces 3x the
                // software of a resource-rich city without them.
                float datacenterMult = 1.0f;
                if (recipe->recipeId == 24 || recipe->recipeId == 60) {
                    int32_t dcCount = 0;
                    for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
                        if (!grid.isValid(tile)) { continue; }
                        if (grid.improvement(grid.toIndex(tile))
                            == aoc::map::ImprovementType::DataCenter) {
                            ++dcCount;
                        }
                    }
                    datacenterMult = 1.0f + std::min(2.0f, 0.5f * static_cast<float>(dcCount));
                }
                const int32_t boostedOutput = std::max(1, static_cast<int32_t>(
                    static_cast<float>(recipe->outputAmount)
                    * infraBonus * envModifier * powerEff * expMultiplier
                    * revMultiplier * toolEff * supplyMultiplier * curseMultiplier
                    * chainMult * datacenterMult));
                stockpile.addGoods(recipe->outputGoodId, boostedOutput);

                // Recipe-fire audit: per-game counter + milestone log
                // (first-fire per recipe per game). WP-C8: counter lives on
                // `this` so ml/headless runs don't leak counts across games.
                {
                    const uint16_t rid = recipe->recipeId;
                    if (rid < this->m_recipeFireCount.size()) {
                        if (this->m_recipeFireCount[rid] == 0) {
                            LOG_INFO("recipe_first_fire: id=%u output=%u city=%u turn=%d",
                                     static_cast<unsigned>(rid),
                                     static_cast<unsigned>(recipe->outputGoodId),
                                     static_cast<unsigned>(city->owner()),
                                     static_cast<int>(gameState.currentTurn()));
                        }
                        ++this->m_recipeFireCount[rid];
                    }
                }

                bool hasPrecisionInstr = stockpile.getAmount(goods::PRECISION_INSTRUMENTS) > 0;
                uint32_t qualityHash   = this->m_depletionTurnCounter * 2654435761u
                    + static_cast<uint32_t>(reinterpret_cast<uintptr_t>(city)) * 2246822519u
                    + recipe->recipeId * 104729u;
                QualityTier outputQuality = determineOutputQuality(
                    buildingLevel,
                    experience.getExperience(recipe->recipeId),
                    hasPrecisionInstr,
                    avgInputQuality,
                    qualityHash);
                quality.addGoods(recipe->outputGoodId, boostedOutput, outputQuality);

                if (recipe->outputGoodId == goods::COPPER_COINS
                    || recipe->outputGoodId == goods::SILVER_COINS
                    || recipe->outputGoodId == goods::GOLD_BARS) {
                    int32_t existing = stockpile.getAmount(recipe->outputGoodId);
                    if (existing <= boostedOutput) {
                        LOG_INFO("First coins minted: %d x good %u in '%s' (player %u)",
                                 boostedOutput,
                                 static_cast<unsigned>(recipe->outputGoodId),
                                 city->name().c_str(),
                                 static_cast<unsigned>(city->owner()));
                    }
                }

                // Accumulate waste inline (mirrors accumulateWaste).
                WasteOutput waste = buildingWasteOutput(recipe->requiredBuilding);
                if (waste.amount > 0
                    && waste.type != static_cast<WasteType>(
                            static_cast<uint8_t>(WasteType::Count))) {
                    CityPollutionComponent& pollution = city->pollution();
                    pollution.wasteAccumulated += waste.amount;
                    if (waste.type == WasteType::Emissions) {
                        pollution.co2ContributionPerTurn += waste.amount;
                    }
                }

                state.totalRecipesExecuted += recipe->workerSlots;
                ++state.buildingBatchesUsed[recipe->requiredBuilding.value];
            }
        }
    }

    // --- Pass 3: waste treatment ---
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            CityPollutionComponent& pollution = cityPtr->pollution();
            if (pollution.wasteAccumulated <= 0) { continue; }
            if (!cityPtr->districts().hasBuilding(WASTE_TREATMENT_PLANT)) { continue; }
            constexpr int32_t TREATMENT_RATE = 5;
            int32_t treated = std::min(pollution.wasteAccumulated, TREATMENT_RATE);
            pollution.wasteAccumulated -= treated;
            if (treated > 0) {
                cityPtr->stockpile().addGoods(goods::CONSTRUCTION_MAT, treated / 2);
            }
        }
    }
}

// ============================================================================
// Internal trade: redistribute surplus goods between a player's own cities
// ============================================================================

void EconomySimulation::processInternalTradeForAllPlayers(aoc::game::GameState& gameState,
                                                          const aoc::map::HexGrid& grid) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        if (playerPtr->cities().empty()) { continue; }
        processInternalTrade(gameState, grid, playerPtr->id());
    }
}

// ============================================================================
// Resource depletion / renewable regeneration (counter tick only)
// ============================================================================

void EconomySimulation::applyResourceDepletion(aoc::game::GameState& /*gameState*/,
                                                aoc::map::HexGrid& /*grid*/) {
    ++this->m_depletionTurnCounter;
    // Depletion is handled via reserve consumption in harvestResources().
    // Future: add renewable regeneration for unworked tiles here.
}

// ============================================================================
// Step 3: Report supply/demand to the market
// ============================================================================

void EconomySimulation::reportToMarket(aoc::game::GameState& gameState) {
    // C34: stockpile cap + spoilage. Per-good cap scales with Granary building
    // (food preservation). Overflow is sold at 20% of base price (fire-sale)
    // so a glut actually reaches the market instead of sitting forever.
    constexpr int32_t kBaseCap = 2000;
    constexpr int32_t kGranaryBonus = 2000;
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            const int32_t cap = kBaseCap
                + (cityPtr->hasBuilding(BuildingId{15}) ? kGranaryBonus : 0);
            CityStockpileComponent& stock = cityPtr->stockpile();
            for (std::pair<const uint16_t, int32_t>& entry : stock.goods) {
                if (entry.second > cap) {
                    const int32_t excess = entry.second - cap;
                    const GoodDef& def = goodDef(entry.first);
                    const CurrencyAmount fireSaleGold = static_cast<CurrencyAmount>(
                        static_cast<float>(excess) * static_cast<float>(def.basePrice) * 0.2f);
                    if (fireSaleGold > 0) {
                        playerPtr->addGold(fireSaleGold);
                    }
                    this->m_market.reportSupply(entry.first, excess);
                    entry.second = cap;
                }
            }
        }
    }

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }

            const CityStockpileComponent& stockpile = cityPtr->stockpile();
            for (const std::pair<const uint16_t, int32_t>& entry : stockpile.goods) {
                if (entry.second > 0) {
                    this->m_market.reportSupply(entry.first, entry.second);
                } else if (entry.second < 0) {
                    this->m_market.reportDemand(entry.first, -entry.second);
                }
            }

            // Food and consumer goods demand based on population.
            this->m_market.reportDemand(goods::WHEAT, cityPtr->population());
            this->m_market.reportDemand(goods::CONSUMER_GOODS, cityPtr->population() / 3 + 1);
            if (cityPtr->population() > 5) {
                this->m_market.reportDemand(goods::PROCESSED_FOOD,
                    (cityPtr->population() - 5) / 2 + 1);
            }
            if (cityPtr->population() > 10) {
                this->m_market.reportDemand(goods::ADV_CONSUMER_GOODS,
                    (cityPtr->population() - 10) / 3 + 1);
            }

            // Actual consumption drain: convert population demand into real
            // stockpile depletion so there is pull on the production chain.
            // Without this, reporting demand to the market did nothing —
            // consumer goods piled up uncapped and downstream recipes had
            // no reason to fire.  Each citizen consumes a small fraction
            // per turn; missing goods just don't drain (no negative).
            CityStockpileComponent& stockpileMut = cityPtr->stockpile();
            const int32_t pop = cityPtr->population();
            const float demandScale = aoc::balance::params().consumerDemandScale;
            const int32_t consumerDrain = static_cast<int32_t>(
                static_cast<float>(pop / 3 + 1) * demandScale);
            const int32_t avail = stockpileMut.getAmount(goods::CONSUMER_GOODS);
            const int32_t consumerTake = std::min(consumerDrain, avail);
            if (consumerTake > 0) {
                [[maybe_unused]] const bool ok =
                    stockpileMut.consumeGoods(goods::CONSUMER_GOODS, consumerTake);
            }
            if (pop > 10) {
                const int32_t advDrain = (pop - 10) / 3 + 1;
                const int32_t advAvail = stockpileMut.getAmount(goods::ADV_CONSUMER_GOODS);
                const int32_t advTake = std::min(advDrain, advAvail);
                if (advTake > 0) {
                    [[maybe_unused]] const bool ok =
                        stockpileMut.consumeGoods(goods::ADV_CONSUMER_GOODS, advTake);
                }
            }
        }
    }
}

// ============================================================================
// Step 4: Execute active trade routes
// ============================================================================

void EconomySimulation::executeTradeRoutes(aoc::game::GameState& gameState) {
    for (TradeRouteComponent& route : gameState.tradeRoutes()) {
        if (route.turnsRemaining > 0) {
            --route.turnsRemaining;
            continue;
        }

        // Resolve destination stockpile via destPlayer + first city.
        // TradeRouteComponent carries destPlayer for exactly this purpose.
        aoc::game::Player* destPlayer = gameState.player(route.destPlayer);
        if (destPlayer == nullptr || destPlayer->cities().empty()) {
            continue;
        }
        // Deliver to the first (capital) city of the destination player.
        aoc::game::City* destCity = destPlayer->cities().front().get();
        if (destCity == nullptr) {
            continue;
        }
        CityStockpileComponent& destStockpile = destCity->stockpile();

        // Export price multiplier from source player's currency devaluation.
        float exportMult = 1.0f;
        aoc::game::Player* srcPlayer = gameState.player(route.sourcePlayer);
        if (srcPlayer != nullptr) {
            exportMult = srcPlayer->currencyDevaluation().exportPriceMultiplier();
        }

        for (const TradeOffer& offer : route.cargo) {
            int32_t adjusted = static_cast<int32_t>(
                static_cast<float>(offer.amountPerTurn) / std::max(0.5f, exportMult));
            destStockpile.addGoods(offer.goodId, adjusted);
        }

        int32_t baseTurns = static_cast<int32_t>(route.path.size()) / 5 + 1;

        // Tech check: Computers (TechId 16) reduces travel time by 1 turn.
        if (srcPlayer != nullptr && srcPlayer->hasResearched(TechId{16})) {
            baseTurns = std::max(1, baseTurns - 1);
        }

        route.turnsRemaining = baseTurns;
    }
}

// ============================================================================
// Step 5: Monetary policy
// ============================================================================

void EconomySimulation::executeMonetaryPolicy(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        MonetaryStateComponent& state = playerPtr->monetary();

        CurrencyAmount currentGDP = 0;
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            for (const std::pair<const uint16_t, int32_t>& entry : cityPtr->stockpile().goods) {
                if (entry.second > 0) {
                    currentGDP += static_cast<CurrencyAmount>(entry.second)
                                * static_cast<CurrencyAmount>(this->m_market.price(entry.first));
                }
            }
        }

        float bankingMult = bankingGDPMultiplier(state);
        currentGDP = static_cast<CurrencyAmount>(
            static_cast<float>(currentGDP) * bankingMult);

        CurrencyAmount prevGDP   = this->m_previousGDP[playerPtr->id()];
        CurrencyAmount prevMoney = this->m_previousMoneySupply[playerPtr->id()];

        executeFiscalPolicy(state, currentGDP);
        computeInflation(state, prevGDP, currentGDP, prevMoney);
        applyInflationEffects(state);

        // Monetary system advancement. Civs upgrade currency systems
        // when prerequisite techs are researched. Without this all
        // civs stayed in Barter through end-game (audit 2026-05-02).
        // Tech tree: Currency=5, Banking=9, Industrialization=11,
        // Computers=16.
        if (state.system == MonetarySystemType::Barter
            && playerPtr->hasResearched(aoc::TechId{5})) {
            state.system = MonetarySystemType::CommodityMoney;
        }
        if (state.system == MonetarySystemType::CommodityMoney
            && playerPtr->hasResearched(aoc::TechId{9})) {
            state.system = MonetarySystemType::GoldStandard;
        }
        if (state.system == MonetarySystemType::GoldStandard
            && playerPtr->hasResearched(aoc::TechId{11})) {
            state.system = MonetarySystemType::FiatMoney;
        }
        if (state.system == MonetarySystemType::FiatMoney
            && playerPtr->hasResearched(aoc::TechId{16})) {
            state.system = MonetarySystemType::Digital;
        }

        this->m_previousGDP[playerPtr->id()]         = currentGDP;
        this->m_previousMoneySupply[playerPtr->id()] = state.moneySupply;
    }

    // Seigniorage: reserve currency holders earn income from foreign GDP
    CurrencyAmount totalGDP = 0;
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr != nullptr) {
            totalGDP += playerPtr->monetary().gdp;
        }
    }

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        const CurrencyTrustComponent& trust = playerPtr->currencyTrust();
        if (!trust.isReserveCurrency) { continue; }

        MonetaryStateComponent& reserveState = playerPtr->monetary();
        CurrencyAmount foreignGDP   = totalGDP - reserveState.gdp;
        CurrencyAmount seigniorage  = computeSeigniorage(reserveState, true, foreignGDP);
        if (seigniorage > 0) {
            reserveState.treasury += seigniorage;
        }
    }
}

// ============================================================================
// Step 4b: Settle trade route imbalances in coins
// ============================================================================

void EconomySimulation::settleTradeInCoins(aoc::game::GameState& gameState) {
    const std::vector<TradeRouteComponent>& tradeRoutes = gameState.tradeRoutes();

    struct PlayerPairHash {
        std::size_t operator()(const std::pair<PlayerId, PlayerId>& p) const {
            return std::hash<uint32_t>()(
                (static_cast<uint32_t>(p.first) << 16) | static_cast<uint32_t>(p.second));
        }
    };
    std::unordered_map<std::pair<PlayerId, PlayerId>, int32_t, PlayerPairHash> tradeFlows;

    for (const TradeRouteComponent& route : tradeRoutes) {
        if (route.turnsRemaining > 0) { continue; }

        int32_t cargoValue = 0;
        for (const TradeOffer& offer : route.cargo) {
            cargoValue += offer.amountPerTurn * this->m_market.price(offer.goodId);
        }

        if (cargoValue > 0) {
            tradeFlows[std::make_pair(route.sourcePlayer, route.destPlayer)] += cargoValue;
        }
    }

    std::unordered_set<uint64_t> processed;
    for (const std::pair<const std::pair<PlayerId, PlayerId>, int32_t>& entry : tradeFlows) {
        PlayerId pA = entry.first.first;
        PlayerId pB = entry.first.second;

        uint64_t pairKey = (static_cast<uint64_t>(std::min(pA, pB)) << 32)
                         | static_cast<uint64_t>(std::max(pA, pB));
        if (processed.count(pairKey) > 0) { continue; }
        processed.insert(pairKey);

        int32_t flowAtoB = entry.second;
        std::pair<PlayerId, PlayerId> reverseKey = std::make_pair(pB, pA);
        std::unordered_map<std::pair<PlayerId, PlayerId>, int32_t, PlayerPairHash>::iterator reverseIt =
            tradeFlows.find(reverseKey);
        int32_t flowBtoA   = (reverseIt != tradeFlows.end()) ? reverseIt->second : 0;
        int32_t netBalance = flowAtoB - flowBtoA;

        if (netBalance == 0) { continue; }

        PlayerId payer    = (netBalance > 0) ? pB : pA;
        PlayerId receiver = (netBalance > 0) ? pA : pB;
        int32_t paymentValue = std::abs(netBalance);

        aoc::game::Player* payerPlayer    = gameState.player(payer);
        aoc::game::Player* receiverPlayer = gameState.player(receiver);
        if (payerPlayer == nullptr || receiverPlayer == nullptr) { continue; }

        float efficiency = bilateralTradeEfficiency(gameState, payer, receiver);
        int32_t effectivePayment = static_cast<int32_t>(
            static_cast<float>(paymentValue) * efficiency * 0.05f);
        effectivePayment = std::max(1, effectivePayment);

        // Transfer coins via city stockpiles (not just reserve fields) so that
        // updateCoinReservesFromStockpiles() picks them up correctly.
        // Coins are withdrawn from the payer's first city (capital) and deposited
        // into the receiver's first city. This is how ore-less civs acquire coins:
        // through trade surpluses (price-specie flow).
        if (payerPlayer->cities().empty() || receiverPlayer->cities().empty()) { continue; }
        CityStockpileComponent& payerStock = payerPlayer->cities().front()->stockpile();
        CityStockpileComponent& recvStock  = receiverPlayer->cities().front()->stockpile();

        int32_t remaining = effectivePayment;

        // Transfer highest-value coins first (gold bars > silver > copper)
        if (remaining > 0) {
            int32_t payerGold = payerStock.getAmount(goods::GOLD_BARS);
            if (payerGold > 0) {
                int32_t goldToTransfer = std::min(payerGold,
                    (remaining + GOLD_BAR_VALUE - 1) / GOLD_BAR_VALUE);
                [[maybe_unused]] bool ok1 = payerStock.consumeGoods(goods::GOLD_BARS, goldToTransfer);
                recvStock.addGoods(goods::GOLD_BARS, goldToTransfer);
                remaining -= goldToTransfer * GOLD_BAR_VALUE;
            }
        }
        if (remaining > 0) {
            int32_t payerSilver = payerStock.getAmount(goods::SILVER_COINS);
            if (payerSilver > 0) {
                int32_t silverToTransfer = std::min(payerSilver,
                    (remaining + SILVER_COIN_VALUE - 1) / SILVER_COIN_VALUE);
                [[maybe_unused]] bool ok2 = payerStock.consumeGoods(goods::SILVER_COINS, silverToTransfer);
                recvStock.addGoods(goods::SILVER_COINS, silverToTransfer);
                remaining -= silverToTransfer * SILVER_COIN_VALUE;
            }
        }
        if (remaining > 0) {
            int32_t payerCopper = payerStock.getAmount(goods::COPPER_COINS);
            if (payerCopper > 0) {
                int32_t copperToTransfer = std::min(payerCopper, remaining);
                [[maybe_unused]] bool ok3 = payerStock.consumeGoods(goods::COPPER_COINS, copperToTransfer);
                recvStock.addGoods(goods::COPPER_COINS, copperToTransfer);
            }
        }
    }
}

// ============================================================================
// Sync coin reserves from city stockpiles into the monetary state
// ============================================================================

void EconomySimulation::updateCoinReservesFromStockpiles(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        MonetaryStateComponent& state = playerPtr->monetary();
        state.copperCoinReserves = 0;
        state.silverCoinReserves = 0;
        state.goldBarReserves   = 0;

        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            const CityStockpileComponent& stockpile = cityPtr->stockpile();
            state.copperCoinReserves += stockpile.getAmount(goods::COPPER_COINS);
            state.silverCoinReserves += stockpile.getAmount(goods::SILVER_COINS);
            state.goldBarReserves   += stockpile.getAmount(goods::GOLD_BARS);
        }

        CoinTier previousTier = state.effectiveCoinTier;
        state.updateCoinTier();

        if (state.system == MonetarySystemType::Barter
            && previousTier == CoinTier::None
            && state.effectiveCoinTier != CoinTier::None) {
            LOG_INFO("Player %u reached coin tier %.*s: Cu=%d Ag=%d Au=%d",
                     static_cast<unsigned>(playerPtr->id()),
                     static_cast<int>(coinTierName(state.effectiveCoinTier).size()),
                     coinTierName(state.effectiveCoinTier).data(),
                     state.copperCoinReserves, state.silverCoinReserves, state.goldBarReserves);
        }

        if (state.effectiveCoinTier != previousTier) {
            LOG_INFO("Player %u coin tier changed: %.*s -> %.*s",
                     static_cast<unsigned>(playerPtr->id()),
                     static_cast<int>(coinTierName(previousTier).size()),
                     coinTierName(previousTier).data(),
                     static_cast<int>(coinTierName(state.effectiveCoinTier).size()),
                     coinTierName(state.effectiveCoinTier).data());
        }

        if (state.system == MonetarySystemType::CommodityMoney) {
            state.moneySupply = static_cast<CurrencyAmount>(state.totalCoinValue());
        }

        // === MONEY SUPPLY UPDATE ===
        // The coin stockpile determines the money supply, which governs:
        //   - Trade efficiency
        //   - Tax revenue base (taxation of circulating coins)
        //   - Inflation (more coins vs more goods)
        //
        // Treasury is NOT directly set to coinValue here. Treasury is the
        // government's spending account: it accumulates from income (taxation of
        // the money supply) and is drained by expenses (unit/building maintenance).
        // Starting at 0 with no money, it grows as coins are minted and taxed.
        //
        // This fixes the critical bug where treasury was overwritten each turn,
        // undoing all income and expense calculations from the previous turn.
        //
        // In BARTER mode (no coins): treasury is forced to 0 (no spending power).
        // In COMMODITY/GOLD/FIAT: moneySupply tracks coin pool, treasury accumulates.
        if (state.system == MonetarySystemType::Barter) {
            if (state.totalCoinCount() == 0) {
                playerPtr->setTreasury(0);
                state.treasury = 0;
            }
            // Once coins exist (transition just happened), let treasury accumulate.
        } else {
            // Update money supply for display, trade efficiency, and inflation.
            // CommodityMoney: moneySupply = physical coins
            // GoldStandard: moneySupply = coins + paper notes
            // Fiat: moneySupply also includes printed notes (managed separately)
            if (state.system == MonetarySystemType::CommodityMoney) {
                state.moneySupply = static_cast<CurrencyAmount>(state.totalCoinValue());
            } else if (state.system == MonetarySystemType::GoldStandard) {
                const int32_t coinWealth = state.totalCoinValue();
                state.moneySupply = static_cast<CurrencyAmount>(
                    static_cast<float>(coinWealth) * (1.0f + state.goldBackingRatio));
            }
            // Fiat moneySupply is managed by printMoney() and tracked separately.
        }
    }
}

// ============================================================================
// Per-turn monetary mechanic ticks
// ============================================================================

void EconomySimulation::tickMonetaryMechanics(aoc::game::GameState& gameState) {
    int32_t playerCount = static_cast<int32_t>(gameState.players().size());

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        MonetaryStateComponent& state = playerPtr->monetary();
        ++state.turnsInCurrentSystem;

        if (state.system == MonetarySystemType::CommodityMoney) {
            if (tickDebasementDiscovery(state)) {
                LOG_INFO("Player %u: coin debasement discovered by trade partners!",
                         static_cast<unsigned>(playerPtr->id()));
            }
        }

        if (state.system == MonetarySystemType::FiatMoney
            || state.system == MonetarySystemType::Digital) {
            CurrencyTrustComponent& trust = playerPtr->currencyTrust();
            if (trust.trustScore == 0.0f) {
                // First fiat/digital turn: initialise trust
                trust.owner      = playerPtr->id();
                trust.trustScore = 0.30f;
            }
            computeCurrencyTrust(gameState, state, trust, playerCount);
            // Floor Fiat money supply at the physical coin base. Tax > spending
            // drift otherwise drains moneySupply toward zero across long games,
            // which breaks currencyStrength() and the Digital transition gate.
            // Physical coins still circulate under fiat -- they're just no longer
            // redeemable for gold.
            const CurrencyAmount coinFloor =
                static_cast<CurrencyAmount>(state.totalCoinValue());
            if (state.moneySupply < coinFloor) {
                state.moneySupply = coinFloor;
            }
        }
    }

    updateReserveCurrencyStatus(gameState);
    updateExchangeRates(gameState);
}

// ============================================================================
// Currency crises, bond payments, and currency war processing
// ============================================================================

void EconomySimulation::processCrisisAndBonds(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        MonetaryStateComponent& state = playerPtr->monetary();
        CurrencyCrisisComponent& crisis = playerPtr->currencyCrisis();
        if (crisis.owner == INVALID_PLAYER) {
            crisis.owner = playerPtr->id();
        }
        // Reserve-ratio stress must run BEFORE processCurrencyCrisis so the
        // forced GoldStandard -> Fiat suspension lands before hyperinflation
        // checks see the new fiat state.
        processReserveStress(state);
        processCurrencyCrisis(gameState, state, crisis);
    }

    processBondPayments(gameState);
    processIOUPayments(gameState);
    processCurrencyWar(gameState, this->m_currencyWarState);
}

// ============================================================================
// Economic zones, speculation, and sanctions
// ============================================================================

void EconomySimulation::processEconomicZonesAndSpeculation(aoc::game::GameState& gameState,
                                                            aoc::map::HexGrid& grid) {
    processEconomicZones(gameState, grid, this->m_market, this->m_economicZones);
    processSanctions(gameState, this->m_sanctions);
    processSpeculation(gameState, this->m_market);

    // Check industrial revolution progress for all players
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        checkIndustrialRevolution(gameState, playerPtr->id(),
                                  static_cast<TurnNumber>(this->m_depletionTurnCounter));
    }
}

} // namespace aoc::sim
