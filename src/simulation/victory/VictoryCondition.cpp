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

        // Cities: population, happiness, loyalty, wonders
        for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
            ++s.cityCount;
            s.totalPopulation += city->population();
            s.avgHappiness += city->happiness().amenities - city->happiness().demand;
            s.avgLoyalty  += city->loyalty().loyalty;
            s.wonderCount += static_cast<int32_t>(city->wonders().wonders.size());
        }
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

        // 3. Conquest: lost capital + only one city remaining
        {
            bool hasCapital = false;
            const int32_t cities = gsPlayer->cityCount();
            for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
                if (city->isOriginalCapital()) {
                    hasCapital = true;
                    break;
                }
            }
            if (!hasCapital && cities <= 1) {
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

namespace {

/// Pairwise: do these two players share ANY active alliance type?
[[nodiscard]] bool isAllied(const PairwiseRelation& rel) {
    return rel.hasAnyAlliance();
}

/// Enumerate all maximal cliques of size >= 3 in a small alliance graph using
/// Bron-Kerbosch without pivot (player count <= ~16 makes this trivial).
/// Returns the clique with the highest combined prestige, or empty if no
/// 3+ clique exists.
struct BronKerboschCtx {
    const std::vector<std::vector<bool>>& adj;
    const std::vector<float>&              prestige;
    std::vector<PlayerId>                  current;
    std::vector<PlayerId>                  bestClique;
    float                                  bestPrestigeSum = 0.0f;
};

void bronKerbosch(BronKerboschCtx& ctx,
                  std::vector<PlayerId> R,
                  std::vector<PlayerId> P,
                  std::vector<PlayerId> X) {
    if (P.empty() && X.empty()) {
        if (R.size() >= 3) {
            float sum = 0.0f;
            for (PlayerId p : R) { sum += ctx.prestige[p]; }
            if (sum > ctx.bestPrestigeSum) {
                ctx.bestPrestigeSum = sum;
                ctx.bestClique      = R;
            }
        }
        return;
    }
    std::vector<PlayerId> Pcopy = P;
    for (PlayerId v : Pcopy) {
        std::vector<PlayerId> newR = R;
        newR.push_back(v);
        std::vector<PlayerId> newP;
        std::vector<PlayerId> newX;
        for (PlayerId u : P) { if (u != v && ctx.adj[v][u]) { newP.push_back(u); } }
        for (PlayerId u : X) { if (ctx.adj[v][u]) { newX.push_back(u); } }
        bronKerbosch(ctx, std::move(newR), std::move(newP), std::move(newX));
        P.erase(std::remove(P.begin(), P.end(), v), P.end());
        X.push_back(v);
    }
}

} // namespace

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

    // 3a. Domination Victory: own every other civ's original capital
    if ((enabledTypes & VICTORY_MASK_DOMINATION) != 0u) {
        for (const std::unique_ptr<aoc::game::Player>& candidate : gameState.players()) {
            if (candidate->victoryTracker().isEliminated) { continue; }
            bool ownsAllCapitals = true;
            for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
                if (other->id() == candidate->id()) { continue; }
                if (other->victoryTracker().isEliminated) { continue; }
                // Check if candidate owns the other's original capital
                bool foundCapital = false;
                for (const std::unique_ptr<aoc::game::City>& city : candidate->cities()) {
                    if (city->isOriginalCapital() && city->originalOwner() != candidate->id()) {
                        // This is a captured capital — check if it was from 'other'
                        if (city->originalOwner() == other->id()) {
                            foundCapital = true;
                            break;
                        }
                    }
                }
                if (!foundCapital) {
                    ownsAllCapitals = false;
                    break;
                }
            }
            if (ownsAllCapitals && alive > 1) {
                LOG_INFO("Player %u wins by DOMINATION (owns all original capitals)",
                         static_cast<unsigned>(candidate->id()));
                return {VictoryType::Domination, candidate->id()};
            }
        }
    }

    // 3b. Science Victory: completed all Space Race projects
    if ((enabledTypes & VICTORY_MASK_SCIENCE) != 0u) {
        for (const std::unique_ptr<aoc::game::Player>& candidate : gameState.players()) {
            if (candidate->victoryTracker().isEliminated) { continue; }
            if (candidate->spaceRace().allCompleted()) {
                LOG_INFO("Player %u wins by SCIENCE (all space projects completed)",
                         static_cast<unsigned>(candidate->id()));
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
                // Require >=75% of rivals dominated and at least one rival
                // (otherwise a solo survivor wins trivially by default).
                const float ratio = static_cast<float>(dominatedCount)
                                  / static_cast<float>(rivalCount);
                if (ratio >= 0.75f) {
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
        const float CULTURE_VICTORY_THRESHOLD = bal.cultureVictoryThreshold;
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
            if (clearLead) {
                LOG_INFO("Player %u wins by CULTURE (culture=%.0f wonders=%d)",
                         static_cast<unsigned>(leader),
                         static_cast<double>(bestCulture),
                         leaderWonders);
                return {VictoryType::Culture, leader};
            }
        }
    }

    // 3e. Confederation co-win: at turn limit, if a mutually-allied bloc of
    // 3+ non-eliminated civs has combined prestige >= 1.2x the best single
    // civ's prestige, all bloc members co-win. Rewards long-lasting diplomatic
    // blocs and gives a strategic counter to runaway single-civ winners.
    if ((enabledTypes & VICTORY_MASK_CONFEDERATION) != 0u
        && diplomacy != nullptr
        && currentTurn >= maxTurns
        && gameState.playerCount() >= 3) {
        const int32_t pc = gameState.playerCount();
        std::vector<float> prestige(static_cast<std::size_t>(pc), 0.0f);
        std::vector<bool>  aliveMask(static_cast<std::size_t>(pc), false);
        for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
            const auto idx = static_cast<std::size_t>(p->id());
            if (idx >= prestige.size()) { continue; }
            if (p->victoryTracker().isEliminated) { continue; }
            prestige[idx]  = p->prestige().total;
            aliveMask[idx] = true;
        }

        std::vector<std::vector<bool>> adj(
            static_cast<std::size_t>(pc), std::vector<bool>(static_cast<std::size_t>(pc), false));
        std::vector<PlayerId> candidates;
        for (int32_t i = 0; i < pc; ++i) {
            if (!aliveMask[static_cast<std::size_t>(i)]) { continue; }
            candidates.push_back(static_cast<PlayerId>(i));
            for (int32_t j = i + 1; j < pc; ++j) {
                if (!aliveMask[static_cast<std::size_t>(j)]) { continue; }
                const PairwiseRelation& rel = diplomacy->relation(
                    static_cast<PlayerId>(i), static_cast<PlayerId>(j));
                if (isAllied(rel)) {
                    adj[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = true;
                    adj[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] = true;
                }
            }
        }

        BronKerboschCtx ctx{adj, prestige, {}, {}, 0.0f};
        bronKerbosch(ctx, {}, candidates, {});

        if (ctx.bestClique.size() >= 3) {
            float bestSinglePrestige = 0.0f;
            for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
                if (p->victoryTracker().isEliminated) { continue; }
                if (p->prestige().total > bestSinglePrestige) {
                    bestSinglePrestige = p->prestige().total;
                }
            }
            if (ctx.bestPrestigeSum >= 1.2f * bestSinglePrestige
                && bestSinglePrestige > 0.0f) {
                PlayerId leader      = INVALID_PLAYER;
                float    leaderScore = -1.0f;
                for (PlayerId m : ctx.bestClique) {
                    const float s = prestige[static_cast<std::size_t>(m)];
                    if (s > leaderScore) { leaderScore = s; leader = m; }
                }
                VictoryResult result;
                result.type   = VictoryType::Confederation;
                result.winner = leader;
                for (PlayerId m : ctx.bestClique) {
                    if (m != leader) { result.coWinners.push_back(m); }
                }
                LOG_INFO("CONFEDERATION wins at turn %d: bloc size %zu, combined prestige %.1f "
                         "(vs best single %.1f). Leader = Player %u",
                         static_cast<int>(currentTurn),
                         ctx.bestClique.size(),
                         static_cast<double>(ctx.bestPrestigeSum),
                         static_cast<double>(bestSinglePrestige),
                         static_cast<unsigned>(leader));
                return result;
            }
        }
    }

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
        {"confederation", VICTORY_MASK_CONFEDERATION},
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
