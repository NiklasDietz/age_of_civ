/**
 * @file Maintenance.cpp
 * @brief Unit and building maintenance cost processing implementation.
 *
 * All maintenance logic uses the GameState object model (Player/City/Unit).
 * ECS versions have been removed as part of the Phase 3 migration.
 */

#include "aoc/simulation/economy/Maintenance.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

EconomicBreakdown computeEconomicBreakdown(const aoc::game::Player& player,
                                            const aoc::map::HexGrid& grid) {
    EconomicBreakdown bd{};

    // In barter mode with no coins, no monetary income exists.
    if (player.monetary().system == MonetarySystemType::Barter
        && player.monetary().totalCoinCount() == 0) {
        // Still compute expenses and goods so the diagnostic is useful.
        for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
            const CityDistrictsComponent& districts = city->districts();
            for (const CityDistrictsComponent::PlacedDistrict& d : districts.districts) {
                if (d.type != DistrictType::CityCenter) { bd.expenseBuildings += 1; }
                for (BuildingId bid : d.buildings) {
                    bd.expenseBuildings += static_cast<CurrencyAmount>(buildingDef(bid).maintenanceCost);
                }
            }
            if (!city->isOriginalCapital()) { bd.expenseBuildings += 2; }
            for (const std::pair<const uint16_t, int32_t>& entry : city->stockpile().goods) {
                bd.goodsStockpiled += entry.second;
            }
        }
        for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
            const int32_t cost = unit->typeDef().maintenanceGold();
            if (cost > 0) { bd.expenseUnits += static_cast<CurrencyAmount>(cost); }
        }
        bd.totalExpense = bd.expenseUnits + bd.expenseBuildings;
        bd.netFlow      = -bd.totalExpense;
        return bd;
    }

    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        // Capital Palace
        if (city->isOriginalCapital()) { bd.incomeCapital += 5; }

        // Population tax: 1 per citizen
        bd.incomeTax += static_cast<CurrencyAmount>(city->population());

        // Industrial revolution per-citizen
        const float indGold = player.industrial().cumulativeGoldPerCitizen();
        if (indGold > 0.0f) {
            bd.incomeIndustrial += static_cast<CurrencyAmount>(
                static_cast<float>(city->population()) * indGold);
        }

        // Tile gold
        for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
            if (grid.isValid(tile)) {
                bd.incomeTileGold += static_cast<CurrencyAmount>(
                    grid.tileYield(grid.toIndex(tile)).gold);
            }
        }

        // Commercial district tax + building bonuses (monetary era only).
        // Adjacency yields the grid-only subset of computeAdjacencyBonus:
        //   Commercial: +2 gold if adjacent to a river edge.
        //   Harbor:     +2 gold per adjacent coastal resource tile.
        // Cross-player district adjacency is skipped here to avoid plumbing
        // GameState through the income-breakdown signature.
        const CityDistrictsComponent& districts = city->districts();
        for (const CityDistrictsComponent::PlacedDistrict& d : districts.districts) {
            if (d.type == DistrictType::Commercial) { bd.incomeCommercial += 3; }
            if (d.type == DistrictType::Harbor)     { bd.incomeCommercial += 2; }
            for (BuildingId bid : d.buildings) {
                bd.incomeCommercial += static_cast<CurrencyAmount>(buildingDef(bid).goldBonus);
            }
            if ((d.type == DistrictType::Commercial || d.type == DistrictType::Harbor)
                && grid.isValid(d.location)) {
                const int32_t tileIdx = grid.toIndex(d.location);
                if (d.type == DistrictType::Commercial
                    && grid.riverEdges(tileIdx) != 0) {
                    bd.incomeCommercial += 2;
                }
                if (d.type == DistrictType::Harbor) {
                    const std::array<aoc::hex::AxialCoord, 6> neighbors =
                        aoc::hex::neighbors(d.location);
                    int32_t adjCoastalResources = 0;
                    for (const aoc::hex::AxialCoord& nbr : neighbors) {
                        if (!grid.isValid(nbr)) { continue; }
                        const int32_t nbrIdx = grid.toIndex(nbr);
                        if (aoc::map::isWater(grid.terrain(nbrIdx))
                            && grid.resource(nbrIdx).isValid()) {
                            ++adjCoastalResources;
                        }
                    }
                    bd.incomeCommercial +=
                        static_cast<CurrencyAmount>(adjCoastalResources * 2);
                }
            }
        }

        // Goods economic activity (Phase B: increased caps/rates).
        // Must stay in sync with the identical block in processGoldIncome
        // (below) — previous hardcoded ids 72/79/75 resolved to
        // SURFACE_PLATE / CHARCOAL / SEMICONDUCTORS instead of
        // CONSUMER_GOODS / CLOTHING / ELECTRONICS, so the diagnostic
        // breakdown and the real income loop both taxed the wrong goods.
        {
            const CityStockpileComponent& stock = city->stockpile();
            int32_t ecoGold = 0;
            ecoGold += stock.getAmount(goods::CONSUMER_GOODS) / 4;
            ecoGold += stock.getAmount(goods::PROCESSED_FOOD) / 4;
            ecoGold += stock.getAmount(goods::CLOTHING)       / 2;
            ecoGold += stock.getAmount(goods::ELECTRONICS)    / 1;
            bd.incomeGoodsEcon += static_cast<CurrencyAmount>(std::min(ecoGold, 15));
        }

        // Building maintenance (no flat district fee; only building definitions)
        for (const CityDistrictsComponent::PlacedDistrict& d : districts.districts) {
            for (BuildingId bid : d.buildings) {
                bd.expenseBuildings += static_cast<CurrencyAmount>(buildingDef(bid).maintenanceCost);
            }
        }
        if (!city->isOriginalCapital()) { bd.expenseBuildings += 1; }  // sprawl

        // Goods stockpile count
        for (const std::pair<const uint16_t, int32_t>& entry : city->stockpile().goods) {
            bd.goodsStockpiled += entry.second;
        }
    }

    // Unit maintenance
    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        const int32_t cost = unit->typeDef().maintenanceGold();
        if (cost > 0) { bd.expenseUnits += static_cast<CurrencyAmount>(cost); }
    }

    // Phase B: Money-supply taxation (mirrors processGoldIncome logic)
    {
        const int32_t moneySupply = player.monetary().totalCoinValue();
        if (moneySupply > 0) {
            float collectionEfficiency = 0.50f;
            for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
                const CityDistrictsComponent& districts = city->districts();
                for (const CityDistrictsComponent::PlacedDistrict& d : districts.districts) {
                    if (d.type == DistrictType::Commercial) { collectionEfficiency += 0.05f; }
                    for (BuildingId bid : d.buildings) {
                        const uint16_t bv = bid.value;
                        if (bv == 6u)  { collectionEfficiency += 0.08f; }
                        if (bv == 20u) { collectionEfficiency += 0.12f; }
                        if (bv == 21u) { collectionEfficiency += 0.18f; }
                        if (bv == 13u) { collectionEfficiency += 0.10f; }
                    }
                }
            }
            collectionEfficiency = std::min(collectionEfficiency, 1.0f);
            constexpr float MONEY_VELOCITY = 0.35f;
            bd.incomeCommercial += static_cast<CurrencyAmount>(
                static_cast<float>(moneySupply) * MONEY_VELOCITY
                * player.monetary().taxRate * collectionEfficiency);
        }
    }

    bd.totalIncome = bd.incomeCapital + bd.incomeTax + bd.incomeIndustrial
                   + bd.incomeTileGold + bd.incomeCommercial + bd.incomeGoodsEcon;
    bd.effectiveIncome = static_cast<CurrencyAmount>(
        static_cast<float>(bd.totalIncome) * player.monetary().goldAllocation);
    bd.totalExpense = bd.expenseUnits + bd.expenseBuildings;
    bd.netFlow = bd.effectiveIncome - bd.totalExpense;

    return bd;
}

CurrencyAmount processGoldIncome(aoc::game::Player& player,
                                  const aoc::map::HexGrid& grid) {
    // In pure barter mode no money exists yet — income is zero.
    // Coins must first be minted before any treasury income can flow.
    if (player.monetary().system == MonetarySystemType::Barter
        && player.monetary().totalCoinCount() == 0) {
        player.setIncomePerTurn(0);
        return 0;
    }

    CurrencyAmount goldIncome = 0;

    // Find capital location for distance-based corruption
    aoc::hex::AxialCoord capitalLocation{0, 0};
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (city->isOriginalCapital()) {
            capitalLocation = city->location();
            break;
        }
    }

    // Get government corruption rates
    const aoc::sim::GovernmentDef& govDef = governmentDef(player.government().government);

    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        CurrencyAmount cityGold = 0;

        // Capital Palace bonus. Bumped from 5 to 10 so small empires
        // can still fund 1-2 cheap buildings/turn instead of starving
        // while waiting for pop to generate tax revenue.
        if (city->isOriginalCapital()) {
            cityGold += 10;
        }

        // Population-based tax income: 1 gold per citizen.
        // Citizens pay taxes — a pop-10 city should produce 10 gold from taxation
        // alone. This is the primary income source and scales naturally with empire
        // size: more cities → more population → more tax revenue → can afford
        // more buildings and units. No artificial discounts needed.
        cityGold += static_cast<CurrencyAmount>(city->population());

        // Industrial revolution per-citizen bonus: knowledge/services economy.
        // Post-industrial nations generate wealth from citizens, not territory.
        // A small 3-city nation with 20 pop at 3rd revolution (Digital Age)
        // earns 20 * 3.5 = 70 extra gold/turn -- competitive with a 10-city empire.
        const float indGoldPerCitizen = player.industrial().cumulativeGoldPerCitizen();
        if (indGoldPerCitizen > 0.0f) {
            cityGold += static_cast<CurrencyAmount>(
                static_cast<float>(city->population()) * indGoldPerCitizen);
        }

        // Gold from worked tiles
        for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
            if (grid.isValid(tile)) {
                aoc::map::TileYield yield = grid.tileYield(grid.toIndex(tile));
                cityGold += static_cast<CurrencyAmount>(yield.gold);
            }
        }

        // Specialist taxmen: +3 gold each (read from ECS sync'd data or default 0)
        // Taxmen gold is handled here since we compute per-city gold

        // Commercial buildings tax merchant activity (market stalls, banking fees,
        // stock trade commissions). In monetary eras this is REAL taxable commerce.
        // In barter it's 0 (our barter guard handles that at the function level).
        // Harbor port fees apply in all monetary phases.
        const CityDistrictsComponent& districts = city->districts();
        for (const CityDistrictsComponent::PlacedDistrict& d : districts.districts) {
            if (d.type == DistrictType::Commercial) {
                cityGold += 3;  // Commercial district hub: tax on local trade
            }
            if (d.type == DistrictType::Harbor) {
                cityGold += 2;  // Port fees
            }
            for (BuildingId bid : d.buildings) {
                cityGold += static_cast<CurrencyAmount>(buildingDef(bid).goldBonus);
            }
        }

        // Wonder gold bonus (H4.9): Big Ben, Colossus, Machu Picchu, etc.
        // WP-A7: era-decay so ancient gold wonders don't dominate late-game.
        // A7 unique effects:
        //   - Machu Picchu (7): only pays out when the host city is adjacent
        //     to a Mountain tile — per its description.
        //   - Big Ben (9): doubles per-market-building gold in the host city.
        for (const WonderId wid : city->wonders().wonders) {
            const WonderDef& wdef = wonderDef(wid);
            const float decay = wonderEraDecayFactor(wdef, player.era().currentEra);

            if (wid == 7) { // Machu Picchu
                bool mountainAdj = false;
                if (grid.isValid(city->location())) {
                    const std::array<aoc::hex::AxialCoord, 6> nbrs =
                        aoc::hex::neighbors(city->location());
                    for (const aoc::hex::AxialCoord& n : nbrs) {
                        if (!grid.isValid(n)) { continue; }
                        if (grid.terrain(grid.toIndex(n))
                            == aoc::map::TerrainType::Mountain) {
                            mountainAdj = true;
                            break;
                        }
                    }
                }
                if (mountainAdj) {
                    cityGold += static_cast<CurrencyAmount>(
                        wdef.effect.goldBonus * decay);
                }
                continue;
            }

            if (wid == 9) { // Big Ben — flat bonus + doubles market gold.
                cityGold += static_cast<CurrencyAmount>(wdef.effect.goldBonus * decay);
                int32_t marketGold = 0;
                for (const CityDistrictsComponent::PlacedDistrict& d
                        : city->districts().districts) {
                    for (BuildingId bid : d.buildings) {
                        if (bid.value == 6 || bid.value == 20 || bid.value == 21) {
                            // Market (6), Bank (20), Stock Exchange (21).
                            marketGold += buildingDef(bid).goldBonus;
                        }
                    }
                }
                cityGold += static_cast<CurrencyAmount>(
                    static_cast<float>(marketGold) * decay);
                continue;
            }

            cityGold += static_cast<CurrencyAmount>(wdef.effect.goldBonus * decay);
        }

        // Goods-based commerce tax: goods circulating in the city represent
        // real economic activity that the government taxes. This is the natural
        // income loop: produce goods → local market activity → tax revenue.
        // Goods-rich cities pay more taxes because they have a larger real economy.
        // Max 15 gold/city so supply-side improvements are meaningful but not dominant.
        {
            const CityStockpileComponent& stock = city->stockpile();
            int32_t economicActivityGold = 0;
            economicActivityGold += stock.getAmount(goods::CONSUMER_GOODS) / 4;   // was /5
            economicActivityGold += stock.getAmount(goods::PROCESSED_FOOD) / 4;   // was /5
            economicActivityGold += stock.getAmount(goods::CLOTHING)       / 2;   // was /3
            economicActivityGold += stock.getAmount(goods::ELECTRONICS)    / 1;   // was /2
            cityGold += static_cast<CurrencyAmount>(std::min(economicActivityGold, 15));
        }

        // Distance-based corruption: reduces gold based on distance from capital.
        // Varies by government type (Communism has 0 distance corruption).
        if (!city->isOriginalCapital() && govDef.distanceCorruptionRate > 0.0f) {
            int32_t dist = grid.distance(city->location(), capitalLocation);
            float maxDist = static_cast<float>(std::max(grid.width(), grid.height()));
            float distFraction = static_cast<float>(dist) / maxDist;
            float corruptionPct = govDef.corruptionRate + distFraction * govDef.distanceCorruptionRate * 0.1f;
            corruptionPct = std::min(corruptionPct, 0.50f);  // Cap at 50%
            cityGold = static_cast<CurrencyAmount>(
                static_cast<float>(cityGold) * (1.0f - corruptionPct));
        }

        goldIncome += cityGold;
    }

    // Phase B: Money-supply taxation via velocity-adjusted GDP.
    // Income = moneySupply × velocity × taxRate × collectionEfficiency.
    //
    // Velocity (0.2 = 20% of coins change hands per turn, i.e. each coin
    // is used in a transaction about once every 5 turns) converts the coin
    // STOCK into a GDP FLOW suitable for taxation.  Without velocity the 15%
    // tax rate would be applied to the full stockpile each turn — yielding far
    // too much revenue and making coin accumulation the only goal.
    //
    // This makes the mining → minting → velocity loop meaningful:
    //   more coins → higher GDP flow → more tax revenue (but not proportionally
    //   more, because velocity limits how fast the economy can use them).
    //
    // Commercial buildings increase collection efficiency (base 0.50):
    //   Market +8%, Bank +12%, StockExchange +18%, Telecom Hub +10%, district hub +5%.
    {
        const int32_t moneySupply = player.monetary().totalCoinValue();
        if (moneySupply > 0) {
            constexpr float MONEY_VELOCITY = 0.35f;  // 35% of supply transacts per turn

            float collectionEfficiency = 0.50f;
            for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
                const CityDistrictsComponent& districts = city->districts();
                for (const CityDistrictsComponent::PlacedDistrict& d : districts.districts) {
                    if (d.type == DistrictType::Commercial) {
                        collectionEfficiency += 0.05f;
                    }
                    for (BuildingId bid : d.buildings) {
                        const uint16_t bv = bid.value;
                        if (bv == 6u)  { collectionEfficiency += 0.08f; }   // Market
                        if (bv == 20u) { collectionEfficiency += 0.12f; }   // Bank
                        if (bv == 21u) { collectionEfficiency += 0.18f; }   // Stock Exchange
                        if (bv == 13u) { collectionEfficiency += 0.10f; }   // Telecom Hub
                    }
                }
            }
            collectionEfficiency = std::min(collectionEfficiency, 1.0f);

            const float taxRate = player.monetary().taxRate;
            // Effective rate: taxRate × velocity × efficiency
            const CurrencyAmount taxRevenue = static_cast<CurrencyAmount>(
                static_cast<float>(moneySupply) * MONEY_VELOCITY * taxRate * collectionEfficiency);
            goldIncome += taxRevenue;
        }
    }

    // Apply gold allocation slider: only the gold fraction goes to treasury.
    // The rest is allocated to science and luxury bonuses (handled in their respective systems).
    CurrencyAmount effectiveGold = static_cast<CurrencyAmount>(
        static_cast<float>(goldIncome) * player.monetary().goldAllocation);
    player.addGold(effectiveGold);
    player.setIncomePerTurn(goldIncome);  // Display full income before split
    return goldIncome;
}

void processUnitMaintenance(aoc::game::Player& player) {
    // In barter mode with no coins, money doesn't exist yet.
    // Units are maintained by the city's food/production (not tracked monetarily).
    if (player.monetary().system == MonetarySystemType::Barter
        && player.monetary().totalCoinCount() == 0) {
        return;
    }

    // Hard floor: the treasury must never drop below -500.  Below this point
    // debt compounds faster than any realistic income can recover it.
    constexpr CurrencyAmount TREASURY_HARD_FLOOR    = -500;
    // Threshold at which we switch to military-only mode.
    constexpr CurrencyAmount TREASURY_DEFICIT_LIMIT = 0;
    // Minimum garrison we never disband below.
    constexpr int32_t        MIN_GARRISON           = 2;
    // Maximum tax rate applied automatically when bankrupt to boost income.
    constexpr float          MAX_TAX_RATE           = 0.40f;

    // When deeply bankrupt, force maximum tax rate to maximise income recovery.
    if (player.treasury() < TREASURY_HARD_FLOOR) {
        if (player.monetary().taxRate < MAX_TAX_RATE) {
            player.monetary().taxRate = MAX_TAX_RATE;
            LOG_WARN("Player %u [Maintenance.cpp:processUnitMaintenance] treasury %lld "
                     "below hard floor -- tax rate forced to %.2f",
                     static_cast<unsigned>(player.id()),
                     static_cast<long long>(player.treasury()),
                     static_cast<double>(MAX_TAX_RATE));
        }
    }

    // Count military units before any potential disband.
    int32_t militaryCount = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        if (isMilitary(unit->typeDef().unitClass)) {
            ++militaryCount;
        }
    }

    // Aggressive disband at the hard floor: remove the most expensive military
    // unit immediately so the treasury stops bleeding.
    if (player.treasury() < TREASURY_HARD_FLOOR && militaryCount > MIN_GARRISON) {
        aoc::game::Unit* disbandTarget = nullptr;
        int32_t worstCost = 0;
        for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
            if (unit->typeDef().unitClass == UnitClass::Settler) {
                continue;
            }
            const int32_t cost = unit->typeDef().maintenanceGold();
            if (isMilitary(unit->typeDef().unitClass) && cost > worstCost) {
                worstCost    = cost;
                disbandTarget = unit.get();
            }
        }
        // Fall back to any non-settler unit if no military candidate found.
        if (disbandTarget == nullptr) {
            for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
                if (unit->typeDef().unitClass == UnitClass::Settler) {
                    continue;
                }
                const int32_t cost = unit->typeDef().maintenanceGold();
                if (cost > worstCost) {
                    worstCost    = cost;
                    disbandTarget = unit.get();
                }
            }
        }
        if (disbandTarget != nullptr) {
            LOG_WARN("Player %u [Maintenance.cpp:processUnitMaintenance] hard-floor "
                     "bankruptcy (treasury %lld): disbanding %s (cost %d gold/turn)",
                     static_cast<unsigned>(player.id()),
                     static_cast<long long>(player.treasury()),
                     disbandTarget->typeDef().name.data(),
                     worstCost);
            player.removeUnit(disbandTarget);
            --militaryCount;
        }
    }

    // Per-unit maintenance: each military unit costs gold based on its era.
    // Civilian units (settlers, builders, traders, scouts) are free.
    CurrencyAmount totalMaintenance = 0;
    int32_t paidUnits = 0;

    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        const int32_t cost = unit->typeDef().maintenanceGold();
        if (cost > 0) {
            totalMaintenance += static_cast<CurrencyAmount>(cost);
            ++paidUnits;
        }
    }

    if (totalMaintenance <= 0) {
        return;
    }

    if (player.treasury() < TREASURY_DEFICIT_LIMIT) {
        // Already in deficit: skip ALL unit maintenance -- paying it would push
        // the treasury further negative and trigger the hard floor faster.
        // The disband logic above already sheds the most expensive unit each
        // turn, which is the correct pressure relief mechanism.
        LOG_INFO("Player %u unit maintenance skipped (treasury %lld < 0): "
                 "would have cost %lld gold",
                 static_cast<unsigned>(player.id()),
                 static_cast<long long>(player.treasury()),
                 static_cast<long long>(totalMaintenance));
    } else {
        // Treasury is non-negative: pay in full, but apply the hard floor to
        // avoid a single large maintenance bill punching through it.
        const CurrencyAmount afterDeduction = player.treasury() - totalMaintenance;
        if (afterDeduction < TREASURY_HARD_FLOOR) {
            // Partial payment: only deduct down to the floor.
            const CurrencyAmount allowed = player.treasury() - TREASURY_HARD_FLOOR;
            if (allowed > 0) {
                player.addGold(-allowed);
            }
            LOG_INFO("Player %u unit maintenance partially paid: %lld of %lld gold "
                     "(hard floor hit, treasury: %lld)",
                     static_cast<unsigned>(player.id()),
                     static_cast<long long>(allowed > 0 ? allowed : 0),
                     static_cast<long long>(totalMaintenance),
                     static_cast<long long>(player.treasury()));
        } else {
            player.addGold(-totalMaintenance);
            LOG_INFO("Player %u unit maintenance: %d units, cost %lld gold "
                     "(treasury: %lld)",
                     static_cast<unsigned>(player.id()), paidUnits,
                     static_cast<long long>(totalMaintenance),
                     static_cast<long long>(player.treasury()));
        }
    }

    // Update consecutive negative-treasury counter.
    if (player.treasury() < 0) {
        ++player.monetary().consecutiveNegativeTurns;
    } else {
        player.monetary().consecutiveNegativeTurns = 0;
    }

    // Sustained bankruptcy (>= 5 consecutive turns below -200): disband the
    // most expensive unit, still respecting the minimum garrison.
    constexpr CurrencyAmount SUSTAINED_THRESHOLD = -200;
    if (player.monetary().consecutiveNegativeTurns >= 5
        && player.treasury() < SUSTAINED_THRESHOLD
        && militaryCount > MIN_GARRISON)
    {
        aoc::game::Unit* disbandTarget = nullptr;
        int32_t worstCost = 0;
        for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
            if (unit->typeDef().unitClass == UnitClass::Settler) {
                continue;
            }
            const int32_t cost = unit->typeDef().maintenanceGold();
            if (cost > worstCost) {
                worstCost    = cost;
                disbandTarget = unit.get();
            }
        }
        if (disbandTarget != nullptr) {
            LOG_WARN("Player %u [Maintenance.cpp:processUnitMaintenance] sustained "
                     "bankruptcy (%d turns, treasury %lld): disbanded %s",
                     static_cast<unsigned>(player.id()),
                     player.monetary().consecutiveNegativeTurns,
                     static_cast<long long>(player.treasury()),
                     disbandTarget->typeDef().name.data());
            player.removeUnit(disbandTarget);
            // Reset counter so we don't disband every turn once over the threshold.
            player.monetary().consecutiveNegativeTurns = 0;
        }
    }
}

void processBuildingMaintenance(aoc::game::Player& player) {
    // In barter mode with no coins, money doesn't exist yet — no building upkeep.
    if (player.monetary().system == MonetarySystemType::Barter
        && player.monetary().totalCoinCount() == 0) {
        return;
    }

    // Skip all building and district maintenance when the treasury is already
    // deeply in debt.  City-center upkeep (the +2 per city sprawl cost) is
    // also deferred -- the unit maintenance hard floor is the primary recovery
    // mechanism at this point.
    constexpr CurrencyAmount SKIP_THRESHOLD = -200;
    if (player.treasury() < SKIP_THRESHOLD) {
        LOG_INFO("Player %u building/city maintenance skipped (treasury %lld < %lld)",
                 static_cast<unsigned>(player.id()),
                 static_cast<long long>(player.treasury()),
                 static_cast<long long>(SKIP_THRESHOLD));
        return;
    }

    CurrencyAmount totalMaintenance = 0;

    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        const CityDistrictsComponent& districts = city->districts();

        // Building maintenance from building definitions only.
        // Districts themselves no longer add flat maintenance — their upkeep
        // is embodied in the buildings inside them.  Removing the district flat
        // fee reduces the early-game maintenance burden so players can sustain
        // an empire while the coin economy is still bootstrapping.
        for (const CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
            for (BuildingId bid : district.buildings) {
                totalMaintenance += static_cast<CurrencyAmount>(buildingDef(bid).maintenanceCost);
            }
        }

        // Per-city maintenance: 1 gold per city beyond the first (empire sprawl).
        // Reduced from 2 to 1 — sprawl costs are still present but less punishing.
        if (!city->isOriginalCapital()) {
            totalMaintenance += 1;
        }
    }

    if (totalMaintenance <= 0) {
        return;
    }

    // Scale by inflation price level.
    const float priceMultiplier = priceLevelMaintenanceMultiplier(
        player.monetary().priceLevel);
    const CurrencyAmount adjustedMaintenance = static_cast<CurrencyAmount>(
        static_cast<float>(totalMaintenance) * priceMultiplier);

    // Apply the hard floor: never let a single maintenance tick punch the
    // treasury below -500.
    constexpr CurrencyAmount TREASURY_HARD_FLOOR = -500;
    const CurrencyAmount afterDeduction = player.treasury() - adjustedMaintenance;
    if (afterDeduction < TREASURY_HARD_FLOOR) {
        const CurrencyAmount allowed = player.treasury() - TREASURY_HARD_FLOOR;
        if (allowed > 0) {
            player.addGold(-allowed);
        }
        LOG_INFO("Player %u building/city maintenance partially paid: %lld of %lld gold "
                 "(hard floor hit, treasury: %lld)",
                 static_cast<unsigned>(player.id()),
                 static_cast<long long>(allowed > 0 ? allowed : 0),
                 static_cast<long long>(adjustedMaintenance),
                 static_cast<long long>(player.treasury()));
    } else {
        player.addGold(-adjustedMaintenance);
        LOG_INFO("Player %u building/city maintenance: %lld gold (treasury: %lld)",
                 static_cast<unsigned>(player.id()),
                 static_cast<long long>(adjustedMaintenance),
                 static_cast<long long>(player.treasury()));
    }
}

} // namespace aoc::sim
