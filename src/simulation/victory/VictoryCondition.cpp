/**
 * @file VictoryCondition.cpp
 * @brief CSI-based victory system: composite scoring, era evaluation,
 *        interdependence bonuses, losing conditions, and integration project.
 */

#include "aoc/simulation/victory/VictoryCondition.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace aoc::sim {

// ============================================================================
// Helpers: collect raw stats per player
// ============================================================================

struct PlayerRawStats {
    PlayerId owner = INVALID_PLAYER;
    // Economic
    CurrencyAmount gdp = 0;
    int32_t tradeVolume = 0;
    int32_t tradePartnerCount = 0;
    float inflationRate = 0.0f;
    // Military
    int32_t militaryUnits = 0;
    float totalCombatStrength = 0.0f;
    // Cultural
    float culturePerTurn = 0.0f;
    int32_t wonderCount = 0;
    // Scientific
    int32_t techsResearched = 0;
    // Diplomatic
    int32_t allianceCount = 0;
    int32_t agreementCount = 0;
    // Quality of life
    float avgHappiness = 0.0f;
    int32_t totalPopulation = 0;
    // Territorial
    int32_t cityCount = 0;
    int32_t improvedTiles = 0;
    int32_t strategicResources = 0;
    // Financial
    CurrencyAmount bondHoldings = 0;
    CurrencyAmount treasury = 0;
    bool isReserveCurrency = false;
    CurrencyAmount tradeSurplus = 0;
    // Loyalty
    float avgLoyalty = 100.0f;
};

static std::unordered_map<PlayerId, PlayerRawStats> gatherPlayerStats(
    const aoc::ecs::World& world,
    const aoc::map::HexGrid& grid,
    const EconomySimulation& economy) {

    std::unordered_map<PlayerId, PlayerRawStats> stats;

    // Cities: population, happiness, loyalty, improvements, wonders
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            const CityComponent& city = cityPool->data()[i];
            if (city.owner == INVALID_PLAYER || city.owner == BARBARIAN_PLAYER) {
                continue;
            }
            PlayerRawStats& s = stats[city.owner];
            s.owner = city.owner;
            ++s.cityCount;
            s.totalPopulation += city.population;

            // Happiness
            const CityHappinessComponent* happiness =
                world.tryGetComponent<CityHappinessComponent>(cityPool->entities()[i]);
            if (happiness != nullptr) {
                s.avgHappiness += happiness->amenities - happiness->demand;
            }

            // Loyalty
            const CityLoyaltyComponent* loyalty =
                world.tryGetComponent<CityLoyaltyComponent>(cityPool->entities()[i]);
            if (loyalty != nullptr) {
                s.avgLoyalty += loyalty->loyalty;
            }

            // Wonders
            const CityWondersComponent* wonders =
                world.tryGetComponent<CityWondersComponent>(cityPool->entities()[i]);
            if (wonders != nullptr) {
                s.wonderCount += static_cast<int32_t>(wonders->wonders.size());
            }
        }

        // Average happiness and loyalty
        for (std::pair<const PlayerId, PlayerRawStats>& entry : stats) {
            if (entry.second.cityCount > 0) {
                entry.second.avgHappiness /= static_cast<float>(entry.second.cityCount);
                entry.second.avgLoyalty /= static_cast<float>(entry.second.cityCount);
            }
        }
    }

    // Culture
    for (std::pair<const PlayerId, PlayerRawStats>& entry : stats) {
        entry.second.culturePerTurn = computePlayerCulture(world, grid, entry.first);
    }

    // Techs
    const aoc::ecs::ComponentPool<PlayerTechComponent>* techPool =
        world.getPool<PlayerTechComponent>();
    if (techPool != nullptr) {
        for (uint32_t i = 0; i < techPool->size(); ++i) {
            const PlayerTechComponent& tech = techPool->data()[i];
            int32_t count = 0;
            for (std::size_t bit = 0; bit < tech.completedTechs.size(); ++bit) {
                if (tech.completedTechs[bit]) { ++count; }
            }
            stats[tech.owner].techsResearched = count;
            stats[tech.owner].owner = tech.owner;
        }
    }

    // Military units
    const aoc::ecs::ComponentPool<UnitComponent>* unitPool =
        world.getPool<UnitComponent>();
    if (unitPool != nullptr) {
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            const UnitComponent& unit = unitPool->data()[i];
            if (unit.owner == INVALID_PLAYER || unit.owner == BARBARIAN_PLAYER) {
                continue;
            }
            PlayerRawStats& s = stats[unit.owner];
            s.owner = unit.owner;
            ++s.militaryUnits;
            s.totalCombatStrength += static_cast<float>(unitTypeDef(unit.typeId).combatStrength);
        }
    }

    // Monetary: GDP, inflation, treasury, reserve currency
    const aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool != nullptr) {
        for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
            const MonetaryStateComponent& ms = monetaryPool->data()[i];
            PlayerRawStats& s = stats[ms.owner];
            s.owner = ms.owner;
            s.gdp = ms.gdp;
            s.inflationRate = ms.inflationRate;
            s.treasury = ms.treasury;
        }
    }

    // Currency trust / reserve currency
    const aoc::ecs::ComponentPool<CurrencyTrustComponent>* trustPool =
        world.getPool<CurrencyTrustComponent>();
    if (trustPool != nullptr) {
        for (uint32_t i = 0; i < trustPool->size(); ++i) {
            const CurrencyTrustComponent& ct = trustPool->data()[i];
            stats[ct.owner].isReserveCurrency = ct.isReserveCurrency;
        }
    }

    // Bonds
    const aoc::ecs::ComponentPool<PlayerBondComponent>* bondPool =
        world.getPool<PlayerBondComponent>();
    if (bondPool != nullptr) {
        for (uint32_t i = 0; i < bondPool->size(); ++i) {
            const PlayerBondComponent& pb = bondPool->data()[i];
            CurrencyAmount held = 0;
            for (const BondIssue& b : pb.heldBonds) {
                held += b.principal + b.accruedInterest;
            }
            stats[pb.owner].bondHoldings = held;
        }
    }

    // Trade routes: partner count and volume
    const aoc::ecs::ComponentPool<TradeRouteComponent>* tradePool =
        world.getPool<TradeRouteComponent>();
    if (tradePool != nullptr) {
        std::unordered_map<PlayerId, std::unordered_set<PlayerId>> partnerSets;
        for (uint32_t i = 0; i < tradePool->size(); ++i) {
            const TradeRouteComponent& route = tradePool->data()[i];
            partnerSets[route.sourcePlayer].insert(route.destPlayer);
            partnerSets[route.destPlayer].insert(route.sourcePlayer);

            int32_t cargoValue = 0;
            for (const TradeOffer& offer : route.cargo) {
                cargoValue += offer.amountPerTurn * economy.market().price(offer.goodId);
            }
            stats[route.sourcePlayer].tradeVolume += cargoValue;
        }
        for (const std::pair<const PlayerId, std::unordered_set<PlayerId>>& entry : partnerSets) {
            stats[entry.first].tradePartnerCount = static_cast<int32_t>(entry.second.size());
        }
    }

    // Improved tiles (count from grid)
    for (std::pair<const PlayerId, PlayerRawStats>& entry : stats) {
        // Count tiles owned by this player that have improvements
        int32_t improved = 0;
        for (int32_t t = 0; t < grid.tileCount(); ++t) {
            if (grid.owner(t) == entry.first && grid.improvement(t) != aoc::map::ImprovementType::None) {
                ++improved;
            }
        }
        entry.second.improvedTiles = improved;
    }

    return stats;
}

// ============================================================================
// CSI Computation
// ============================================================================

void computeCSI(aoc::ecs::World& world, const aoc::map::HexGrid& grid,
                const EconomySimulation& economy) {
    std::unordered_map<PlayerId, PlayerRawStats> stats = gatherPlayerStats(world, grid, economy);
    if (stats.empty()) {
        return;
    }

    int32_t playerCount = static_cast<int32_t>(stats.size());

    // Compute global averages for relative scoring
    float avgGDP = 0.0f, avgMilitary = 0.0f, avgCulture = 0.0f, avgTech = 0.0f;
    float avgCities = 0.0f, avgPop = 0.0f, avgTreasury = 0.0f;

    for (const std::pair<const PlayerId, PlayerRawStats>& entry : stats) {
        avgGDP += static_cast<float>(entry.second.gdp);
        avgMilitary += entry.second.totalCombatStrength;
        avgCulture += entry.second.culturePerTurn + static_cast<float>(entry.second.wonderCount) * 10.0f;
        avgTech += static_cast<float>(entry.second.techsResearched);
        avgCities += static_cast<float>(entry.second.cityCount);
        avgPop += static_cast<float>(entry.second.totalPopulation);
        avgTreasury += static_cast<float>(entry.second.treasury);
    }

    float invCount = 1.0f / static_cast<float>(std::max(1, playerCount));
    avgGDP *= invCount;
    avgMilitary *= invCount;
    avgCulture *= invCount;
    avgTech *= invCount;
    avgCities *= invCount;
    avgPop *= invCount;
    avgTreasury *= invCount;

    // auto required: lambda type is unnameable
    auto relScore = [](float value, float avg) -> float {
        if (avg < 0.01f) { return 1.0f; }
        return value / avg;
    };

    // Update each player's tracker
    aoc::ecs::ComponentPool<VictoryTrackerComponent>* trackerPool =
        world.getPool<VictoryTrackerComponent>();
    if (trackerPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < trackerPool->size(); ++i) {
        VictoryTrackerComponent& tracker = trackerPool->data()[i];
        std::unordered_map<PlayerId, PlayerRawStats>::iterator it = stats.find(tracker.owner);
        if (it == stats.end()) {
            continue;
        }
        const PlayerRawStats& s = it->second;

        // Category scores (relative to average)
        // Economic: GDP + trade volume + monetary stability
        float econRaw = static_cast<float>(s.gdp) + static_cast<float>(s.tradeVolume) * 0.5f;
        float stabilityBonus = (std::abs(s.inflationRate) < 0.05f) ? 1.1f : 1.0f;
        tracker.categoryScores[0] = relScore(econRaw * stabilityBonus, avgGDP) ;

        // Military
        tracker.categoryScores[1] = relScore(s.totalCombatStrength, avgMilitary);

        // Cultural
        float cultureRaw = s.culturePerTurn + static_cast<float>(s.wonderCount) * 10.0f;
        tracker.categoryScores[2] = relScore(cultureRaw, avgCulture);

        // Scientific
        tracker.categoryScores[3] = relScore(static_cast<float>(s.techsResearched), avgTech);

        // Diplomatic: alliances + trade partners + agreements
        float diplomacyRaw = static_cast<float>(s.tradePartnerCount) * 2.0f
                           + static_cast<float>(s.allianceCount) * 3.0f;
        float avgDiplomacy = static_cast<float>(playerCount); // Rough baseline
        tracker.categoryScores[4] = relScore(diplomacyRaw, avgDiplomacy);

        // Quality of life: happiness + population health
        float qolRaw = s.avgHappiness + static_cast<float>(s.totalPopulation) * 0.1f;
        float avgQol = avgPop * 0.1f + 2.0f;
        tracker.categoryScores[5] = relScore(qolRaw, avgQol);

        // Territorial: cities + improved tiles
        float terrRaw = static_cast<float>(s.cityCount) * 5.0f
                      + static_cast<float>(s.improvedTiles);
        float avgTerr = avgCities * 5.0f + static_cast<float>(s.improvedTiles);
        tracker.categoryScores[6] = relScore(terrRaw, std::max(1.0f, avgTerr));

        // Financial: treasury + bonds + reserve currency
        float finRaw = static_cast<float>(s.treasury)
                     + static_cast<float>(s.bondHoldings) * 0.5f;
        if (s.isReserveCurrency) { finRaw *= 1.3f; }
        tracker.categoryScores[7] = relScore(finRaw, std::max(1.0f, avgTreasury));

        // Interdependence multipliers
        // Trade network: 0 partners=0.7, 1=0.85, 2=0.95, 3=1.0, 4=1.05, 5+=1.1, 6+=1.2
        float tradeMult = 0.70f + static_cast<float>(std::min(s.tradePartnerCount, 6)) * 0.083f;
        tracker.tradeNetworkMultiplier = std::clamp(tradeMult, 0.70f, 1.20f);

        // Financial integration: bonds + reserve currency
        float finMult = 1.0f;
        if (s.bondHoldings > 0) { finMult += 0.05f; }
        if (s.isReserveCurrency) { finMult += 0.10f; }
        tracker.financialIntegrationMult = std::min(finMult, 1.15f);

        // Diplomatic web
        float dipMult = 1.0f + static_cast<float>(s.allianceCount) * 0.03f
                       + static_cast<float>(s.tradePartnerCount) * 0.02f;
        tracker.diplomaticWebMult = std::min(dipMult, 1.15f);

        // Composite CSI = average of all categories * all multipliers
        float categorySum = 0.0f;
        for (int32_t c = 0; c < CSI_CATEGORY_COUNT; ++c) {
            categorySum += tracker.categoryScores[c];
        }
        float categoryAvg = categorySum / static_cast<float>(CSI_CATEGORY_COUNT);

        tracker.compositeCSI = categoryAvg
                             * tracker.tradeNetworkMultiplier
                             * tracker.financialIntegrationMult
                             * tracker.diplomaticWebMult;

        // Legacy score for display/compatibility
        tracker.scienceProgress = s.techsResearched;
        tracker.totalCultureAccumulated += s.culturePerTurn;
        tracker.score = static_cast<int32_t>(tracker.compositeCSI * 1000.0f);

        // Track peak GDP for collapse detection
        if (s.gdp > tracker.peakGDP) {
            tracker.peakGDP = static_cast<int32_t>(s.gdp);
        }
    }
}

// ============================================================================
// Era Evaluation
// ============================================================================

/// Era length in turns.
constexpr TurnNumber ERA_EVALUATION_INTERVAL = 30;

void performEraEvaluation(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<VictoryTrackerComponent>* trackerPool =
        world.getPool<VictoryTrackerComponent>();
    if (trackerPool == nullptr || trackerPool->size() == 0) {
        return;
    }

    // For each category, find top 3 players
    for (int32_t cat = 0; cat < CSI_CATEGORY_COUNT; ++cat) {
        // Collect {score, index} pairs
        struct Entry { float score; uint32_t index; };
        std::vector<Entry> entries;
        entries.reserve(trackerPool->size());
        for (uint32_t i = 0; i < trackerPool->size(); ++i) {
            if (!trackerPool->data()[i].isEliminated) {
                entries.push_back({trackerPool->data()[i].categoryScores[cat], i});
            }
        }
        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b) { return a.score > b.score; });

        // Award VP: 1st=3, 2nd=2, 3rd=1
        if (entries.size() >= 1) { trackerPool->data()[entries[0].index].eraVictoryPoints += 3; }
        if (entries.size() >= 2) { trackerPool->data()[entries[1].index].eraVictoryPoints += 2; }
        if (entries.size() >= 3) { trackerPool->data()[entries[2].index].eraVictoryPoints += 1; }
    }

    // Bonus 5 VP for highest composite CSI
    uint32_t bestIdx = 0;
    float bestCSI = -1.0f;
    for (uint32_t i = 0; i < trackerPool->size(); ++i) {
        if (!trackerPool->data()[i].isEliminated
            && trackerPool->data()[i].compositeCSI > bestCSI) {
            bestCSI = trackerPool->data()[i].compositeCSI;
            bestIdx = i;
        }
    }
    trackerPool->data()[bestIdx].eraVictoryPoints += 5;

    for (uint32_t i = 0; i < trackerPool->size(); ++i) {
        ++trackerPool->data()[i].erasEvaluated;
    }

    LOG_INFO("Era evaluation complete. Top CSI: player %u (%.2f)",
             static_cast<unsigned>(trackerPool->data()[bestIdx].owner),
             static_cast<double>(bestCSI));
}

// ============================================================================
// Collapse (losing conditions)
// ============================================================================

void checkCollapseConditions(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<VictoryTrackerComponent>* trackerPool =
        world.getPool<VictoryTrackerComponent>();
    if (trackerPool == nullptr) {
        return;
    }

    const aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    const aoc::ecs::ComponentPool<CurrencyCrisisComponent>* crisisPool =
        world.getPool<CurrencyCrisisComponent>();

    for (uint32_t i = 0; i < trackerPool->size(); ++i) {
        VictoryTrackerComponent& tracker = trackerPool->data()[i];
        if (tracker.isEliminated) {
            continue;
        }

        // 1. Economic collapse: GDP < 50% of peak for 10 turns
        if (monetaryPool != nullptr) {
            for (uint32_t m = 0; m < monetaryPool->size(); ++m) {
                if (monetaryPool->data()[m].owner == tracker.owner) {
                    CurrencyAmount currentGDP = monetaryPool->data()[m].gdp;
                    if (tracker.peakGDP > 0
                        && currentGDP < static_cast<CurrencyAmount>(tracker.peakGDP) / 2) {
                        ++tracker.turnsGDPBelowHalf;
                    } else {
                        tracker.turnsGDPBelowHalf = 0;
                    }
                    break;
                }
            }
        }
        if (tracker.turnsGDPBelowHalf >= 10) {
            tracker.activeCollapse = CollapseType::EconomicCollapse;
            tracker.isEliminated = true;
            LOG_INFO("Player %u ELIMINATED: economic collapse (GDP < 50%% of peak for 10 turns)",
                     static_cast<unsigned>(tracker.owner));
            continue;
        }

        // 2. Revolution: average loyalty < 30 for 5 turns
        if (cityPool != nullptr) {
            float totalLoyalty = 0.0f;
            int32_t cities = 0;
            for (uint32_t c = 0; c < cityPool->size(); ++c) {
                if (cityPool->data()[c].owner == tracker.owner) {
                    const CityLoyaltyComponent* loyalty =
                        world.tryGetComponent<CityLoyaltyComponent>(cityPool->entities()[c]);
                    if (loyalty != nullptr) {
                        totalLoyalty += loyalty->loyalty;
                    } else {
                        totalLoyalty += 100.0f;
                    }
                    ++cities;
                }
            }
            float avgLoyalty = (cities > 0) ? totalLoyalty / static_cast<float>(cities) : 100.0f;
            if (avgLoyalty < 30.0f) {
                ++tracker.turnsLowLoyalty;
            } else {
                tracker.turnsLowLoyalty = 0;
            }
        }
        if (tracker.turnsLowLoyalty >= 5) {
            tracker.activeCollapse = CollapseType::Revolution;
            tracker.isEliminated = true;
            LOG_INFO("Player %u ELIMINATED: revolution (avg loyalty < 30 for 5 turns)",
                     static_cast<unsigned>(tracker.owner));
            continue;
        }

        // 3. Conquest: lost capital + 75% of cities
        if (cityPool != nullptr) {
            bool hasCapital = false;
            int32_t cities = 0;
            for (uint32_t c = 0; c < cityPool->size(); ++c) {
                if (cityPool->data()[c].owner == tracker.owner) {
                    ++cities;
                    if (cityPool->data()[c].isOriginalCapital) {
                        hasCapital = true;
                    }
                }
            }
            if (!hasCapital && cities <= 1) {
                tracker.activeCollapse = CollapseType::Conquest;
                tracker.isEliminated = true;
                LOG_INFO("Player %u ELIMINATED: conquest (capital lost, %d cities remaining)",
                         static_cast<unsigned>(tracker.owner), cities);
                continue;
            }
        }

        // 4. Debt spiral: default + hyperinflation simultaneously
        if (crisisPool != nullptr && monetaryPool != nullptr) {
            bool inDefault = false;
            bool inHyper = false;
            for (uint32_t c = 0; c < crisisPool->size(); ++c) {
                if (crisisPool->data()[c].owner == tracker.owner) {
                    if (crisisPool->data()[c].activeCrisis == CrisisType::SovereignDefault) {
                        inDefault = true;
                    }
                    if (crisisPool->data()[c].activeCrisis == CrisisType::Hyperinflation) {
                        inHyper = true;
                    }
                    break;
                }
            }
            if (inDefault && inHyper) {
                tracker.activeCollapse = CollapseType::DebtSpiral;
                tracker.isEliminated = true;
                LOG_INFO("Player %u ELIMINATED: debt spiral (default + hyperinflation)",
                         static_cast<unsigned>(tracker.owner));
                continue;
            }
        }
    }
}

// ============================================================================
// Global Integration Project
// ============================================================================

/// All categories must be above this threshold for 10 consecutive turns.
constexpr float INTEGRATION_THRESHOLD = 1.5f;
constexpr int32_t INTEGRATION_TURNS_REQUIRED = 10;

void updateIntegrationProject(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<VictoryTrackerComponent>* trackerPool =
        world.getPool<VictoryTrackerComponent>();
    if (trackerPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < trackerPool->size(); ++i) {
        VictoryTrackerComponent& tracker = trackerPool->data()[i];
        if (tracker.isEliminated || tracker.integrationComplete) {
            continue;
        }

        // Check if ALL categories are above threshold
        bool allAbove = true;
        for (int32_t c = 0; c < CSI_CATEGORY_COUNT; ++c) {
            if (tracker.categoryScores[c] < INTEGRATION_THRESHOLD) {
                allAbove = false;
                break;
            }
        }

        if (allAbove) {
            ++tracker.integrationProgress;
            if (tracker.integrationProgress >= INTEGRATION_TURNS_REQUIRED) {
                tracker.integrationComplete = true;
                LOG_INFO("Player %u completed the GLOBAL INTEGRATION PROJECT!",
                         static_cast<unsigned>(tracker.owner));
            }
        } else {
            tracker.integrationProgress = 0;  // Reset if any category drops below
        }
    }
}

// ============================================================================
// Master victory check
// ============================================================================

VictoryResult checkVictoryConditions(const aoc::ecs::World& world,
                                      TurnNumber currentTurn,
                                      TurnNumber maxTurns) {
    const aoc::ecs::ComponentPool<VictoryTrackerComponent>* trackerPool =
        world.getPool<VictoryTrackerComponent>();
    if (trackerPool == nullptr) {
        return {};
    }

    // 1. Global Integration Project win
    for (uint32_t i = 0; i < trackerPool->size(); ++i) {
        if (trackerPool->data()[i].integrationComplete) {
            return {VictoryType::Integration, trackerPool->data()[i].owner};
        }
    }

    // 2. Last standing: all but one eliminated
    int32_t alive = 0;
    PlayerId lastAlive = INVALID_PLAYER;
    for (uint32_t i = 0; i < trackerPool->size(); ++i) {
        if (!trackerPool->data()[i].isEliminated) {
            ++alive;
            lastAlive = trackerPool->data()[i].owner;
        }
    }
    if (alive == 1 && trackerPool->size() > 1) {
        return {VictoryType::LastStanding, lastAlive};
    }

    // 3. Turn limit: highest cumulative Era VP wins
    if (currentTurn >= maxTurns) {
        PlayerId bestPlayer = INVALID_PLAYER;
        int32_t bestVP = -1;
        for (uint32_t i = 0; i < trackerPool->size(); ++i) {
            if (!trackerPool->data()[i].isEliminated
                && trackerPool->data()[i].eraVictoryPoints > bestVP) {
                bestVP = trackerPool->data()[i].eraVictoryPoints;
                bestPlayer = trackerPool->data()[i].owner;
            }
        }
        if (bestPlayer != INVALID_PLAYER) {
            LOG_INFO("Player %u wins by SCORE (Era VP = %d) at turn %d",
                     static_cast<unsigned>(bestPlayer), bestVP,
                     static_cast<int>(currentTurn));
            return {VictoryType::Score, bestPlayer};
        }
    }

    return {};
}

// ============================================================================
// Master per-turn update
// ============================================================================

void updateVictoryTrackers(aoc::ecs::World& world, const aoc::map::HexGrid& grid,
                           const EconomySimulation& economy, TurnNumber currentTurn) {
    computeCSI(world, grid, economy);
    checkCollapseConditions(world);
    updateIntegrationProject(world);

    // Era evaluation every 30 turns
    if (currentTurn > 0 && (currentTurn % ERA_EVALUATION_INTERVAL) == 0) {
        performEraEvaluation(world);
    }
}

// Backwards-compatible overload (limited scoring without economy data)
void updateVictoryTrackers(aoc::ecs::World& world, const aoc::map::HexGrid& grid) {
    // Minimal update: just compute basic stats without full CSI
    aoc::ecs::ComponentPool<VictoryTrackerComponent>* trackerPool =
        world.getPool<VictoryTrackerComponent>();
    if (trackerPool == nullptr) {
        return;
    }

    const aoc::ecs::ComponentPool<PlayerTechComponent>* techPool =
        world.getPool<PlayerTechComponent>();
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();

    for (uint32_t i = 0; i < trackerPool->size(); ++i) {
        VictoryTrackerComponent& tracker = trackerPool->data()[i];

        // Techs
        if (techPool != nullptr) {
            for (uint32_t t = 0; t < techPool->size(); ++t) {
                if (techPool->data()[t].owner == tracker.owner) {
                    int32_t count = 0;
                    for (std::size_t bit = 0; bit < techPool->data()[t].completedTechs.size(); ++bit) {
                        if (techPool->data()[t].completedTechs[bit]) { ++count; }
                    }
                    tracker.scienceProgress = count;
                    break;
                }
            }
        }

        // Culture
        tracker.totalCultureAccumulated += computePlayerCulture(world, grid, tracker.owner);

        // Basic score
        int32_t cityCount = 0;
        int32_t pop = 0;
        if (cityPool != nullptr) {
            for (uint32_t c = 0; c < cityPool->size(); ++c) {
                if (cityPool->data()[c].owner == tracker.owner) {
                    ++cityCount;
                    pop += cityPool->data()[c].population;
                }
            }
        }
        tracker.score = pop * 5 + tracker.scienceProgress * 10 + cityCount * 20
                      + static_cast<int32_t>(tracker.totalCultureAccumulated);
    }
}

} // namespace aoc::sim
