/**
 * @file VictoryCondition.cpp
 * @brief CSI-based victory system: composite scoring, era evaluation,
 *        interdependence bonuses, losing conditions, and integration project.
 */

#include "aoc/game/Unit.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"
#include "aoc/simulation/victory/Prestige.hpp"
#include "aoc/simulation/victory/SpaceRace.hpp"
#include "aoc/balance/BalanceParams.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
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
    // WP-N2: tourism. Distinct accumulator from culture-per-turn. Sourced
    // from late-era wonders + Theatre district buildings. Folded into the
    // culture-toward-victory total at 1.5x once Renaissance unlocks.
    float tourismPerTurn = 0.0f;
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
    const aoc::game::GameState& gameState,
    const aoc::map::HexGrid& grid,
    const EconomySimulation& economy,
    const DiplomacyManager* diplomacy) {
    std::unordered_map<PlayerId, PlayerRawStats> stats;

    for (const std::unique_ptr<aoc::game::Player>& gsPlayer : gameState.players()) {
        const PlayerId pid = gsPlayer->id();
        if (pid == INVALID_PLAYER || pid == BARBARIAN_PLAYER) {
            continue;
        }
        PlayerRawStats& s = stats[pid];
        s.owner = pid;

        // Cities: population, happiness, loyalty, wonders, tourism
        int32_t theatreBuildings = 0;
        for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
            ++s.cityCount;
            s.totalPopulation += city->population();
            s.avgHappiness += city->happiness().amenities - city->happiness().demand;
            s.avgLoyalty  += city->loyalty().loyalty;
            s.wonderCount += static_cast<int32_t>(city->wonders().wonders.size());
            for (const aoc::sim::CityDistrictsComponent::PlacedDistrict& d
                    : city->districts().districts) {
                if (d.type == aoc::sim::DistrictType::Theatre) {
                    theatreBuildings += static_cast<int32_t>(d.buildings.size());
                }
            }
        }
        // WP-N2: tourism = late-era wonders ×5 + Theatre buildings ×2.
        s.tourismPerTurn = static_cast<float>(s.wonderCount) * 5.0f
                         + static_cast<float>(theatreBuildings) * 2.0f;
        if (s.cityCount > 0) {
            s.avgHappiness /= static_cast<float>(s.cityCount);
            s.avgLoyalty   /= static_cast<float>(s.cityCount);
        }

        // Culture
        s.culturePerTurn = computePlayerCulture(gameState, grid, pid);

        // Techs
        {
            int32_t count = 0;
            for (std::size_t bit = 0; bit < gsPlayer->tech().completedTechs.size(); ++bit) {
                if (gsPlayer->tech().completedTechs[bit]) { ++count; }
            }
            s.techsResearched = count;
        }

        // Military units
        for (const std::unique_ptr<aoc::game::Unit>& unit : gsPlayer->units()) {
            ++s.militaryUnits;
            s.totalCombatStrength +=
                static_cast<float>(unitTypeDef(unit->typeId()).combatStrength);
        }

        // Monetary: GDP, inflation, treasury
        {
            const aoc::sim::MonetaryStateComponent& ms = gsPlayer->monetary();
            s.gdp           = ms.gdp;
            s.inflationRate = ms.inflationRate;
            s.treasury      = ms.treasury;
        }

        // Currency trust / reserve currency
        s.isReserveCurrency = gsPlayer->currencyTrust().isReserveCurrency;

        // Bonds
        {
            CurrencyAmount held = 0;
            for (const BondIssue& b : gsPlayer->bonds().heldBonds) {
                held += b.principal + b.accruedInterest;
            }
            s.bondHoldings = held;
        }

        // Improved tiles
        {
            int32_t improved = 0;
            for (int32_t t = 0; t < grid.tileCount(); ++t) {
                if (grid.owner(t) == pid
                    && grid.improvement(t) != aoc::map::ImprovementType::None) {
                    ++improved;
                }
            }
            s.improvedTiles = improved;
        }
    }

    // Trade routes: partner count and volume (global collection on GameState)
    {
        std::unordered_map<PlayerId, std::unordered_set<PlayerId>> partnerSets;
        for (const TradeRouteComponent& route : gameState.tradeRoutes()) {
            partnerSets[route.sourcePlayer].insert(route.destPlayer);
            partnerSets[route.destPlayer].insert(route.sourcePlayer);

            int32_t cargoValue = 0;
            for (const TradeOffer& offer : route.cargo) {
                cargoValue += offer.amountPerTurn * economy.market().price(offer.goodId);
            }
            stats[route.sourcePlayer].tradeVolume += cargoValue;
        }
        for (const std::pair<const PlayerId, std::unordered_set<PlayerId>>& entry : partnerSets) {
            stats[entry.first].tradePartnerCount =
                static_cast<int32_t>(entry.second.size());
        }
    }

    // Alliance counts: tally any-type alliances from DiplomacyManager so the
    // diplomaticWebMult in computeCSI actually responds to alliance formation.
    // Prior code left allianceCount at zero, making alliances invisible to CSI.
    if (diplomacy != nullptr) {
        for (std::pair<const PlayerId, PlayerRawStats>& entry : stats) {
            const PlayerId pid = entry.first;
            int32_t count = 0;
            for (std::pair<const PlayerId, PlayerRawStats>& other : stats) {
                if (other.first == pid) { continue; }
                if (diplomacy->relation(pid, other.first).hasAnyAlliance()) {
                    ++count;
                }
            }
            entry.second.allianceCount = count;
        }
    }

    return stats;
}

// ============================================================================
// CSI Computation
// ============================================================================

void computeCSI(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                const EconomySimulation& economy,
                const DiplomacyManager* diplomacy) {
    std::unordered_map<PlayerId, PlayerRawStats> stats =
        gatherPlayerStats(gameState, grid, economy, diplomacy);
    if (stats.empty()) {
        return;
    }

    int32_t playerCount = static_cast<int32_t>(stats.size());

    // Compute global averages for relative scoring
    float avgGDP = 0.0f, avgMilitary = 0.0f, avgCulture = 0.0f, avgTech = 0.0f;
    float avgCities = 0.0f, avgPop = 0.0f;
    float avgDiplomacy = 0.0f, avgFinancial = 0.0f;
    float avgImprovedTiles = 0.0f;

    for (const std::pair<const PlayerId, PlayerRawStats>& entry : stats) {
        avgGDP += static_cast<float>(entry.second.gdp);
        avgMilitary += entry.second.totalCombatStrength;
        avgCulture += entry.second.culturePerTurn + static_cast<float>(entry.second.wonderCount) * 10.0f;
        avgTech += static_cast<float>(entry.second.techsResearched);
        avgCities += static_cast<float>(entry.second.cityCount);
        avgPop += static_cast<float>(entry.second.totalPopulation);
        avgDiplomacy += 1.0f + static_cast<float>(entry.second.tradePartnerCount) * 2.0f;
        avgImprovedTiles += static_cast<float>(entry.second.improvedTiles);
        // Clamp negative treasury (debt) to 0 for averaging so a single deeply
        // indebted civ cannot invert the financial category for everyone else.
        float finRaw = std::max(0.0f, static_cast<float>(entry.second.treasury))
                     + static_cast<float>(entry.second.bondHoldings) * 0.5f;
        if (entry.second.isReserveCurrency) { finRaw *= 1.3f; }
        avgFinancial += finRaw;
    }

    float invCount = 1.0f / static_cast<float>(std::max(1, playerCount));
    avgGDP *= invCount;
    avgMilitary *= invCount;
    avgCulture *= invCount;
    avgTech *= invCount;
    avgCities *= invCount;
    avgPop *= invCount;
    avgDiplomacy *= invCount;
    avgFinancial *= invCount;
    avgImprovedTiles *= invCount;

    // auto required: lambda type is unnameable
    auto relScore = [](float value, float avg) -> float {
        if (avg < 0.01f) { return 1.0f; }
        return value / avg;
    };

    // Update each player's tracker via Player object model
    for (const std::unique_ptr<aoc::game::Player>& gsPlayer : gameState.players()) {
        VictoryTrackerComponent& tracker = gsPlayer->victoryTracker();
        std::unordered_map<PlayerId, PlayerRawStats>::iterator it = stats.find(gsPlayer->id());
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

        // Diplomatic: trade partners (alliance tracking lives elsewhere; not
        // plumbed through here yet, so baseline on trade partners alone).
        // Add +1 flat so a civ with zero partners still scores against the
        // average rather than forcing the per-player avg toward zero.
        float diplomacyRaw = 1.0f + static_cast<float>(s.tradePartnerCount) * 2.0f;
        tracker.categoryScores[4] = relScore(diplomacyRaw, avgDiplomacy);

        // Quality of life: happiness + population health
        float qolRaw = s.avgHappiness + static_cast<float>(s.totalPopulation) * 0.1f;
        float avgQol = avgPop * 0.1f + 2.0f;
        tracker.categoryScores[5] = relScore(qolRaw, avgQol);

        // Territorial: cities + improved tiles, scored vs global mean of both.
        float terrRaw = static_cast<float>(s.cityCount) * 5.0f
                      + static_cast<float>(s.improvedTiles);
        float avgTerr = avgCities * 5.0f + avgImprovedTiles;
        tracker.categoryScores[6] = relScore(terrRaw, std::max(1.0f, avgTerr));

        // Financial: treasury (non-negative) + bonds + reserve currency.
        // Debt (negative treasury) scores 0, not a negative number, so a
        // bankrupt civ cannot flip the relative score into nonsense.
        float finRaw = std::max(0.0f, static_cast<float>(s.treasury))
                     + static_cast<float>(s.bondHoldings) * 0.5f;
        if (s.isReserveCurrency) { finRaw *= 1.3f; }
        tracker.categoryScores[7] = relScore(finRaw, std::max(1.0f, avgFinancial));

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
        // WP-N1: per-era multiplier on culture-toward-victory. Civ-6
        // tourism analog: pre-Renaissance culture builds civic unlocks
        // but barely contributes to victory. Renaissance opens the gate;
        // Industrial+ scales toward 1.0×.
        const uint16_t eraVal = gsPlayer->era().currentEra.value;
        float eraMult = 0.0f;
        switch (eraVal) {
            case 0:         eraMult = 0.1f;  break;  // Ancient
            case 1:         eraMult = 0.3f;  break;  // Classical
            case 2:         eraMult = 0.5f;  break;  // Medieval
            case 3:         eraMult = 0.8f;  break;  // Renaissance
            case 4:         eraMult = 1.2f;  break;  // Industrial
            case 5:         eraMult = 1.5f;  break;  // Modern
            case 6:         eraMult = 1.8f;  break;  // Atomic
            default:        eraMult = 2.2f;  break;  // Information+
        }
        // WP-N2: tourism rides on the same era gate but at 1.5x rate so it
        // dominates accumulation late-game. Pre-Renaissance contributes zero
        // (eraMult = 0). Wonders + Theatre buildings drive it; Civ-6 analog.
        tracker.totalCultureAccumulated +=
            (s.culturePerTurn * 0.5f + s.tourismPerTurn * 1.5f) * eraMult;
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

void performEraEvaluation(aoc::game::GameState& gameState) {
    if (gameState.players().empty()) {
        return;
    }

    // For each category, find top 3 players by score.
    // We work with raw Player pointers so we can award VP through the object model.
    for (int32_t cat = 0; cat < CSI_CATEGORY_COUNT; ++cat) {
        struct Entry {
            float score;
            aoc::game::Player* player;
        };
        std::vector<Entry> entries;
        entries.reserve(gameState.players().size());
        for (const std::unique_ptr<aoc::game::Player>& gsPlayer : gameState.players()) {
            const VictoryTrackerComponent& tracker = gsPlayer->victoryTracker();
            if (!tracker.isEliminated) {
                entries.push_back({tracker.categoryScores[cat], gsPlayer.get()});
            }
        }
        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b) { return a.score > b.score; });

        // Award VP: 1st=3, 2nd=2, 3rd=1
        if (entries.size() >= 1) { entries[0].player->victoryTracker().eraVictoryPoints += 3; }
        if (entries.size() >= 2) { entries[1].player->victoryTracker().eraVictoryPoints += 2; }
        if (entries.size() >= 3) { entries[2].player->victoryTracker().eraVictoryPoints += 1; }

        // B7 catch-up: laggards beyond rank 3 whose category score is
        // less than half the leader's receive +1 VP. Without this the
        // 3/2/1 top-3 structure lets leaders compound their lead each
        // era; the score-gap floor keeps trailing civs in contention
        // for the late-game VP total without handing them a free ride.
        if (entries.size() >= 4 && entries[0].score > 0.0f) {
            const float catchupThreshold = entries[0].score * 0.5f;
            for (std::size_t i = 3; i < entries.size(); ++i) {
                if (entries[i].score > 0.0f && entries[i].score < catchupThreshold) {
                    entries[i].player->victoryTracker().eraVictoryPoints += 1;
                }
            }
        }
    }

    // Bonus 5 VP for highest composite CSI; increment erasEvaluated for all players.
    aoc::game::Player* bestPlayer = nullptr;
    float bestCSI = -1.0f;
    for (const std::unique_ptr<aoc::game::Player>& gsPlayer : gameState.players()) {
        VictoryTrackerComponent& tracker = gsPlayer->victoryTracker();
        ++tracker.erasEvaluated;
        if (!tracker.isEliminated && tracker.compositeCSI > bestCSI) {
            bestCSI = tracker.compositeCSI;
            bestPlayer = gsPlayer.get();
        }
    }
    if (bestPlayer != nullptr) {
        bestPlayer->victoryTracker().eraVictoryPoints += 5;
        LOG_INFO("Era evaluation complete. Top CSI: player %u (%.2f)",
                 static_cast<unsigned>(bestPlayer->id()),
                 static_cast<double>(bestCSI));
    }
}

// ============================================================================
// Collapse (losing conditions)
// ============================================================================

// Minimum peak GDP before economic collapse can trigger. Civilizations with very
// low peak GDP are still in the early expansion phase where natural volatility
// would otherwise cause spurious eliminations.
static constexpr int32_t COLLAPSE_PEAK_GDP_FLOOR = 100;

// GDP must fall below this fraction of peak GDP to count as a collapse turn.
// 25% (divide by 4) is far more forgiving than the original 50% threshold and
// matches the intent of a true civilisational collapse rather than a recession.
static constexpr int32_t COLLAPSE_GDP_DIVISOR = 4;

// Number of consecutive below-threshold turns required before elimination fires.
static constexpr int32_t COLLAPSE_TURNS_REQUIRED = 30;

// No player can be eliminated before this turn, preventing early-game accidents
// from snowballing into immediate game-overs.
static constexpr TurnNumber COLLAPSE_MIN_TURN = 100;

void checkCollapseConditions(aoc::game::GameState& gameState, TurnNumber currentTurn) {
    for (const std::unique_ptr<aoc::game::Player>& gsPlayer : gameState.players()) {
        VictoryTrackerComponent& tracker = gsPlayer->victoryTracker();
        if (tracker.isEliminated) {
            continue;
        }

        // 1. Economic collapse: GDP < 25% of peak for 30 consecutive turns,
        //    only after turn 100 and only when peak GDP was meaningful (>= 100).
        {
            const CurrencyAmount currentGDP = gsPlayer->monetary().gdp;
            const bool peakIsMeaningful = (tracker.peakGDP >= COLLAPSE_PEAK_GDP_FLOOR);
            const CurrencyAmount collapseFloor =
                static_cast<CurrencyAmount>(tracker.peakGDP) / COLLAPSE_GDP_DIVISOR;
            if (peakIsMeaningful && currentGDP < collapseFloor) {
                ++tracker.turnsGDPBelowHalf;
            } else {
                tracker.turnsGDPBelowHalf = 0;
            }
        }
        if (tracker.turnsGDPBelowHalf >= COLLAPSE_TURNS_REQUIRED
            && currentTurn >= COLLAPSE_MIN_TURN) {
            tracker.activeCollapse = CollapseType::EconomicCollapse;
            tracker.isEliminated = true;
            LOG_INFO("Player %u ELIMINATED: economic collapse "
                     "(GDP < 25%% of peak for %d turns, peak was %d)",
                     static_cast<unsigned>(gsPlayer->id()),
                     COLLAPSE_TURNS_REQUIRED,
                     tracker.peakGDP);
            continue;
        }

        // 2. Revolution: average loyalty < 30 for 5 turns
        {
            float totalLoyalty = 0.0f;
            int32_t cities = 0;
            for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
                totalLoyalty += city->loyalty().loyalty;
                ++cities;
            }
            const float avgLoyalty = (cities > 0)
                ? totalLoyalty / static_cast<float>(cities)
                : 100.0f;
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
                     static_cast<unsigned>(gsPlayer->id()));
            continue;
        }

        // 3. Conquest: lost capital + only one city remaining.
        // Gate on hasEverFoundedCity: pre-settlement civs (turn-1 Settler still
        // walking) have cityCount == 0 and would otherwise trip this branch,
        // marking them eliminated before they even play their first move.
        // That fired LastStanding on turn 1 against a 2-player setup.
        {
            bool hasCapital = false;
            const int32_t cities = gsPlayer->cityCount();
            for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
                if (city->isOriginalCapital()) {
                    hasCapital = true;
                    break;
                }
            }
            if (cities > 0) {
                tracker.hasEverFoundedCity = true;
            }
            if (tracker.hasEverFoundedCity && !hasCapital && cities <= 1) {
                tracker.activeCollapse = CollapseType::Conquest;
                tracker.isEliminated = true;
                LOG_INFO("Player %u ELIMINATED: conquest (capital lost, %d cities remaining)",
                         static_cast<unsigned>(gsPlayer->id()), cities);
                continue;
            }
        }

        // 4. Debt spiral: sovereign default + hyperinflation simultaneously
        {
            const aoc::sim::CurrencyCrisisComponent& crisis = gsPlayer->currencyCrisis();
            const bool inDefault = (crisis.activeCrisis == CrisisType::SovereignDefault);
            const bool inHyper   = (crisis.activeCrisis == CrisisType::Hyperinflation);
            if (inDefault && inHyper) {
                tracker.activeCollapse = CollapseType::DebtSpiral;
                tracker.isEliminated = true;
                LOG_INFO("Player %u ELIMINATED: debt spiral (default + hyperinflation)",
                         static_cast<unsigned>(gsPlayer->id()));
                continue;
            }
        }
    }
}

// ============================================================================
// Master victory check
// ============================================================================

VictoryResult checkVictoryConditions(const aoc::game::GameState& gameState,
                                      TurnNumber currentTurn,
                                      TurnNumber maxTurns,
                                      uint32_t enabledTypes,
                                      const DiplomacyManager* diplomacy) {
    if (gameState.players().empty()) {
        return {};
    }

    // 1. Last standing: all but one eliminated
    int32_t alive = 0;
    PlayerId lastAlive = INVALID_PLAYER;
    for (const std::unique_ptr<aoc::game::Player>& gsPlayer : gameState.players()) {
        if (!gsPlayer->victoryTracker().isEliminated) {
            ++alive;
            lastAlive = gsPlayer->id();
        }
    }
    if ((enabledTypes & VICTORY_MASK_LAST_STANDING) != 0u
        && alive == 1 && gameState.playerCount() > 1) {
        return {VictoryType::LastStanding, lastAlive};
    }

    // ================================================================
    // Classic Victory Conditions (checked alongside default system)
    // These trigger regardless of VictoryMode — the mode only determines
    // whether they're the PRIMARY win conditions or bonus achievements.
    // ================================================================

    // 3a. Domination Victory: own ≥60% of other civs' original capitals.
    // Audit 2026-04: full all-capitals requirement never fired in
    // 1500t sims (6-player game = conquer 5 capitals). Relaxed to 60%
    // (3-of-5 for 6-player) so a strong military civ has a reachable
    // path. Still meaningful — conquering 3 capitals is a major
    // achievement.
    if ((enabledTypes & VICTORY_MASK_DOMINATION) != 0u) {
        // WP-D1 BUG FIX: candidate->cities() is the founder list; captured
        // cities REMAIN in the original founder's list with `owner()` updated
        // to the conqueror. Iterate ALL cities globally and check
        // `city->owner() == candidate->id() && originalOwner() == other->id()`.
        // Previously this loop never saw captured capitals.
        for (const std::unique_ptr<aoc::game::Player>& candidate : gameState.players()) {
            if (candidate->victoryTracker().isEliminated) { continue; }
            int32_t rivalCount = 0;
            int32_t capitalsOwned = 0;
            for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
                if (other->id() == candidate->id()) { continue; }
                if (other->victoryTracker().isEliminated) { continue; }
                ++rivalCount;
                bool foundCapital = false;
                // Walk all founder lists; a captured capital still lives in
                // the original founder's m_cities even after ownership flip.
                for (const std::unique_ptr<aoc::game::Player>& holder
                        : gameState.players()) {
                    for (const std::unique_ptr<aoc::game::City>& city
                            : holder->cities()) {
                        if (city->owner() != candidate->id()) { continue; }
                        if (!city->isOriginalCapital()) { continue; }
                        if (city->originalOwner() != other->id()) { continue; }
                        foundCapital = true;
                        break;
                    }
                    if (foundCapital) { break; }
                }
                if (foundCapital) {
                    ++capitalsOwned;
                }
            }
            const float ratio = (rivalCount > 0)
                ? static_cast<float>(capitalsOwned) / static_cast<float>(rivalCount)
                : 0.0f;
            // 2026-04-28 iter11: combined 384-sim Dom 14.8%. Widen late-game
            // escape from "≤4 alive" to "≤5 alive" so 8-player games entering
            // late attrition (3 dead) qualify with 1 captured capital.
            if (alive > 1
                && (capitalsOwned >= 2
                    || (capitalsOwned >= 1 && alive <= 5))) {
                LOG_INFO("Player %u wins by DOMINATION (%d/%d rival capitals owned)",
                         static_cast<unsigned>(candidate->id()),
                         capitalsOwned, rivalCount);
                return {VictoryType::Domination, candidate->id()};
            }
        }
    }

    // 3b. Science Victory: completed 4 of 5 Space Race projects.
    // Audit 2026-04: all-5 (inc. Exoplanet / Nanotechnology tech 25) was
    // unreachable in 1500t sims. 4-of-5 still requires the full Earth →
    // Moon → Lunar → Mars chain and is a meaningful achievement, while
    // allowing Science path to actually resolve.
    if ((enabledTypes & VICTORY_MASK_SCIENCE) != 0u) {
        for (const std::unique_ptr<aoc::game::Player>& candidate : gameState.players()) {
            if (candidate->victoryTracker().isEliminated) { continue; }
            // 2026-04-27 iter6: 4-of-5 → 43%; revert to 5-of-5 with eased cost.
            if (candidate->spaceRace().completedCount() >= 5) {
                LOG_INFO("Player %u wins by SCIENCE (%d/5 space projects completed)",
                         static_cast<unsigned>(candidate->id()),
                         candidate->spaceRace().completedCount());
                return {VictoryType::Science, candidate->id()};
            }
        }
    }

    // 3c. Religious Victory: your religion is dominant in >50% of every other civ's cities
    if ((enabledTypes & VICTORY_MASK_RELIGION) != 0u) {
        for (const std::unique_ptr<aoc::game::Player>& candidate : gameState.players()) {
            if (candidate->victoryTracker().isEliminated) { continue; }
            const ReligionId myReligion = candidate->faith().foundedReligion;
            if (myReligion == NO_RELIGION) { continue; }

            // B6 relaxation: previously required dominance in EVERY other
            // living civ, which is impossible under the 8-religion slot cap
            // and hostile missionary pathing. Now requires 3-of-4 (75%) of
            // the other living civs. A single hold-out no longer vetoes a
            // religious victory that is otherwise globally dominant.
            int32_t dominatedCount = 0;
            int32_t rivalCount     = 0;
            for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
                if (other->id() == candidate->id()) { continue; }
                if (other->victoryTracker().isEliminated) { continue; }
                if (other->cities().empty()) { continue; }

                ++rivalCount;
                int32_t followingCities = 0;
                for (const std::unique_ptr<aoc::game::City>& city : other->cities()) {
                    if (city->religion().dominantReligion() == myReligion) {
                        ++followingCities;
                    }
                }
                const float needed = static_cast<float>(other->cities().size())
                                   * aoc::balance::params().religionDominanceFrac;
                if (static_cast<float>(followingCities) >= needed) {
                    ++dominatedCount;
                }
            }

            if (rivalCount > 0) {
                // Audit 2026-04 third pass: 0.60 (3-of-5) still 0 fires.
                // Relax to 0.50 (majority of rivals dominated) — a clear
                // bar but achievable for a faith-focused civ with the
                // boosted BASE_PASSIVE_PRESSURE in Religion.cpp.
                const float ratio = static_cast<float>(dominatedCount)
                                  / static_cast<float>(rivalCount);
                if (ratio >= 0.35f) {  // 2026-04-28 iter10: 0.40 → 13%; relax further
                    LOG_INFO("Player %u wins by RELIGION (%d/%d rivals dominated)",
                             static_cast<unsigned>(candidate->id()),
                             dominatedCount, rivalCount);
                    return {VictoryType::Religion, candidate->id()};
                }
            }
        }
    }

    // 3d. Culture Victory: accumulated culture, wonder count, and clear lead.
    // A civ wins if:
    //   - totalCultureAccumulated >= CULTURE_VICTORY_THRESHOLD
    //   - owns >= CULTURE_VICTORY_MIN_WONDERS wonders
    //   - culture total is at least 2x the next-best non-eliminated civ
    // The 2x gap prevents photo-finish flips near the threshold and forces
    // the winner to have meaningfully out-produced everyone on culture.
    if ((enabledTypes & VICTORY_MASK_CULTURE) != 0u) {
        const aoc::balance::BalanceParams& bal = aoc::balance::params();
        // Scale threshold by GamePace so 300t and 2000t games take comparable
        // proportion-of-game to Culture-win.
        const float CULTURE_VICTORY_THRESHOLD = bal.cultureVictoryThreshold
                                              * aoc::sim::GamePace::instance().costMultiplier;
        const int32_t CULTURE_VICTORY_MIN_WONDERS = bal.cultureVictoryMinWonders;
        const float CULTURE_VICTORY_LEAD_RATIO = bal.cultureVictoryLeadRatio;

        PlayerId leader = INVALID_PLAYER;
        float bestCulture = 0.0f;
        int32_t leaderWonders = 0;

        for (const std::unique_ptr<aoc::game::Player>& candidate : gameState.players()) {
            if (candidate->victoryTracker().isEliminated) { continue; }
            const float acc = candidate->victoryTracker().totalCultureAccumulated;
            if (acc > bestCulture) {
                bestCulture = acc;
                leader = candidate->id();
                int32_t wonders = 0;
                for (const std::unique_ptr<aoc::game::City>& city : candidate->cities()) {
                    wonders += static_cast<int32_t>(city->wonders().wonders.size());
                }
                leaderWonders = wonders;
            }
        }

        if (leader != INVALID_PLAYER
            && bestCulture >= CULTURE_VICTORY_THRESHOLD
            && leaderWonders >= CULTURE_VICTORY_MIN_WONDERS) {
            bool clearLead = true;
            for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
                if (other->id() == leader) { continue; }
                if (other->victoryTracker().isEliminated) { continue; }
                const float otherAcc = other->victoryTracker().totalCultureAccumulated;
                if (otherAcc * CULTURE_VICTORY_LEAD_RATIO > bestCulture) {
                    clearLead = false;
                    break;
                }
            }
            // Political tourism gate (2026-04-27): leader's foreign-tourist
            // count must exceed every rival's domestic-tourist count. Open
            // borders, allies, and friendly stance accelerate tourism;
            // war/hostile/embargo throttle it. Without this, culture wins
            // ignore diplomatic resistance entirely.
            bool tourismDominant = true;
            const aoc::game::Player* leaderPlayer = gameState.player(leader);
            if (leaderPlayer != nullptr) {
                const int32_t leaderForeign =
                    leaderPlayer->tourism().foreignTourists;
                for (const std::unique_ptr<aoc::game::Player>& other :
                         gameState.players()) {
                    if (other == nullptr || other->id() == leader) { continue; }
                    if (other->victoryTracker().isEliminated) { continue; }
                    if (leaderForeign <= other->tourism().domesticTourists) {
                        tourismDominant = false;
                        break;
                    }
                }
            }
            if (clearLead && tourismDominant) {
                LOG_INFO("Player %u wins by CULTURE (culture=%.0f wonders=%d)",
                         static_cast<unsigned>(leader),
                         static_cast<double>(bestCulture),
                         leaderWonders);
                return {VictoryType::Culture, leader};
            }
        }
    }

    (void)diplomacy;  // Pairwise matrix not currently used by victory checks.

    // 4. Turn limit: Prestige tally first (participation-based endgame).
    // Prestige wins when all turns have elapsed -- highest accumulated total
    // across the 7 categories (science, culture, faith, trade, diplomacy,
    // military, governance) wins.  Max per turn is capped, so the achievable
    // ceiling scales with maxTurns and works for any game length.
    if ((enabledTypes & VICTORY_MASK_PRESTIGE) != 0u && currentTurn >= maxTurns) {
        // Tiebreaker: primary = prestige total, secondary = compositeCSI,
        // tertiary = lowest playerId. Without this, iteration order decided
        // ties between players with equal prestige.
        PlayerId bestPlayer = INVALID_PLAYER;
        float bestPrestige = -1.0f;
        float bestCSI = -1.0f;
        for (const std::unique_ptr<aoc::game::Player>& gsPlayer : gameState.players()) {
            if (gsPlayer->victoryTracker().isEliminated) { continue; }
            const float total = gsPlayer->prestige().total;
            const float csi   = gsPlayer->victoryTracker().compositeCSI;
            const PlayerId id = gsPlayer->id();
            const bool better =
                (total >  bestPrestige) ||
                (total == bestPrestige && csi >  bestCSI) ||
                (total == bestPrestige && csi == bestCSI && id < bestPlayer);
            if (better) {
                bestPrestige = total;
                bestCSI      = csi;
                bestPlayer   = id;
            }
        }
        if (bestPlayer != INVALID_PLAYER) {
            LOG_INFO("Player %u wins by PRESTIGE (score = %.1f, CSI = %.2f) at turn %d",
                     static_cast<unsigned>(bestPlayer),
                     static_cast<double>(bestPrestige),
                     static_cast<double>(bestCSI),
                     static_cast<int>(currentTurn));
            return {VictoryType::Prestige, bestPlayer};
        }
    }

    // 5. Turn limit (fallback): highest cumulative Era VP wins.
    // Tiebreaker: primary = eraVictoryPoints, secondary = compositeCSI,
    // tertiary = lowest playerId.
    if ((enabledTypes & VICTORY_MASK_SCORE) != 0u && currentTurn >= maxTurns) {
        PlayerId bestPlayer = INVALID_PLAYER;
        int32_t bestVP = -1;
        float   bestCSI = -1.0f;
        for (const std::unique_ptr<aoc::game::Player>& gsPlayer : gameState.players()) {
            const VictoryTrackerComponent& tracker = gsPlayer->victoryTracker();
            if (tracker.isEliminated) { continue; }
            const int32_t vp  = tracker.eraVictoryPoints;
            const float   csi = tracker.compositeCSI;
            const PlayerId id = gsPlayer->id();
            const bool better =
                (vp >  bestVP) ||
                (vp == bestVP && csi >  bestCSI) ||
                (vp == bestVP && csi == bestCSI && id < bestPlayer);
            if (better) {
                bestVP     = vp;
                bestCSI    = csi;
                bestPlayer = id;
            }
        }
        if (bestPlayer != INVALID_PLAYER) {
            LOG_INFO("Player %u wins by SCORE (Era VP = %d, CSI = %.2f) at turn %d",
                     static_cast<unsigned>(bestPlayer), bestVP,
                     static_cast<double>(bestCSI),
                     static_cast<int>(currentTurn));
            return {VictoryType::Score, bestPlayer};
        }
    }

    return {};
}

// ============================================================================
// Victory-type mask parser
// ============================================================================

uint32_t parseVictoryTypeMask(std::string_view list) {
    if (list.empty()) { return VICTORY_MASK_ALL; }

    struct Token { std::string_view name; uint32_t bit; };
    constexpr Token kTable[] = {
        {"score",        VICTORY_MASK_SCORE},
        {"prestige",     VICTORY_MASK_PRESTIGE},
        {"integration",  VICTORY_MASK_PRESTIGE},  // legacy alias
        {"laststanding", VICTORY_MASK_LAST_STANDING},
        {"science",      VICTORY_MASK_SCIENCE},
        {"domination",   VICTORY_MASK_DOMINATION},
        {"culture",      VICTORY_MASK_CULTURE},
        {"religion",     VICTORY_MASK_RELIGION},
        {"all",          VICTORY_MASK_ALL},
    };

    auto normalize = [](std::string_view in) {
        std::string out;
        out.reserve(in.size());
        for (char c : in) {
            if (c == ' ' || c == '_' || c == '-') { continue; }
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return out;
    };

    uint32_t mask = 0u;
    std::size_t pos = 0;
    while (pos <= list.size()) {
        const std::size_t comma = list.find(',', pos);
        const std::size_t end = (comma == std::string_view::npos) ? list.size() : comma;
        const std::string_view tok = list.substr(pos, end - pos);
        const std::string norm = normalize(tok);

        bool matched = false;
        for (const Token& entry : kTable) {
            if (norm == entry.name) {
                mask |= entry.bit;
                matched = true;
                break;
            }
        }
        if (!matched && !norm.empty()) {
            LOG_WARN("parseVictoryTypeMask: unknown token '%s' (ignored)", norm.c_str());
        }
        if (comma == std::string_view::npos) { break; }
        pos = comma + 1;
    }
    return (mask == 0u) ? VICTORY_MASK_ALL : mask;
}

// ============================================================================
// Master per-turn update
// ============================================================================

void updateVictoryTrackers(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                           const EconomySimulation& economy, TurnNumber currentTurn,
                           const DiplomacyManager* diplomacy) {
    computeCSI(gameState, grid, economy, diplomacy);
    checkCollapseConditions(gameState, currentTurn);

    // Era evaluation every 30 turns
    if (currentTurn > 0 && (currentTurn % ERA_EVALUATION_INTERVAL) == 0) {
        performEraEvaluation(gameState);
    }
}

/**
 * @brief Backwards-compatible overload: minimal scoring without full CSI.
 *
 * Used when economy data is unavailable (e.g., early-game or test contexts).
 * Updates tech progress, culture accumulation, and a basic composite score.
 */
void updateVictoryTrackers(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid) {
    for (const std::unique_ptr<aoc::game::Player>& gsPlayer : gameState.players()) {
        VictoryTrackerComponent& tracker = gsPlayer->victoryTracker();

        // Techs: count completed bits
        {
            int32_t count = 0;
            for (std::size_t bit = 0; bit < gsPlayer->tech().completedTechs.size(); ++bit) {
                if (gsPlayer->tech().completedTechs[bit]) { ++count; }
            }
            tracker.scienceProgress = count;
        }

        // Culture
        tracker.totalCultureAccumulated +=
            computePlayerCulture(gameState, grid, gsPlayer->id());

        // Basic score: population + science + cities + culture
        int32_t pop = 0;
        for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
            pop += city->population();
        }
        const int32_t cityCount = gsPlayer->cityCount();
        tracker.score = pop * 5 + tracker.scienceProgress * 10 + cityCount * 20
                      + static_cast<int32_t>(tracker.totalCultureAccumulated);
    }
}

} // namespace aoc::sim
