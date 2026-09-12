/**
 * @file AIController.cpp
 * @brief Top-level AI orchestration and city/diplomacy/economy management.
 *
 * Delegates research, settlers, builders, and military to focused subsystem
 * controllers. Retains city production queue management, diplomacy, economy,
 * monetary system, and government logic.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/ai/AIConstants.hpp"
#include "aoc/simulation/ai/AIController.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/ai/AIAdvisors.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/core/DecisionLog.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/simulation/unit/CombatExtensions.hpp"
#include "aoc/simulation/diplomacy/EspionageSystem.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/ProductionSystem.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/economy/EnergyDependency.hpp"
#include "aoc/simulation/production/PowerGrid.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/TradeAgreement.hpp"
#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/city/CityActions.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/monetary/MonetaryActions.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/ai/AIEconomicStrategy.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/ai/UtilityScoring.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/greatpeople/GreatPeople.hpp"
#include "aoc/simulation/ai/UtilityAI.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/map/TerrainModification.hpp"
#include "aoc/map/FogOfWar.hpp"

#include <algorithm>
#include <limits>
#include <array>
#include <span>
#include <unordered_set>
#include "aoc/simulation/city/Governor.hpp"

namespace aoc::sim::ai {

/// How fast a district type's appeal falls off as an empire fills up with it.
/// One of a kind scores full; the tenth scores a quarter. Tuned to break the
/// Industrial/Encampment monopoly on specialty slots without inverting it.
constexpr float DISTRICT_SATURATION = 0.3f;

/// Settlers an empire may have in hand before it stops building them. Beyond
/// this the bottleneck is somewhere other than settler supply, and building
/// more only strands units on the map.
constexpr int32_t MAX_PENDING_SETTLERS = 2;

/// How many unspent great people an AI tolerates before it starts cashing the
/// useless ones in. Below this a person is simply waiting for its moment.
/// Measured over seeds 42-45: a limit of 3 left 59 people parked across the
/// four runs, a limit of 2 left 61 but freed enough gold to raise conquest
/// from 8 captures to 16. Most of that difference is one seed, so read it as
/// "no worse, sometimes much better" rather than a doubling.
constexpr std::size_t GP_HOARD_LIMIT = 2;

/// A utility at or below this will not recover by waiting, so the person is
/// worth more retired than held.
constexpr float GP_HOPELESS_UTILITY = 0.0f;


// ============================================================================
// Helper: Find the best military unit type ID the player can produce.
// ============================================================================

/**
 * @brief Analyze enemy military composition visible near our cities.
 *
 * Returns a per-UnitClass count of enemy units within 15 tiles of any
 * of our cities. The AI uses this to pick counter-units.
 */
struct EnemyComposition {
    int32_t counts[static_cast<std::size_t>(UnitClass::Count)] = {};
    int32_t total = 0;

    [[nodiscard]] float fraction(UnitClass cls) const {
        if (this->total == 0) { return 0.0f; }
        return static_cast<float>(this->counts[static_cast<std::size_t>(cls)])
             / static_cast<float>(this->total);
    }
};

static EnemyComposition analyzeEnemyComposition(const aoc::game::GameState& gameState,
                                                 PlayerId player) {
    EnemyComposition comp{};
    const aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return comp; }

    std::vector<aoc::hex::AxialCoord> ownCityLocs;
    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
        ownCityLocs.push_back(city->location());
    }

    for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
        if (other->id() == player) { continue; }
        // Eliminated civs leave their unit list pinned (cannot move or
        // fight) but it inflated threat counts forever and wasted per-turn
        // CPU. Skip them.
        if (other->victoryTracker().isEliminated) { continue; }
        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : other->units()) {
            const UnitTypeDef& def = unitTypeDef(unitPtr->typeId());
            if (!isMilitary(def.unitClass)) { continue; }
            for (const aoc::hex::AxialCoord& cityLoc : ownCityLocs) {
                if (aoc::hex::distance(unitPtr->position(), cityLoc) <= 15) {
                    comp.counts[static_cast<std::size_t>(def.unitClass)] += 1;
                    comp.total += 1;
                    break;
                }
            }
        }
    }
    return comp;
}

/**
 * @brief Score a unit type based on how well it counters the observed enemy composition.
 *
 * Uses the classMatchupModifier table: for each enemy class that has presence,
 * the candidate unit gets bonus score proportional to its matchup advantage.
 */
static float counterUnitScore(UnitClass candidateClass, const EnemyComposition& enemyComp) {
    if (enemyComp.total == 0) { return 1.0f; }

    float score = 0.0f;
    for (uint8_t c = 0; c < static_cast<uint8_t>(UnitClass::Count); ++c) {
        const UnitClass enemyClass = static_cast<UnitClass>(c);
        const float fraction = enemyComp.fraction(enemyClass);
        if (fraction <= 0.0f) { continue; }
        // matchup > 1.0 means we're strong against them
        const float matchup = classMatchupModifier(candidateClass, enemyClass);
        score += fraction * matchup;
    }
    return score;
}

/**
 * @brief Pick the best military unit to build, considering both raw strength
 *        and how well it counters nearby enemy units.
 *
 * Final score = (combatStrength + rangedStrength) * counterBonus.
 * This means a slightly weaker unit that hard-counters the enemy composition
 * can beat a stronger unit that has neutral matchups.
 */
// `stockpileScratch` is a caller-owned vector indexed by goodId. The function
// resizes/clears it and refills it from the player's cities. Indexing by
// goodId (instead of std::unordered_map) keeps iteration order deterministic
// across runs and hosts: libstdc++'s unordered_map rehash uses a per-process
// hash seed, so the previous map could yield different best-unit choices
// for the same simulation seed and break GA replay.
static UnitTypeId bestAvailableMilitaryUnit(const aoc::game::GameState& gameState,
                                            PlayerId player,
                                            std::vector<int32_t>& stockpileScratch) {
    const EnemyComposition enemyComp = analyzeEnemyComposition(gameState, player);

    // Aggregate player's city stockpiles so we can filter out units whose
    // resourceReqs we can't satisfy. Previously the AI enqueued Knights/Horsemen
    // the city lacked Horses/Iron for, production "completed" without output
    // (ProductionSystem drops stuck items silently), and the turn was wasted.
    const std::size_t numGoods = static_cast<std::size_t>(aoc::sim::goodCount());
    stockpileScratch.assign(numGoods, 0);
    const aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer != nullptr) {
        for (const std::unique_ptr<aoc::game::City>& cityPtr : gsPlayer->cities()) {
            for (const std::pair<const uint16_t, int32_t>& entry
                    : cityPtr->stockpile().goods) {
                if (entry.first < numGoods) {
                    stockpileScratch[entry.first] += entry.second;
                }
            }
        }
    }

    UnitTypeId bestId{0};
    float bestScore = 0.0f;

    for (const UnitTypeDef& def : UNIT_TYPE_DEFS) {
        if (!isMilitary(def.unitClass) || isNaval(def.unitClass)) {
            continue;
        }
        if (!canBuildUnit(gameState, player, def.id)) {
            continue;
        }

        bool haveResources = true;
        for (const UnitResourceReq& req : def.resourceReqs) {
            if (req.isValid()) {
                if (req.goodId >= numGoods
                    || stockpileScratch[req.goodId] < req.amount) {
                    haveResources = false;
                    break;
                }
            }
        }
        if (!haveResources) { continue; }

        const float rawStrength = static_cast<float>(def.combatStrength + def.rangedStrength);
        const float counterBonus = counterUnitScore(def.unitClass, enemyComp);
        const float score = rawStrength * counterBonus;
        if (score > bestScore) {
            bestScore = score;
            bestId = def.id;
        }
    }

    return bestId;
}

// ============================================================================
// Helper: Count units by class for a player.
// ============================================================================

struct UnitCounts {
    int32_t military  = 0;
    int32_t builders  = 0;
    int32_t settlers  = 0;
    int32_t scouts    = 0;
    int32_t traders   = 0;
    int32_t total     = 0;
};

static UnitCounts countPlayerUnits(const aoc::game::GameState& gameState, PlayerId player) {
    UnitCounts counts{};
    const aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return counts;
    }
    for (const std::unique_ptr<aoc::game::Unit>& u : gsPlayer->units()) {
        ++counts.total;
        const UnitTypeDef& def = unitTypeDef(u->typeId());
        if (isMilitary(def.unitClass)) {
            ++counts.military;
        }
        // Only actual Builders (UnitTypeId{5}). Diplomat/Spy/Medic share
        // UnitClass::Civilian but do not improve tiles.
        if (u->typeId() == UNIT_BUILDER) {
            ++counts.builders;
        }
        if (def.unitClass == UnitClass::Settler) {
            ++counts.settlers;
        }
        if (def.unitClass == UnitClass::Scout) {
            ++counts.scouts;
        }
        if (def.unitClass == UnitClass::Trader) {
            ++counts.traders;
        }
    }
    return counts;
}

// ============================================================================
// Constructor
// ============================================================================

AIController::AIController(PlayerId player, aoc::ui::AIDifficulty difficulty)
    : m_player(player)
    , m_difficulty(difficulty)
    , m_researchPlanner(player, difficulty)
    , m_settlerController(player, difficulty)
    , m_builderController(player, difficulty)
    , m_militaryController(player, difficulty)
{
}

// ============================================================================
// Main turn execution
// ============================================================================

void AIController::executeTurn(aoc::game::GameState& gameState,
                                aoc::map::HexGrid& grid,
                                const aoc::map::FogOfWar* fogOfWar,
                                DiplomacyManager& diplomacy,
                                const Market& market,
                                aoc::Random& rng) {
    GlobalDealTracker* dealTracker = &gameState.deals();
    // -----------------------------------------------------------------------
    // Advisor updates: run at varying frequencies to maintain a fresh view of
    // the game situation.  All results are posted to the player's AIBlackboard
    // so downstream scoring and posture evaluation can read them cheaply.
    // -----------------------------------------------------------------------
    aoc::game::Player* advisorPlayer = gameState.player(this->m_player);
    if (advisorPlayer != nullptr) {
        aoc::sim::ai::AIBlackboard& bb = advisorPlayer->blackboard();
        const int32_t currentTurn = gameState.currentTurn();
        const StrategicPosture prevPosture = bb.posture;

        // Military advisor runs every turn: threat changes fast.
        aoc::sim::ai::updateMilitaryAssessment(gameState, *advisorPlayer);

        if (currentTurn - bb.lastEconomyUpdate >= 5) {
            aoc::sim::ai::updateEconomyAssessment(*advisorPlayer);
            bb.lastEconomyUpdate = currentTurn;
        }
        if (currentTurn - bb.lastExpansionUpdate >= 10) {
            aoc::sim::ai::updateExpansionAssessment(gameState, *advisorPlayer, grid);
            bb.lastExpansionUpdate = currentTurn;
        }
        if (currentTurn - bb.lastResearchUpdate >= 10) {
            aoc::sim::ai::updateResearchAssessment(gameState, *advisorPlayer);
            bb.lastResearchUpdate = currentTurn;
        }
        if (currentTurn - bb.lastDiplomacyUpdate >= 20) {
            aoc::sim::ai::updateDiplomacyAssessment(gameState, *advisorPlayer, diplomacy);
            bb.lastDiplomacyUpdate = currentTurn;
        }

        aoc::sim::ai::evaluateStrategicPosture(*advisorPlayer);

        if (bb.posture != prevPosture) {
            static constexpr const char* POSTURE_NAMES[] = {
                "Expansion", "Development", "MilitaryBuildup",
                "Aggression", "Defense", "Economic"
            };
            static_assert(std::size(POSTURE_NAMES) ==
                              static_cast<std::size_t>(StrategicPosture::Count),
                          "POSTURE_NAMES must list every StrategicPosture");
            const char* postureName =
                POSTURE_NAMES[static_cast<std::size_t>(bb.posture)];
            LOG_INFO("AI %u Strategic posture changed to: %s",
                     static_cast<unsigned>(this->m_player), postureName);
        }
    }

    this->manageGovernment(gameState);
    this->m_researchPlanner.selectResearch(gameState);
    this->executeCityActions(gameState, grid, market);
    this->m_builderController.manageBuildersAndImprovements(gameState, grid);
    this->m_settlerController.executeSettlerActions(gameState, grid);
    this->m_militaryController.executeMilitaryActions(gameState, grid, rng, &diplomacy);
    this->manageMonetarySystem(gameState, grid, diplomacy);
    aoc::sim::aiEconomicStrategy(gameState, grid, market, diplomacy, this->m_player,
                                  static_cast<int32_t>(this->m_difficulty));
    this->executeDiplomacyActions(gameState, grid, diplomacy, market, rng, dealTracker);
    this->manageTradeRoutes(gameState, grid, market, diplomacy);
    this->considerPurchases(gameState);
    this->considerCanalBuilding(gameState, grid, fogOfWar);

    // --- AI Spy Management ---
    // Gene-driven mission selection: each mission's utility is a product of
    // leader focus genes and per-target context (threat, tech gap, wealth).
    // The top-scoring mission wins; espionagePriority globally scales offensive
    // vs defensive bias.
    {
        aoc::game::Player* spyPlayer = gameState.player(this->m_player);
        if (spyPlayer != nullptr) {
            const aoc::sim::ai::AIBlackboard& bb = spyPlayer->blackboard();
            const LeaderBehavior& bh =
                leaderPersonality(spyPlayer->civId()).behavior;

            for (const std::unique_ptr<aoc::game::Unit>& unitPtr : spyPlayer->units()) {
                SpyComponent& spy = unitPtr->spy();
                if (spy.owner == INVALID_PLAYER) { continue; }
                if (spy.turnsRemaining > 0) { continue; }

                // Pick the richest enemy as target — proxies wealth for
                // SiphonFunds and significance for all offensive ops.
                aoc::hex::AxialCoord targetLoc = spy.location;
                float bestEnemyWealth = 0.0f;
                bool foundTarget = false;
                for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
                    if (other->id() == this->m_player) { continue; }
                    if (other->cities().empty()) { continue; }
                    const float w = static_cast<float>(other->treasury());
                    if (!foundTarget || w > bestEnemyWealth) {
                        bestEnemyWealth = w;
                        for (const std::unique_ptr<aoc::game::City>& city : other->cities()) {
                            targetLoc = city->location();
                            break;
                        }
                        foundTarget = true;
                    }
                }
                if (!foundTarget) { continue; }

                const float techGap = std::max(0.0f, bb.techGap);
                const float threat  = std::max(0.0f, bb.threatLevel);
                const float wealthProxy = std::min(bestEnemyWealth / 1000.0f, 3.0f);
                // MarketManipulation utility scales with the live bubble
                // magnitude so the AI prefers it during inflation/euphoria
                // and ignores it on a flat market. Clamped to [1.0, 3.0]
                // so a 2.5x bubble never overwhelms the gene-weighted base.
                const float bubble = std::clamp(
                    spyPlayer->bubble().bubbleMagnitude, 1.0f, 3.0f);

                // Target-context multipliers
                PlayerId bestTargetId = INVALID_PLAYER;
                for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
                    if (other->id() == this->m_player) { continue; }
                    if (other->cities().empty()) { continue; }
                    if (!other->cities().empty()
                        && other->cities().front()->location() == targetLoc) {
                        bestTargetId = other->id();
                        break;
                    }
                }
                const bool atWarWithTarget = bestTargetId != INVALID_PLAYER
                    && diplomacy.isAtWar(this->m_player, bestTargetId);
                const float warBonus = atWarWithTarget ? 1.5f : 1.0f;
                // Deceit gate: low trustworthiness favors high-damage covert ops.
                const float deceit = std::max(0.1f, 2.0f - bh.trustworthiness);

                // Previously CurrencyCounterfeit dominated 84% of assignments
                // because its `deceit` multiplier (typ. 1.0-2.0) was stable
                // while the "context" multipliers on other missions (techGap,
                // threat, wealthProxy, bubble) collapsed to ~0 early-game,
                // starving their scores.  Rebalanced: Counterfeit base cut,
                // gap-dependent missions get a floor so they aren't starved,
                // and bubble is randomised slightly to surface market ops.
                const float gapFloor  = std::max(0.3f, techGap);
                const float threatFl  = std::max(0.3f, threat);
                const float wealthFl  = std::max(0.5f, wealthProxy);
                // Previously StealTechnology dominated 90% of assignments
                // because its base (100 * scienceFocus * gap) outsized every
                // other candidate.  Flattened the base weights + each slot
                // gets a small random perturbation so repeat picks don't
                // cluster.  Missions that actually benefit from context
                // (war, wealth, threat, deceit) still respond to it.
                struct Cand { SpyMission m; float s; };
                auto jitter = [&]() { return 1.0f + (rng.nextFloat() - 0.5f) * 0.30f; };
                const std::array<Cand, 11> cands = {{
                    {SpyMission::StealTechnology,
                        bh.scienceFocus * 70.0f * (0.5f + gapFloor) * jitter()},
                    {SpyMission::StealTradeSecrets,
                        bh.scienceFocus * 55.0f * (0.5f + gapFloor) * jitter()},
                    {SpyMission::SabotageProduction,
                        bh.militaryAggression * 80.0f * (0.5f + threatFl) * warBonus * jitter()},
                    {SpyMission::SupplyChainDisrupt,
                        bh.militaryAggression * 75.0f * warBonus * deceit * jitter()},
                    {SpyMission::SiphonFunds,
                        bh.economicFocus * 80.0f * wealthFl * jitter()},
                    {SpyMission::CurrencyCounterfeit,
                        bh.economicFocus * 45.0f * deceit * jitter()},
                    {SpyMission::MarketManipulation,
                        bh.speculationAppetite * 70.0f * bubble * jitter()},
                    {SpyMission::InsiderTrading,
                        bh.speculationAppetite * 55.0f * jitter()},
                    {SpyMission::CounterIntelligence,
                        (2.0f - bh.espionagePriority) * 65.0f * (0.5f + threatFl) * jitter()},
                    {SpyMission::FomentUnrest,
                        bh.militaryAggression * 70.0f * (0.5f + threatFl) * deceit * jitter()},
                    {SpyMission::MonitorTreasury,
                        bh.economicFocus * 45.0f * jitter()},
                }};

                SpyMission mission = SpyMission::GatherIntelligence;
                float bestScore = 50.0f; // GatherIntelligence baseline
                for (const Cand& c : cands) {
                    const float s = c.s * bh.espionagePriority;
                    if (s > bestScore) { bestScore = s; mission = c.m; }
                }

                spy.location = targetLoc;
                [[maybe_unused]] ErrorCode spyResult =
                    assignSpyMission(gameState, *unitPtr, mission);
            }
        }
    }

    // --- AI Nuclear Strike Decision ---
    // Gated on: (a) Nuclear Fission researched (TechId 17), (b) currently at
    // war with someone, (c) nukeWillingness * riskTolerance passes roll.
    // One strike attempt per player per turn. Arms the nearest military
    // unit with a warhead and targets the weakest enemy city.
    //
    // Launch multiplier cut 0.20 → 0.04 because once Nuclear Fission
    // diffused across players, strikes rose 2 → 16 per 30 player-games
    // purely from availability. The tech-gate alone wasn't enough restraint
    // once AIResearchPlanner started delivering Nuclear Fission reliably.
    {
        aoc::game::Player* nukePlayer = gameState.player(this->m_player);
        // A7 Manhattan Project (WonderId 11) is the nuke-unlock gate on top
        // of Nuclear Fission (TechId 17): tech alone enables reactors, but
        // weaponization requires the wonder.
        bool hasManhattan = false;
        if (nukePlayer != nullptr) {
            for (const std::unique_ptr<aoc::game::City>& c : nukePlayer->cities()) {
                if (c->wonders().hasWonder(static_cast<aoc::sim::WonderId>(11))) {
                    hasManhattan = true;
                    break;
                }
            }
        }
        if (nukePlayer != nullptr && nukePlayer->tech().hasResearched(TechId{17})
            && hasManhattan) {
            bool atWar = false;
            PlayerId enemyId = INVALID_PLAYER;
            for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
                if (other->id() == this->m_player) { continue; }
                if (diplomacy.isAtWar(this->m_player, other->id())) {
                    atWar = true;
                    enemyId = other->id();
                    break;
                }
            }
            if (atWar && enemyId != INVALID_PLAYER) {
                const LeaderBehavior& bh =
                    leaderPersonality(nukePlayer->civId()).behavior;
                const float launchScore = bh.nukeWillingness * bh.riskTolerance;
                const float roll = rng.nextFloat(0.0f, 1.0f);

                if (roll < launchScore * 0.015f) {  // was 0.04 — audit showed 1.7 strikes/sim, too spammy
                    aoc::game::Player* enemy = gameState.player(enemyId);
                    aoc::hex::AxialCoord targetLoc{};
                    int32_t weakestPop = std::numeric_limits<int32_t>::max();
                    bool haveTarget = false;
                    if (enemy != nullptr) {
                        for (const std::unique_ptr<aoc::game::City>& c : enemy->cities()) {
                            if (c->population() < weakestPop) {
                                weakestPop = c->population();
                                targetLoc = c->location();
                                haveTarget = true;
                            }
                        }
                    }
                    if (haveTarget) {
                        // requestNuclearStrike owns every gate (tech, Manhattan
                        // Project, Uranium, a unit built to carry a warhead) and
                        // arms the carrier itself. It fails before any blast, so
                        // there is no unit pointer to keep alive across the call
                        // (the old code armed a unit here and had to be careful
                        // not to touch it afterwards).
                        LOG_INFO("AI %u NUCLEAR STRIKE decision: target p%u at (%d,%d) "
                                 "score=%.2f roll=%.2f",
                                 static_cast<unsigned>(this->m_player),
                                 static_cast<unsigned>(enemyId),
                                 targetLoc.q, targetLoc.r,
                                 static_cast<double>(launchScore),
                                 static_cast<double>(roll));
                        const ErrorCode nec = requestNuclearStrike(
                            gameState, grid, this->m_player, targetLoc,
                            NukeType::NuclearDevice);
                        if (nec != ErrorCode::Ok) {
                            LOG_WARN("AI %u NUCLEAR STRIKE failed (err %d)",
                                     static_cast<unsigned>(this->m_player),
                                     static_cast<int>(nec));
                        }
                    }
                }
            }
        }
    }

    this->manageGreatPeople(gameState, grid, diplomacy);

    refreshMovement(gameState, this->m_player);
}

// ============================================================================
// manageGreatPeople: gene-weighted activation timing
//
// Each recruited Great Person is a one-use unit. The AI activates it when the
// current situation makes its ability especially valuable, weighted by the
// leader's focus genes:
//
//   Scientist  → active immediately if scienceFocus>=1, else waits for a costly tech
//   Engineer   → active when any city has a slow production item (>100 remaining)
//   General    → active when at war AND there is a friendly unit nearby to heal
//   Artist     → active when unowned land is reachable within 2 hexes of the GP
//   Merchant   → active immediately if economicFocus>=1, else holds until deficit
//
// greatPersonFocus globally scales eagerness: >=1.4 activates on any non-zero
// utility, <0.7 requires strong situational justification.
// ============================================================================

void AIController::manageGreatPeople(aoc::game::GameState& gameState,
                                     aoc::map::HexGrid& grid,
                                     const DiplomacyManager& diplomacy) {
    aoc::game::Player* player = gameState.player(this->m_player);
    if (player == nullptr) { return; }

    const LeaderBehavior& bh = leaderPersonality(player->civId()).behavior;
    const float focusBias    = bh.greatPersonFocus;
    const float eagerGate    = focusBias >= 1.4f ? 0.0f
                                                 : (focusBias < 0.7f ? 2.0f : 1.0f);

    bool atWar = false;
    for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
        if (other->id() == this->m_player) { continue; }
        if (diplomacy.isAtWar(this->m_player, other->id())) {
            atWar = true;
            break;
        }
    }

    // Snapshot GP unit pointers before activation (activateGreatPerson calls
    // removeUnit, invalidating iterators on player->units()).
    std::vector<aoc::game::Unit*> gpUnits;
    gpUnits.reserve(4);
    for (const std::unique_ptr<aoc::game::Unit>& u : player->units()) {
        if (u->typeId() == UnitTypeId{102} && !u->greatPerson().isActivated) {
            gpUnits.push_back(u.get());
        }
    }

    for (aoc::game::Unit* gp : gpUnits) {
        const GreatPersonComponent& comp = gp->greatPerson();
        if (comp.defId >= GREAT_PERSON_COUNT) { continue; }
        const GreatPersonType type = allGreatPersonDefs()[comp.defId].type;

        float utility = 0.0f;
        switch (type) {
            case GreatPersonType::Scientist: {
                const PlayerTechComponent& tech = player->tech();
                const float progressRatio = researchFraction(tech);
                // High score when we are deep into a long tech: a 50% jump is a big gift.
                utility = bh.scienceFocus * (0.5f + progressRatio);
                break;
            }
            case GreatPersonType::Engineer: {
                float slowest = 0.0f;
                for (const std::unique_ptr<aoc::game::City>& c : player->cities()) {
                    if (c->production().isEmpty()) { continue; }
                    const float remaining = c->production().queue.front().totalCost
                                          - c->production().queue.front().progress;
                    if (remaining > slowest) { slowest = remaining; }
                }
                utility = bh.prodBuildings * (slowest / 200.0f);
                break;
            }
            case GreatPersonType::General: {
                if (!atWar) { break; }
                int32_t hurtNearby = 0;
                for (const std::unique_ptr<aoc::game::Unit>& u : player->units()) {
                    if (u.get() == gp) { continue; }
                    if (u->isDead()) { continue; }
                    if (u->hitPoints() >= u->typeDef().maxHitPoints) { continue; }
                    if (grid.distance(u->position(), gp->position()) <= 2) {
                        ++hurtNearby;
                    }
                }
                utility = bh.militaryAggression * static_cast<float>(hurtNearby) * 0.5f;
                break;
            }
            case GreatPersonType::Admiral: {
                // Same shape as the General, but only damaged ships count.
                if (!atWar) { break; }
                int32_t hurtShips = 0;
                for (const std::unique_ptr<aoc::game::Unit>& u : player->units()) {
                    if (u.get() == gp || u->isDead()) { continue; }
                    if (u->typeDef().unitClass != aoc::sim::UnitClass::Naval) { continue; }
                    if (u->hitPoints() >= u->typeDef().maxHitPoints) { continue; }
                    if (grid.distance(u->position(), gp->position()) <= aoc::sim::GP_AURA_RADIUS) {
                        ++hurtShips;
                    }
                }
                utility = bh.militaryAggression * static_cast<float>(hurtShips) * 0.5f;
                break;
            }
            case GreatPersonType::Artist: {
                std::vector<aoc::hex::AxialCoord> tiles;
                tiles.reserve(19);
                aoc::hex::spiral(gp->position(), 2, std::back_inserter(tiles));
                int32_t claimable = 0;
                for (const aoc::hex::AxialCoord& t : tiles) {
                    if (!grid.isValid(t)) { continue; }
                    if (grid.owner(grid.toIndex(t)) == INVALID_PLAYER) { ++claimable; }
                }
                utility = bh.cultureFocus * static_cast<float>(claimable) * 0.2f;
                break;
            }
            case GreatPersonType::Prophet: {
                // Worth most to a civ that has no religion of its own yet.
                const bool godless =
                    player->faith().foundedReligion == aoc::sim::NO_RELIGION;
                utility = bh.religiousZeal * (godless ? 1.5f : 0.4f);
                break;
            }
            case GreatPersonType::Writer:
            case GreatPersonType::Musician: {
                // Worth most when there is somewhere to put the work.
                int32_t freeSlots = 0;
                for (const std::unique_ptr<aoc::game::City>& c : player->cities()) {
                    if (c == nullptr) { continue; }
                    freeSlots += aoc::sim::freeGreatWorkSlots(*c);
                }
                utility = bh.cultureFocus * (freeSlots > 0 ? 1.2f : 0.3f);
                break;
            }
            case GreatPersonType::Merchant: {
                // Stronger utility when treasury is tight.
                const float treasury  = static_cast<float>(player->treasury());
                const float stress    = std::max(0.0f, 1.0f - treasury / 400.0f);
                utility = bh.economicFocus * (0.7f + stress);
                break;
            }
            default:
                break;
        }

        utility *= focusBias;

        if (utility > eagerGate) {
            LOG_INFO("AI %u Activating Great Person defId=%u type=%u utility=%.2f",
                     static_cast<unsigned>(this->m_player),
                     static_cast<unsigned>(comp.defId),
                     static_cast<unsigned>(type),
                     static_cast<double>(utility));
            activateGreatPerson(gameState, grid, *gp);
            continue;
        }

        // Nothing here scored, and nothing about this person's score will
        // change on its own: an Admiral wants damaged ships in a war it is not
        // fighting, a Prophet wants a religion the civ already founded. Held
        // forever they were dead weight -- measured over seeds 42-45, a quarter
        // to two fifths of every great person recruited was never used at all,
        // and Admirals were never used once. A person this far below the gate,
        // in a queue that is already backing up, takes the payout instead.
        if (gpUnits.size() >= GP_HOARD_LIMIT && utility <= GP_HOPELESS_UTILITY) {
            const aoc::hex::AxialCoord where = gp->position();
            if (requestRetireGreatPerson(gameState, this->m_player, where) == ErrorCode::Ok) {
                LOG_INFO("AI %u retired Great Person defId=%u type=%u (utility %.2f)",
                         static_cast<unsigned>(this->m_player),
                         static_cast<unsigned>(comp.defId), static_cast<unsigned>(type),
                         static_cast<double>(utility));
            }
        }
    }
}

// ============================================================================
// City actions: utility-scored production queue management
//
// Each city with an empty production queue scores ALL buildable options
// through UtilityAI considerations and picks the highest-scoring one.
// Scores are products of per-consideration [0,1] values, so a consideration
// of 0 fully disqualifies an option. Personality weights act as multipliers
// on category base scores. A 10% randomization band prevents every AI from
// picking identically when scores are near-equal.
// ============================================================================

// -------------------------------------------------------------------------
// `ProductionCandidate` is defined in AIController.hpp so the per-AIController
// scratch buffer (`m_candidatesScratch`) can hold it directly. Keeping it in
// the header is the cheapest way to avoid the per-city allocation flagged by
// the 2026-05-10 audit.
// -------------------------------------------------------------------------
// Internal: count unimproved tiles owned by the player near a city
// -------------------------------------------------------------------------

static int32_t countUnimprovedOwnedTiles(const aoc::map::HexGrid& grid,
                                          PlayerId player,
                                          const aoc::hex::AxialCoord& cityLoc) {
    int32_t unimproved = 0;
    const std::array<aoc::hex::AxialCoord, 6> neighbors = aoc::hex::neighbors(cityLoc);
    for (const aoc::hex::AxialCoord& nbr : neighbors) {
        if (!grid.isValid(nbr)) { continue; }
        const int32_t idx = grid.toIndex(nbr);
        if (grid.owner(idx) == player &&
            grid.improvement(idx) == aoc::map::ImprovementType::None &&
            grid.movementCost(idx) > 0) {
            ++unimproved;
        }
    }
    return unimproved;
}

// -------------------------------------------------------------------------
// Internal: check whether any enemy unit is within a given hex radius
// -------------------------------------------------------------------------

static bool enemyWithinRadius(const std::vector<aoc::hex::AxialCoord>& enemyPositions,
                               const aoc::hex::AxialCoord& origin,
                               int32_t radius) {
    for (const aoc::hex::AxialCoord& pos : enemyPositions) {
        if (aoc::hex::distance(pos, origin) <= radius) {
            return true;
        }
    }
    return false;
}

// -------------------------------------------------------------------------
// Internal: score a settler candidate for this city using utility curves
// -------------------------------------------------------------------------

static float scoreSettler(const LeaderBehavior& behavior,
                           int32_t ownedCities,
                           int32_t targetCities,
                           int32_t cityPop,
                           int32_t settlePopThreshold,
                           int32_t settlerCount,
                           int32_t militaryUnits,
                           float   treasury,
                           float   expansionOpportunity,
                           bool    expansionExhausted) {
    // Map is saturated: no viable city sites within scan radius.  Producing
    // another settler would strand it or force a disband.  Hard-zero the score
    // so the city picks military/infrastructure instead.
    if (expansionExhausted) { return 0.0f; }

    // A settler that cannot found anything sits on the map forever, and the AI
    // went on producing more. Measured on seed 20260906 at 8 players: one AI
    // reached 27 live settlers, every one of them pathing to the SAME tile,
    // and the run grew without bound until it died of bad_alloc at 24 GB. The
    // `noSettlerScore` damping below is a multiplier, not a stop, so it slowed
    // the pile-up and never prevented it. Past a couple of settlers in hand the
    // problem is not that we want another one; it is that the ones we have
    // cannot land.
    if (settlerCount >= MAX_PENDING_SETTLERS) { return 0.0f; }
    // expansion_need: desire falls from 1.0 (no cities) to 0.0 (at target)
    const aoc::sim::ai::UtilityConsideration expansionNeed{
        0.0f, static_cast<float>(targetCities),
        aoc::sim::ai::UtilityCurve::inverse()
    };
    // pop_ready: a city at the leader's threshold is ready, below it the
    // settler is still worth something but scores lower. The threshold comes
    // from expansionism through computeScaledTargets: an expansionist settles
    // at pop 1, a cautious leader waits for pop 2. It was computed and then
    // dropped on the floor here until 2026-09-06, so every leader waited for
    // pop 2 no matter how expansionist it was written to be.
    const float popScore = (cityPop >= settlePopThreshold) ? 1.0f : 0.4f;

    // no_settler_exists: strongly avoid a second queued settler
    const float noSettlerScore = (settlerCount == 0) ? 1.0f : 0.1f;

    // safety: soft penalty when undefended -- floor at 0.6 so lack of military
    // never fully blocks expansion.  A single-city civ with no units must still
    // expand or it falls too far behind.
    const float safetyScore = (militaryUnits >= ownedCities) ? 1.0f : 0.6f;

    // treasury_ok: clamp the minimum to 0.35 so an empty treasury does not
    // zero-out the entire settler score.  A poor empire still needs cities.
    const aoc::sim::ai::UtilityConsideration treasuryOk{
        0.0f, 200.0f,
        aoc::sim::ai::UtilityCurve::linear(1.0f, 0.0f)
    };
    const float treasuryScore = std::max(0.35f, treasuryOk.score(treasury));

    // Continuous expansion multiplier: scales from 4.0x at 0 cities down to
    // 1.0x at targetCities.  This keeps settler production as the dominant
    // priority until the empire reaches its target size, instead of the old
    // binary singleCityBoost that dropped to 1.0x at 2 cities.
    // Formula: max(1.0, (targetCities - ownedCities) / 2.0 + 1.0)
    // Examples (target=8): 0 cities->5.0, 1->4.5, 2->4.0, 4->3.0, 6->2.0, 8->1.0
    const float expansionBoost = std::max(
        1.0f,
        (static_cast<float>(targetCities - ownedCities) / 2.0f) + 1.0f);

    constexpr float BASE_WEIGHT = 0.95f;

    // expansionOpportunity [0,1] from the expansion advisor amplifies the score
    // when the blackboard confirms good founding sites exist.
    const float opportunityBoost = 1.0f + expansionOpportunity;

    return BASE_WEIGHT
           * behavior.prodSettlers
           * expansionNeed.score(static_cast<float>(ownedCities))
           * popScore
           * noSettlerScore
           * safetyScore
           * treasuryScore
           * expansionBoost
           * opportunityBoost;
}

// -------------------------------------------------------------------------
// Internal: score a military unit candidate using utility curves
// -------------------------------------------------------------------------

static float scoreMilitary(const LeaderBehavior& behavior,
                            int32_t militaryUnits,
                            int32_t ownedCities,
                            bool    enemyNearby,
                            float   treasury,
                            float   threatLevel) {
    // Parametric-policy formula. Shape designer-authored; weights GA-tuned
    // per leader via milBaseWeight / milThreatSensitivity / milEmergencySlope
    // / milOverstockPenalty genes (defaults reproduce pre-formula behavior).
    const float overstock = std::max(0.25f, behavior.milOverstockPenalty);
    const float desiredPerCityF = std::max(1.0f, 2.0f / overstock);
    const int32_t desiredTotal = std::max(
        1, static_cast<int32_t>(static_cast<float>(ownedCities) * desiredPerCityF));

    const aoc::sim::ai::UtilityConsideration militaryNeed{
        0.0f, static_cast<float>(desiredTotal),
        aoc::sim::ai::UtilityCurve::inverse()
    };

    const float threatScore = enemyNearby ? 1.0f : 0.5f;

    const float perCityRatio = static_cast<float>(militaryUnits)
                             / static_cast<float>(std::max(1, ownedCities));
    const float emergencyMultiplier = std::max(
        1.0f, 2.2f - behavior.milEmergencySlope * perCityRatio);

    const float threatLevelBoost = 1.0f + behavior.milThreatSensitivity * threatLevel;

    (void)treasury;  // production paid in hammers, not gold.
    // WP-D3: warmonger pivot. Leaders with militaryAggression >= 1.5 get a
    // sharp boost to military score so the production queue leans to military
    // units. This is the "pump out tanks" mode for civs like Tlatoani /
    // Genghis. Without this they still build wonders/buildings even when
    // supposedly all-in on conquest.
    //
    // The boost was `2.0f * militaryAggression`, applied ON TOP OF the
    // `* militaryAggression` term in the return below, so aggression entered
    // this score QUADRATICALLY: at 1.90 it contributed 7.2x rather than 1.9x.
    // Measured consequence on seed 42, where the deciding product at two cities
    // came out military 21.66 vs settler 3.85 for the Zulu and 15.61 vs 3.42
    // for the Mapuche, against 1.22 vs 7.98 (Phoenicia) and 0.63 vs 8.55
    // (Egypt): the two aggressive civs produced 73 military units between them
    // and NOT ONE SETTLER in 500 turns, sat on two cities each while their
    // neighbours reached eleven and twelve, and finished on a seventeenth of
    // the winner's GDP. A leader that never expands is not playing
    // aggressively, it is not playing.
    //
    // A flat step keeps the pivot the work package asked for -- aggressive
    // leaders still lean military, and still outweigh a builder -- without
    // letting one gene enter the same product twice.
    constexpr float WARMONGER_PIVOT = 1.5f;
    constexpr float WARMONGER_BOOST = 2.0f;
    const float warmongerBoost =
        (behavior.militaryAggression >= WARMONGER_PIVOT) ? WARMONGER_BOOST : 1.0f;
    return behavior.milBaseWeight
           * behavior.prodMilitary
           * behavior.militaryAggression
           * warmongerBoost
           * militaryNeed.score(static_cast<float>(militaryUnits))
           * threatScore
           * emergencyMultiplier
           * threatLevelBoost;
}

// -------------------------------------------------------------------------
// Internal: score a builder candidate using utility curves
// -------------------------------------------------------------------------

static float scoreBuilder(const LeaderBehavior& behavior,
                           int32_t unimprovedTiles,
                           int32_t workedTiles,
                           int32_t builderCount,
                           int32_t ownedCities) {
    // tiles_need: fraction of worked tiles that are unimproved
    const aoc::sim::ai::UtilityConsideration tilesNeed{
        0.0f, static_cast<float>(std::max(1, workedTiles)),
        aoc::sim::ai::UtilityCurve::linear(1.0f, 0.0f)
    };

    // Each builder has 3 charges (Civ 6-style). Want roughly 1 builder per
    // city so the empire can keep up with territorial growth. Scale from
    // 2.0x when empire has no builders down to 1.0x once builderCount >=
    // cities. Ceiling capped at 2.0x -- 3.0x produced ~200 builders per game.
    const float perCityRatio = static_cast<float>(builderCount)
                             / static_cast<float>(std::max(1, ownedCities));
    const float undersupplyMultiplier = std::max(1.0f, 2.0f - perCityRatio);

    // 2.0 base. Higher caused builder spam; lower left them losing to
    // settlers + districts across all seeds.
    constexpr float BASE_WEIGHT = 2.0f;

    return BASE_WEIGHT
           * behavior.prodBuilders
           * tilesNeed.score(static_cast<float>(unimprovedTiles))
           * undersupplyMultiplier;
}

// -------------------------------------------------------------------------
// Internal: score a trader candidate using utility curves
// -------------------------------------------------------------------------

/// Utility of a civ's first Trader while it has none (see the Trader
/// candidate): above routine military, below the defenceless floor.
constexpr float FIRST_TRADER_FLOOR = 20.0f;

/// The capital for production purposes: the first city the player still owns.
[[nodiscard]] static const aoc::game::City* firstOwnedCity(const aoc::game::Player& player) {
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (city != nullptr && city->owner() == player.id()) {
            return city.get();
        }
    }
    return nullptr;
}

static float scoreTrader(const LeaderBehavior& behavior,
                          bool    hasForeignTrade,
                          int32_t traderCount,
                          int32_t cityCount,
                          int32_t tradeSlots) {
    // has_trade_civic: hard prerequisite -- score is zero without it
    if (!hasForeignTrade) { return 0.0f; }

    // trade_need: want Traders up to the route slots the civ actually has,
    // and never more than cities + 2. A flat cap of four left slots idle
    // for every civ past its fourth Market; two are always allowed so a
    // single-city empire still forms routes and tech diffusion has volume.
    // Zero past the cap: a residual 0.1 kept the Trader a candidate in every
    // city, and once tech gates locked the other candidates it won by default
    // (2072 Traders in one seed-42 run, 2026-09-05).
    const int32_t maxTraders = std::max(2, std::min(tradeSlots, cityCount + 2));
    if (traderCount >= maxTraders) { return 0.0f; }

    // Every civ trades; focus sets how eagerly the slots fill, not whether.
    // The first two Traders are a civ's first commercial links (gold, goods,
    // the luxuries it lacks, diffusion) and rank with a Builder; the rest
    // fill slots at the old weight, which 1.2 showed floods the map with
    // Traders. At 0.8 x focus alone the Trader lost to every building for
    // a low-focus leader, so on seeds 42 and 43 no civ ran a route before
    // turn 80 and two of four never did (measurement gate, 2026-09-11).
    constexpr float FIRST_LINKS_WEIGHT = 2.0f;
    constexpr float FILL_WEIGHT        = 0.8f;
    const float eagerness = 0.5f + 0.5f * behavior.economicFocus;
    return (traderCount < 2 ? FIRST_LINKS_WEIGHT : FILL_WEIGHT) * eagerness;
}

// -------------------------------------------------------------------------
// Internal: can this city actually pay a building's construction goods?
//
// A3 (2026-09-03): the AI used to score buildings purely on utility. The only
// two reads of BuildingDef::resourceCosts were at completion time in
// ProductionSystem, so a city would happily queue a building whose goods it
// could never obtain and sit on it forever -- the Mint (1 Stone, quarried by
// nobody) wedged every capital for ~93% of a 60-turn game. Gate selection on
// the same costs completion checks.
// -------------------------------------------------------------------------

static bool cityCanAffordBuildingGoods(const aoc::game::City& city,
                                        const BuildingDef& bdef) {
    if (!bdef.hasResourceCost()) { return true; }
    const CityStockpileComponent& stockpile = city.stockpile();
    for (const BuildingResourceCost& cost : bdef.resourceCosts) {
        if (cost.isValid() && stockpile.getAmount(cost.goodId) < cost.amount) {
            return false;
        }
    }
    return true;
}

// -------------------------------------------------------------------------
// Internal: score a building candidate using the existing utility scorer
// with the UtilityAI base-weight pattern applied
// -------------------------------------------------------------------------

static float scoreBuildingCandidate(const LeaderBehavior& behavior,
                                     BuildingId buildingId,
                                     const aoc::sim::AIContext& aiCtx,
                                     float techGap) {
    // Delegate to the specialized building scorer in UtilityScoring, then
    // apply the building production weight and scale to a [0,1]-ish range.
    const float rawScore = scoreBuildingForLeader(behavior, buildingId, aiCtx);

    // Raw scores are in the 40-200 range. Normalize against a 200-point ceiling
    // so the building score participates in the same 0-1 product as other candidates.
    // BASE_WEIGHT 2.2: generic industrial/commercial buildings (Market=90, Bank=110,
    // Forge=200*techIndustrial) outcompete settlers (~2.14) once districts are built.
    // Without this, AI built only Mint + tech-gap-boosted Library/University across
    // 600-turn 8-player games.
    constexpr float BUILDING_SCORE_MAX = 200.0f;
    constexpr float BASE_WEIGHT        = 2.2f;

    float score = BASE_WEIGHT
                  * behavior.prodBuildings
                  * aoc::sim::ai::normalizeValue(rawScore, 0.0f, BUILDING_SCORE_MAX);

    // Science buildings (Library=7, University=19, Research Lab=12): boost when
    // the research advisor signals this player is falling behind the tech average.
    const uint16_t bid = buildingId.value;
    if (bid == 7u || bid == 19u || bid == 12u) {
        score *= (1.0f + techGap);
    }

    return score;
}

// -------------------------------------------------------------------------
// Main city action function
// -------------------------------------------------------------------------

void AIController::executeCityActions(aoc::game::GameState& gameState,
                                       aoc::map::HexGrid& grid,
                                       const Market& /*market*/) {
    aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    if (gsPlayer == nullptr) {
        return;
    }

    // Snapshot blackboard values used throughout this function so the compiler
    // can keep them in registers and we avoid repeated pointer indirections.
    const aoc::sim::ai::AIBlackboard& bb    = gsPlayer->blackboard();
    const StrategicPosture currentPosture   = bb.posture;
    const float bbExpansionOpportunity      = bb.expansionOpportunity;
    const float bbThreatLevel               = bb.threatLevel;
    const float bbTechGap                   = bb.techGap;

    const UnitCounts unitCounts = countPlayerUnits(gameState, this->m_player);
    const int32_t ownedCityCount = gsPlayer->ownedCityCount();

    const aoc::sim::CivId myCivId = gsPlayer->civId();
    const LeaderPersonalityDef& personality = leaderPersonality(myCivId);
    const AIScaledTargets targets = computeScaledTargets(personality.behavior);

    // Traders require Foreign Trade civic (CivicId{2})
    const bool hasForeignTrade = gsPlayer->civics().hasCompleted(CivicId{2});
    const bool playerHasCoins  = gsPlayer->monetary().totalCoinCount() > 0;

    const UnitTypeId bestMilitaryId  = bestAvailableMilitaryUnit(
        gameState, this->m_player, this->m_stockpileByGoodScratch);
    const UnitTypeDef& bestMilitaryDef = unitTypeDef(bestMilitaryId);

    const float treasuryFloat = static_cast<float>(gsPlayer->treasury());

    // Pre-collect enemy military positions once so per-city threat checks are O(E) not O(C*E)
    std::vector<aoc::hex::AxialCoord> enemyMilitaryPositions;
    for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
        if (other->id() == this->m_player) { continue; }
        for (const std::unique_ptr<aoc::game::Unit>& u : other->units()) {
            if (isMilitary(unitTypeDef(u->typeId()).unitClass)) {
                enemyMilitaryPositions.push_back(u->position());
            }
        }
    }

    // Governor titles (Governor.hpp): seat and promote before the city decisions so
    // the multipliers apply to this turn's production choices.
    aoc::sim::aiSpendGovernorTitles(gameState, this->m_player);

    int32_t cityIndex = 0;
    for (const std::unique_ptr<aoc::game::City>& cityPtr : gsPlayer->cities()) {
        aoc::game::City& city = *cityPtr;

        aoc::sim::ProductionQueueComponent& queue = city.production();
        if (!queue.isEmpty()) {
            ++cityIndex;
            continue;
        }

        // Easy AI: 30% chance to skip city action entirely
        if (this->m_difficulty == aoc::ui::AIDifficulty::Easy) {
            const uint32_t pseudoRand =
                (static_cast<uint32_t>(cityIndex) * 7919u + 31u) % 100u;
            if (pseudoRand < 30u) {
                ++cityIndex;
                continue;
            }
        }

        // ----------------------------------------------------------------
        // Gather city-local context
        // ----------------------------------------------------------------

        const aoc::sim::CityDistrictsComponent& districts = city.districts();
        const bool enemyNearby = enemyWithinRadius(enemyMilitaryPositions, city.location(), 10);

        // Count unimproved tiles in the city's first ring for builder scoring
        const int32_t unimprovedTiles =
            countUnimprovedOwnedTiles(grid, this->m_player, city.location());
        // workedTiles approximation: 6 ring-1 tiles are the normal work radius
        constexpr int32_t RING1_TILES = 6;

        // ----------------------------------------------------------------
        // Build the candidate list and score each option. Reuse the
        // per-AIController scratch vector — clear() preserves capacity, so
        // the per-city ~32-entry allocation flagged in the 2026-05-10 audit
        // collapses into one growth episode at startup.
        // ----------------------------------------------------------------

        std::vector<ProductionCandidate>& candidates = this->m_candidatesScratch;
        candidates.clear();
        if (candidates.capacity() < 32) { candidates.reserve(32); }

        // --- Settler ---
        if (ownedCityCount < targets.maxCities) {
            const float settlerScore = scoreSettler(
                personality.behavior,
                ownedCityCount,
                targets.maxCities,
                city.population(),
                targets.settlePopThreshold,
                unitCounts.settlers,
                unitCounts.military,
                treasuryFloat,
                bbExpansionOpportunity,
                bb.expansionExhausted
            );
            if (settlerScore > 0.0f) {
                ProductionCandidate candidate{};
                candidate.item.type      = ProductionItemType::Unit;
                candidate.item.itemId    = 3u;
                candidate.nameView       = "Settler";
                candidate.item.totalCost = static_cast<float>(
                    unitTypeDef(UNIT_SETTLER).productionCost);
                candidate.item.progress  = 0.0f;
                candidate.score          = settlerScore
                    * postureMultiplier(currentPosture,
                                        false, true, false, false, false, false);
                candidates.push_back(std::move(candidate));
            }
        }

        // --- Military unit ---
        if (bestMilitaryId.isValid()) {
            const float militaryScore = scoreMilitary(
                personality.behavior,
                unitCounts.military,
                ownedCityCount,
                enemyNearby,
                treasuryFloat,
                bbThreatLevel
            );
            const bool defenseless = (unitCounts.military == 0 && ownedCityCount >= 1);
            if (militaryScore > 0.0f || defenseless) {
                ProductionCandidate candidate{};
                candidate.item.type      = ProductionItemType::Unit;
                candidate.item.itemId    = bestMilitaryId.value;
                candidate.nameView       = bestMilitaryDef.name;
                candidate.item.totalCost = static_cast<float>(bestMilitaryDef.productionCost);
                candidate.item.progress  = 0.0f;
                candidate.score          = militaryScore
                    * postureMultiplier(currentPosture,
                                        true, false, false, false, false, false);
                // Emergency override: defenseless empire must rebuild. Pacifist
                // genes + peaceful posture can drive militaryScore below settler
                // and building scores, leaving AI civs with 0 units for hundreds
                // of turns. Hard floor bypasses all multipliers.
                if (defenseless) {
                    // Floor scales with city count so larger defenseless empires
                    // also queue military in more cities (each city picks its own
                    // candidate, so the floor runs per-city). 50.0 base ensures
                    // it beats settler peak (~9.5) and building peak (~5.0).
                    candidate.score = std::max(
                        candidate.score,
                        50.0f + static_cast<float>(ownedCityCount) * 2.0f);
                }
                candidates.push_back(std::move(candidate));
            }
        }

        // --- Builder ---
        {
            const float builderScore = scoreBuilder(
                personality.behavior,
                unimprovedTiles,
                RING1_TILES,
                unitCounts.builders,
                ownedCityCount
            );
            if (builderScore > 0.0f) {
                ProductionCandidate candidate{};
                candidate.item.type      = ProductionItemType::Unit;
                candidate.item.itemId    = 5u;
                candidate.nameView       = "Builder";
                candidate.item.totalCost = static_cast<float>(
                    unitTypeDef(UNIT_BUILDER).productionCost);
                candidate.item.progress  = 0.0f;
                candidate.score          = builderScore
                    * postureMultiplier(currentPosture,
                                        false, false, true, false, false, false);
                candidates.push_back(std::move(candidate));
            }
        }

        // --- Scout ---
        // 2026-05-02: Scouts were never produced by AI (no candidate path
        // existed). Civs got 1 starter scout in headless sim, 0 in UI mode,
        // and never replaced losses. Audit: civs that met 1-2 others traded
        // 0-2%; civs meeting 7-8 traded 75%+. More scouts = more met civs
        // = more trade.
        // Score must out-rank Mint (4.0) and Settlers (~2-5) in capital
        // when civ has no scouts at all — exploration is the ONE early
        // need that other branches don't cover. Cap at 2 per civ; once
        // exploration done, scouts naturally lose to other candidates.
        {
            const int32_t scoutCap = 2;
            if (unitCounts.scouts < scoutCap) {
                ProductionCandidate candidate{};
                candidate.item.type      = ProductionItemType::Unit;
                candidate.item.itemId    = 2u;
                candidate.nameView       = "Scout";
                candidate.item.totalCost = static_cast<float>(
                    unitTypeDef(UnitTypeId{2}).productionCost);
                candidate.item.progress  = 0.0f;
                if (unitCounts.scouts == 0) {
                    candidate.score = 6.0f;  // Top priority — nothing met yet
                } else if (ownedCityCount <= 2) {
                    candidate.score = 4.5f;  // Second scout in young empire
                } else {
                    candidate.score = 2.0f;  // Late replacement
                }
                candidates.push_back(std::move(candidate));
            }
        }

        // --- Trader ---
        {
            const float traderScore = scoreTrader(
                personality.behavior,
                hasForeignTrade,
                unitCounts.traders,
                ownedCityCount,
                computeTotalTradeSlots(*gsPlayer, grid)
            );
            if (traderScore > 0.0f) {
                ProductionCandidate candidate{};
                candidate.item.type      = ProductionItemType::Unit;
                candidate.item.itemId    = 30u;
                candidate.nameView       = "Trader";
                candidate.item.totalCost = static_cast<float>(
                    unitTypeDef(UnitTypeId{30}).productionCost);
                candidate.item.progress  = 0.0f;
                candidate.score          = traderScore
                    * postureMultiplier(currentPosture,
                                        false, false, false, false, false, true);
                // A civ with no Trader at all keeps one: on the blessed runs
                // militarised leaders queued routine military at 12-55 utility
                // against a Trader's 2.5 and never traded in 200+ turns. The
                // first commercial link outranks routine military but not the
                // defenceless floor above (50+), so a civ under real attack
                // still arms first. The first owned city alone carries the
                // floor, so the empire queues one Trader, not one per city.
                if (unitCounts.traders == 0 && ownedCityCount >= 2 && &city == firstOwnedCity(*gsPlayer)) {
                    candidate.score = std::max(candidate.score, FIRST_TRADER_FLOOR);
                }
                candidates.push_back(std::move(candidate));
            }
        }

        // --- WP-S2 Supply Wagon (Logistics) ---
        // Build one wagon per ~3 owned encampments, capped at cities/3 + 1.
        // Engineering tech (TechId 6) gates the unit. Score scales with
        // unmet supply demand (encampment buffer < refill threshold).
        if (gsPlayer->tech().hasResearched(TechId{6})) {
            int32_t ownedEncampments = 0;
            int32_t needRefill = 0;
            for (const std::pair<const int32_t,
                    aoc::game::GameState::EncampmentBuffer>& kv
                    : gameState.encampments()) {
                if (kv.second.owner != gsPlayer->id()) { continue; }
                ++ownedEncampments;
                if (kv.second.food < 50 || kv.second.fuel < 50) { ++needRefill; }
            }
            int32_t existingWagons = 0;
            for (const std::unique_ptr<aoc::game::Unit>& u : gsPlayer->units()) {
                if (u->typeDef().unitClass == UnitClass::Logistics) {
                    ++existingWagons;
                }
            }
            const int32_t cap = std::max(1, ownedCityCount / 3 + 1);
            if (ownedEncampments > 0 && needRefill > 0 && existingWagons < cap) {
                ProductionCandidate candidate{};
                candidate.item.type      = ProductionItemType::Unit;
                candidate.item.itemId    = 62u;
                candidate.nameView       = "Supply Wagon";
                candidate.item.totalCost = static_cast<float>(
                    unitTypeDef(UnitTypeId{62}).productionCost);
                candidate.item.progress  = 0.0f;
                candidate.score          = 1.5f * static_cast<float>(needRefill);
                candidates.push_back(std::move(candidate));
            }
        }

        // --- Buildings ---
        {
            aoc::sim::AIContext aiCtx{};
            aiCtx.ownedCities      = ownedCityCount;
            aiCtx.totalPopulation  = gsPlayer->totalPopulation();
            aiCtx.militaryUnits    = unitCounts.military;
            aiCtx.builderUnits     = unitCounts.builders;
            aiCtx.settlerUnits     = unitCounts.settlers;
            aiCtx.isThreatened     = unitCounts.military < 3;
            aiCtx.needsImprovements = (unimprovedTiles > 0 && unitCounts.builders == 0);
            aiCtx.hasMint          = districts.hasBuilding(BUILDING_MINT);
            aiCtx.hasCoins         = playerHasCoins;
            aiCtx.hasCampus        = districts.hasDistrict(DistrictType::Campus);
            aiCtx.hasCommercial    = districts.hasDistrict(DistrictType::Commercial);
            aiCtx.treasury         = static_cast<CurrencyAmount>(gsPlayer->treasury());
            aiCtx.targetMaxCities  = targets.maxCities;
            aiCtx.desiredMilitary  = ownedCityCount * targets.desiredMilitaryPerCity + 2;
            aiCtx.religionScienceCoef = aoc::sim::religionScienceCoefficient(
                aoc::sim::effectiveEraFromTech(*gsPlayer),
                aoc::sim::countRenaissancePlusTechs(*gsPlayer));

            for (uint16_t bidx = 0;
                     bidx < static_cast<uint16_t>(BUILDING_DEFS.size()); ++bidx) {
                const BuildingDef& bdef = BUILDING_DEFS[bidx];
                if (!canBuildBuilding(gameState, this->m_player, city, bdef.id, &grid)) {
                    continue;
                }
                // Don't queue what we cannot finish (A3).
                if (!cityCanAffordBuildingGoods(city, bdef)) {
                    continue;
                }
                const float buildingScore =
                    scoreBuildingCandidate(personality.behavior, bdef.id, aiCtx, bbTechGap);
                if (buildingScore > 0.0f) {
                    // Classify for posture multiplier: science=Library/University/ResearchLab,
                    // gold=Market/Bank/StockExchange/Mint.
                    const uint16_t bid = bdef.id.value;
                    const bool isScienceBuilding =
                        (bid == 7u || bid == 19u || bid == 12u);
                    const bool isGoldBuilding =
                        (bid == 6u || bid == 20u || bid == 21u || bid == 24u);
                    const float postureMult = postureMultiplier(
                        currentPosture,
                        false, false, false,
                        isScienceBuilding, isGoldBuilding, false);

                    ProductionCandidate candidate{};
                    candidate.item.type      = ProductionItemType::Building;
                    candidate.item.itemId    = bdef.id.value;
                    candidate.nameView       = bdef.name;
                    candidate.item.totalCost = static_cast<float>(bdef.productionCost);
                    candidate.item.progress  = 0.0f;
                    candidate.score          = buildingScore * postureMult;
                    candidates.push_back(std::move(candidate));
                }
            }
        }

        // --- Districts ---
        {
            // Check if city is coastal (any ring-1 neighbor is water)
            bool isCityCoastal = false;
            {
                const std::array<aoc::hex::AxialCoord, 6> cityNbrs =
                    aoc::hex::neighbors(city.location());
                for (const aoc::hex::AxialCoord& cn : cityNbrs) {
                    if (grid.isValid(cn) &&
                        aoc::map::isWater(grid.terrain(grid.toIndex(cn)))) {
                        isCityCoastal = true;
                        break;
                    }
                }
            }

            // Utility weights for district types. Scores are personality-adjusted
            // and then normalized so they compare fairly with unit/building scores.
            // Each district type is scored independently so the best one wins.
            struct DistrictOption {
                DistrictType type;
                float        baseCost;
                float        utilityScore;
            };

            // In barter mode, the Commercial district unlocks the Mint which is the
            // ONLY path out of barter. Override economic score with a high fixed bonus.
            // Otherwise give Commercial non-capital cities 1.15 * econFocus so it can
            // beat Harbor (1.1) / match Industrial; prior 0.45 left most non-capitals
            // without Market/Bank buildings across 600-turn games.
            const float commercialScore = (gsPlayer->monetary().system == MonetarySystemType::Barter
                                           && city.isOriginalCapital()
                                           && !districts.hasDistrict(DistrictType::Commercial))
                                          ? 1.4f   // High — need Commercial before Mint
                                          : 1.15f * personality.behavior.economicFocus;

            // Base scores bumped (0.5 -> 1.4 etc.) so districts actually win
            // over settlers (~2.14) and military (~1-2) once a city has room.
            // Without this the AI never builds districts in most seeds, which
            // kills Great People spawns and long-term yield growth.
            const float religionCoefNow = aoc::sim::religionScienceCoefficient(
                aoc::sim::effectiveEraFromTech(*gsPlayer),
                aoc::sim::countRenaissancePlusTechs(*gsPlayer));
            // The lower clamp used to be 0.2, which did not slow religion down in
            // later eras so much as end it: measured over seeds 42-45, not one
            // Holy Site was ever completed in any game, so Great Prophets could
            // not be recruited and Shrine, Temple and Cathedral could not be
            // built at all. The cause is timing, not weight. Astrology lands
            // around turn 25, by which point a young city has already spent its
            // one or two specialty slots, and by the time it grows another the
            // empire has left the eras where the coefficient is kind. A 0.4
            // floor keeps the intended decline (Ancient civs weight it 1.5,
            // Industrial ones 0.4) while leaving the district reachable.
            // Measured over seeds 42-45: 0.2 gave no Holy Sites and 9 captures,
            // 0.4 gives Holy Sites and Prophets and the same 9 captures, 0.6
            // gives Holy Sites in every seed but costs two thirds of the
            // conquest by spending slots that would have been Encampments.
            const float holySiteEraMult = std::clamp(1.0f + religionCoefNow, 0.4f, 1.8f);
            const std::array<DistrictOption, 8> districtOptions = {{
                { DistrictType::Industrial,
                  60.0f,
                  1.4f * personality.behavior.prodBuildings * personality.behavior.economicFocus },
                { DistrictType::Commercial,
                  60.0f,
                  commercialScore },
                { DistrictType::Campus,
                  55.0f,
                  1.3f * personality.behavior.scienceFocus
                       * personality.behavior.greatPersonFocus },
                { DistrictType::Encampment,
                  55.0f,
                  1.0f * personality.behavior.militaryAggression },
                { DistrictType::Harbor,
                  70.0f,
                  isCityCoastal
                      ? 1.1f * personality.behavior.economicFocus
                      : 0.0f },
                // HolySite gates all faith buildings, religion founding, and
                // Great Prophet spawns. Weight by religiousZeal; cultureFocus
                // folded in because faith also feeds cultural-policy paths.
                // Additive (not multiplicative) blend so zealots with low culture
                // still prioritize it.  The era religion-science coefficient
                // scales the whole score so Ancient/Classical civs prioritise it
                // while Industrial+ civs stop building Holy Sites.
                { DistrictType::HolySite,
                  55.0f,
                  (1.3f * personality.behavior.religiousZeal
                       + 0.8f * personality.behavior.cultureFocus)
                  * holySiteEraMult },
                // Theatre Square: Amphitheater/Art Museum/Arch. Museum. Gates
                // culture output and Great Works slots. Weighted by cultureFocus
                // and greatPersonFocus so AI tilted toward cultural/GP paths
                // prioritises it.
                { DistrictType::Theatre,
                  55.0f,
                  1.1f * personality.behavior.cultureFocus
                       * personality.behavior.greatPersonFocus },
                // Farmland: the food district. Wanted most by a city that is
                // actually short of food, so it is scored on the deficit rather
                // than on personality alone -- a city already growing well has
                // better uses for a scarce specialty slot.
                { DistrictType::Farmland,
                  50.0f,
                  (city.foodSurplus() < 0.0f ? 1.6f : 0.7f)
                      * personality.behavior.expansionism },
            }};

            // Specialty district slots are scarce: maxSpecialtyDistricts is
            // 1 + (pop - 1) / 3, so a young city has one or two. Scored on
            // personality alone, every city in an empire made the same choice
            // and the top two types took every slot. Measured over seeds 42-45:
            // 230 Industrial and 110 Encampment districts, and not one Holy Site
            // or Harbor in any game, which left Prophets and Admirals
            // unrecruitable and the whole faith building chain unbuildable.
            // Scaling by how well represented a type already is across the
            // empire lets the first of a kind compete with the fortieth.
            std::array<int32_t, DISTRICT_TYPE_COUNT> empireDistricts{};
            for (const std::unique_ptr<aoc::game::City>& owned : gsPlayer->cities()) {
                if (owned == nullptr) { continue; }
                for (const CityDistrictsComponent::PlacedDistrict& d : owned->districts().districts) {
                    const auto idx = static_cast<std::size_t>(d.type);
                    if (idx < empireDistricts.size()) { ++empireDistricts[idx]; }
                }
            }

            for (const DistrictOption& opt : districtOptions) {
                if (opt.utilityScore <= 0.0f) { continue; }
                if (districts.hasDistrict(opt.type)) { continue; }
                // Tech / civic prerequisite, population cap and coast rule.
                if (districtLockReason(gameState, this->m_player, city,
                                       static_cast<uint8_t>(opt.type), &grid)
                    != static_cast<uint8_t>(BuildLockReason::None)) {
                    continue;
                }

                ProductionCandidate candidate{};
                candidate.item.type      = ProductionItemType::District;
                candidate.item.itemId    = static_cast<uint16_t>(opt.type);
                candidate.nameView       = districtTypeName(opt.type);
                candidate.item.totalCost = opt.baseCost;
                candidate.item.progress  = 0.0f;
                const int32_t owned = empireDistricts[static_cast<std::size_t>(opt.type)];
                candidate.score =
                    opt.utilityScore / (1.0f + DISTRICT_SATURATION * static_cast<float>(owned));
                candidates.push_back(std::move(candidate));
            }
        }

        // --- Mint priority ---
        // Capital must build a Mint before anything else when in Barter.
        // Settlers score ~2.14 in expansion phase, so Mint needs to score higher.
        // Score 4.0 ensures Mint is always first production in the capital.
        // BuildingId 24 = Mint. Only CityCenter district required (always present).
        if (city.isOriginalCapital()
            && !city.hasBuilding(BUILDING_MINT)
            && canBuildBuilding(gameState, this->m_player, city, BUILDING_MINT)
            && gsPlayer->monetary().system == MonetarySystemType::Barter) {
            ProductionCandidate candidate{};
            candidate.item.type      = ProductionItemType::Building;
            candidate.item.itemId    = 24u;
            candidate.nameView       = "Mint";
            candidate.item.totalCost = 70.0f;
            candidate.item.progress  = 0.0f;
            candidate.score          = 4.0f;  // Must beat settlers (~2.14) and military
            candidates.push_back(std::move(candidate));
        }

        // --- Chain-enabler priority (Refinery, Electronics Plant, Food
        // Processing Plant) ---
        // These buildings unlock major production chains (OIL→PLASTICS→
        // ELECTRONICS→CONSUMER_GOODS, WHEAT+CATTLE→PROCESSED_FOOD).  The
        // generic scorer consistently picked Electronics Plant over Refinery
        // because of tech-era score multipliers, leaving PLASTICS starved.
        // Force-enqueue these with a guaranteed-top-tier score whenever they
        // become available and the city has the Industrial district.  The
        // player's first city to hit each condition builds the chain enabler
        // immediately instead of queueing Forge-tier buildings repeatedly.
        {
            struct ChainPriority {
                uint16_t buildingId;
                const char* name;
                float totalCost;
            };
            // 2026-05-03: Forge/Workshop/Factory added at the top. The
            // Phase-2 diag sweep showed 97.6% of IR#1-blocked civs had no
            // Charcoal in `totalSupply` even after the demand-pull fix —
            // generic scorer was leaving Forge unbuilt because higher-tier
            // industrial buildings (Refinery+) still won the score race once
            // their tech unlocked. Putting Forge/Workshop/Factory ahead in
            // this list with the same force-priority 5.0 + early `break`
            // guarantees the upstream chain enabler is queued first whenever
            // missing. Tier ordering: 0 (Forge) → 1 (Workshop) → 3 (Factory)
            // → existing late-tier list.
            const std::array<ChainPriority, 10> chain = {{
                {0u,  "Forge",               60.0f},
                {1u,  "Workshop",            40.0f},
                {3u,  "Factory",            120.0f},
                {2u,  "Refinery",           100.0f},
                {4u,  "Electronics Plant",  180.0f},
                {9u,  "Food Proc. Plant",    90.0f},
                {5u,  "Industrial Complex", 250.0f},
                {10u, "Precision Workshop", 140.0f},
                {11u, "Semiconductor Fab",  220.0f},
                {33u, "Biofuel Plant",      120.0f},
            }};
            for (const ChainPriority& cp : chain) {
                if (city.hasBuilding(BuildingId{cp.buildingId})) { continue; }
                if (!canBuildBuilding(gameState, this->m_player, city,
                                       BuildingId{cp.buildingId}, &grid)) { continue; }
                ProductionCandidate candidate{};
                candidate.item.type      = ProductionItemType::Building;
                candidate.item.itemId    = cp.buildingId;
                candidate.nameView       = cp.name;
                candidate.item.totalCost = cp.totalCost;
                candidate.item.progress  = 0.0f;
                candidate.score          = 5.0f;  // beat all production scorers
                candidates.push_back(std::move(candidate));
                break;  // only one chain-enabler forced per city per turn
            }
        }

        // --- Walls priority ---
        // Build walls when enemy is nearby and city doesn't have them yet.
        // Wall BuildingId 17 = Ancient Walls
        if (enemyNearby && !city.hasBuilding(BuildingId{17})
            && canBuildBuilding(gameState, this->m_player, city, BuildingId{17})) {
            ProductionCandidate candidate{};
            candidate.item.type      = ProductionItemType::Building;
            candidate.item.itemId    = 17u;
            candidate.nameView       = "Ancient Walls";
            candidate.item.totalCost = 80.0f;
            candidate.item.progress  = 0.0f;
            candidate.score          = 0.9f * personality.behavior.militaryAggression;
            candidates.push_back(std::move(candidate));
        }

        // --- Spy unit production ---
        // Build a Diplomat/Spy if we have none and have the tech.
        // UnitTypeId{100} = Diplomat, UnitTypeId{101} = Spy (moved from 55/56
        // after those collided with Frigate/Ironclad and unitTypeDef() kept
        // returning the naval unit for spy lookups).
        {
            bool hasSpy = false;
            for (const std::unique_ptr<aoc::game::Unit>& u : gsPlayer->units()) {
                if (u->spy().owner != INVALID_PLAYER) {
                    hasSpy = true;
                    break;
                }
            }
            if (!hasSpy) {
                UnitTypeId spyUnitId{101};
                if (!canBuildUnit(gameState, this->m_player, spyUnitId)) {
                    spyUnitId = UnitTypeId{100};  // Fallback to Diplomat
                }
                if (canBuildUnit(gameState, this->m_player, spyUnitId)) {
                    const UnitTypeDef& spyDef = unitTypeDef(spyUnitId);
                    ProductionCandidate candidate{};
                    candidate.item.type      = ProductionItemType::Unit;
                    candidate.item.itemId    = spyUnitId.value;
                    candidate.nameView       = spyDef.name;
                    candidate.item.totalCost = static_cast<float>(spyDef.productionCost);
                    candidate.item.progress  = 0.0f;
                    // 3.5 gives spy parity with military (~3-6 post-posture)
                    // so it wins sometimes but doesn't monopolize production
                    // after spies die on failed missions.
                    candidate.score          = 3.5f * personality.behavior.espionagePriority;
                    candidates.push_back(std::move(candidate));
                }
            }
        }

        // --- Missionary / Apostle production ---
        // UnitTypeId{19}=Missionary, {20}=Apostle, {21}=Inquisitor (see
        // UnitTypes.hpp:207-209). Requires: player founded a religion AND city
        // has a HolySite. Score scales with religiousZeal so zealous leaders
        // actually convert neighbors; pragmatic leaders skip.
        {
            const ReligionId foundedRel = gsPlayer->faith().foundedReligion;
            const bool hasReligion = (foundedRel != NO_RELIGION);
            const bool hasHolySite = city.hasDistrict(DistrictType::HolySite);
            if (hasReligion && hasHolySite) {
                // Count own missionaries to avoid swamping the queue.
                int32_t ownMissionaries = 0;
                for (const std::unique_ptr<aoc::game::Unit>& u : gsPlayer->units()) {
                    if (u == nullptr) { continue; }
                    const uint16_t tid = u->typeId().value;
                    if (tid == 19 || tid == 20) { ++ownMissionaries; }
                }
                const int32_t missionaryCap = static_cast<int32_t>(gsPlayer->cities().size());
                if (ownMissionaries < missionaryCap) {
                    UnitTypeId missionaryId{19};
                    if (canBuildUnit(gameState, this->m_player, missionaryId)) {
                        const UnitTypeDef& mdef = unitTypeDef(missionaryId);
                        ProductionCandidate candidate{};
                        candidate.item.type      = ProductionItemType::Unit;
                        candidate.item.itemId    = missionaryId.value;
                        candidate.nameView       = mdef.name;
                        candidate.item.totalCost = static_cast<float>(mdef.productionCost);
                        candidate.item.progress  = 0.0f;
                        candidate.score          = 2.8f
                            * personality.behavior.religiousZeal
                            * personality.behavior.prodReligious;
                        candidates.push_back(std::move(candidate));
                    }
                }
            }
        }

        // --- Wonders ---
        // Score each buildable wonder using cultureFocus + prodWonders genes.
        // Buildable = tech prereq met, not already built globally.
        {
            const std::array<WonderDef, WONDER_COUNT>& allWonders = allWonderDefs();
            // Wonder scoring deliberately kept in the 1-4 range so a
            // culture-focused leader prefers them but a militaristic one
            // does not sink half of every city's hammers into wonders.
            // Previous 200 base + 40/unit bonuses ballooned to 11-39,
            // crushing military/settler/district candidates.
            const float wonderBase = 80.0f
                * personality.behavior.cultureFocus
                * personality.behavior.prodWonders;

            for (const WonderDef& wdef : allWonders) {
                if (!canBuildWonder(gameState, this->m_player, wdef.id)) {
                    continue;
                }
                if (city.wonders().hasWonder(wdef.id)) {
                    continue;
                }

                float wonderScore = wonderBase;
                wonderScore += 15.0f * wdef.effect.scienceBonus
                            * personality.behavior.scienceFocus;
                wonderScore += 15.0f * wdef.effect.cultureBonus
                            * personality.behavior.cultureFocus;
                wonderScore += 12.0f * wdef.effect.goldBonus
                            * personality.behavior.economicFocus;
                wonderScore += 12.0f * wdef.effect.faithBonus
                            * personality.behavior.religiousZeal;
                wonderScore *= 0.01f;

                if (wonderScore <= 0.0f) { continue; }

                ProductionCandidate candidate{};
                candidate.item.type      = ProductionItemType::Wonder;
                candidate.item.itemId    = static_cast<uint16_t>(wdef.id);
                candidate.nameView       = wdef.name;
                candidate.item.totalCost = static_cast<float>(wdef.productionCost);
                candidate.item.progress  = 0.0f;
                candidate.score          = wonderScore
                    * postureMultiplier(currentPosture,
                                        false, false, false, false, false, false);
                candidates.push_back(std::move(candidate));
            }
        }

        // ----------------------------------------------------------------
        // Select from the top candidates with 10% randomization band.
        //
        // Pick the highest score, then collect all candidates within 90% of
        // that peak. Choose uniformly from that set using a deterministic
        // per-city hash so different cities make different decisions each turn
        // without a full PRNG dependency here.
        // ----------------------------------------------------------------

        if (candidates.empty()) {
            // Absolute last resort: fill the army up to the AI's own target; past
            // it the city runs a project (Civ VI's answer to an empty build list)
            // instead of idling. An unconditional unit here tripled production
            // and bankrupted every AI (measured 2026-09-05, seed 42).
            const int32_t desiredMilitary = ownedCityCount * targets.desiredMilitaryPerCity + 2;
            if (!bestMilitaryId.isValid() || unitCounts.military >= desiredMilitary) {
                // Raising a production building the city already runs beats a
                // generic project: capacity() gates how many recipes it can
                // turn out per turn, and every building sat at level 1 until
                // 2026-09-07 because nothing could upgrade one. Deepest tier
                // first -- those are the throughput bottlenecks.
                bool queuedUpgrade = false;
                constexpr std::array<BuildingId, 8> UPGRADE_ORDER = {
                    BuildingId{5},  // Industrial Complex
                    BuildingId{11}, // Semiconductor Fab
                    BuildingId{12}, // Research Lab
                    BuildingId{3},  // Factory
                    BuildingId{4},  // Electronics Plant
                    BuildingId{2},  // Refinery
                    BuildingId{1},  // Workshop
                    BuildingId{0},  // Forge
                };
                for (BuildingId bid : UPGRADE_ORDER) {
                    if (requestUpgradeBuilding(gameState, this->m_player, city.location(), bid)
                        == ErrorCode::Ok) {
                        queuedUpgrade = true;
                        break;
                    }
                }
                if (!queuedUpgrade) {
                    constexpr std::array<CityProjectType, 4> PROJECT_ORDER = {
                        CityProjectType::IndustrialSurge, CityProjectType::CampusResearch,
                        CityProjectType::CommercialInvestment, CityProjectType::BreadAndCircuses};
                    for (CityProjectType project : PROJECT_ORDER) {
                        if (requestQueueProject(gameState, this->m_player, city.location(), project)
                            == ErrorCode::Ok) {
                            break;
                        }
                    }
                }
                ++cityIndex;
                continue;
            }
            if (bestMilitaryId.isValid() && unitCounts.military < desiredMilitary) {
                ProductionQueueItem fallbackItem{};
                fallbackItem.type      = ProductionItemType::Unit;
                fallbackItem.itemId    = bestMilitaryId.value;
                fallbackItem.name      = std::string(bestMilitaryDef.name);
                fallbackItem.totalCost = static_cast<float>(bestMilitaryDef.productionCost)
                                         * aoc::sim::GamePace::instance().costMultiplier;
                fallbackItem.progress  = 0.0f;
                queue.queue.push_back(std::move(fallbackItem));
                ++cityIndex;
                continue;
            }
            ++cityIndex;
            continue;
        }

        float topScore = 0.0f;
        for (const ProductionCandidate& c : candidates) {
            if (c.score > topScore) { topScore = c.score; }
        }

        // Gather all candidates within 10% of the top score. Reuse the
        // per-AIController scratch (clear() preserves capacity) so this is not a
        // fresh per-city allocation (audit WP-10 #6).
        std::vector<std::size_t>& topIndices = this->m_topIndicesScratch;
        topIndices.clear();
        topIndices.reserve(candidates.size());
        const float scoreFloor = topScore * 0.9f;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (candidates[i].score >= scoreFloor) {
                topIndices.push_back(i);
            }
        }

        // Deterministic tie-break: hash (cityIndex, cityIndex^turnSeed) to pick
        // uniformly from the near-top set. This gives each city a stable but
        // varied choice without requiring the RNG to be threaded through here.
        const std::size_t choiceHash =
            (static_cast<std::size_t>(cityIndex) * 6364136223846793005ULL + 1442695040888963407ULL)
            ^ (static_cast<std::size_t>(ownedCityCount) * 2654435761ULL);
        const std::size_t chosenIdx = topIndices[choiceHash % topIndices.size()];

        ProductionQueueItem chosen = candidates[chosenIdx].item;
        // Materialise the owning name std::string only for the chosen candidate;
        // candidates carry a non-owning nameView during scoring (audit WP-10 #6).
        chosen.name.assign(candidates[chosenIdx].nameView);
        chosen.totalCost *= aoc::sim::GamePace::instance().costMultiplier;

        LOG_INFO("AI %u Enqueued %s in %s (utility %.3f)",
                 static_cast<unsigned>(this->m_player),
                 chosen.name.c_str(),
                 city.name().c_str(),
                 static_cast<double>(candidates[chosenIdx].score));

        // Binary decision log: top-3 alternates (excluding chosen) + chosen.
        if (aoc::core::DecisionLog* log = aoc::core::currentDecisionLog();
            log != nullptr && log->active()) {
            auto mapKind = [](ProductionItemType t) {
                switch (t) {
                    case ProductionItemType::Unit:     return aoc::core::ProductionItemKind::Unit;
                    case ProductionItemType::Building:
                    case ProductionItemType::BuildingUpgrade:
                        return aoc::core::ProductionItemKind::Building;
                    case ProductionItemType::Project:  // logged as a district-tier build
                    case ProductionItemType::District: return aoc::core::ProductionItemKind::District;
                    case ProductionItemType::Wonder:   return aoc::core::ProductionItemKind::Wonder;
                }
                return aoc::core::ProductionItemKind::Unknown;
            };

            // DEBT (audit WP-10 #7): only the top 3 of `sorted` are read below,
            // so std::partial_sort would suffice. NOT applied: neither std::sort
            // nor std::partial_sort is stable, and they can order tied scores
            // differently from each other. The logged alternates would then
            // differ on ties, which a decision-log replay/diff harness compares.
            // This whole block is also cold (only runs when the decision log is
            // active). Switching requires a stable top-N (e.g. tie-break on index)
            // to keep the logged order byte-identical -- deferred.
            std::vector<std::size_t> sorted(candidates.size());
            for (std::size_t i = 0; i < candidates.size(); ++i) { sorted[i] = i; }
            std::sort(sorted.begin(), sorted.end(),
                      [&](std::size_t a, std::size_t b) {
                          return candidates[a].score > candidates[b].score;
                      });

            std::vector<aoc::core::ProductionAlt> alts;
            alts.reserve(3);
            for (std::size_t i = 0; i < sorted.size() && alts.size() < 3; ++i) {
                if (sorted[i] == chosenIdx) { continue; }
                const ProductionCandidate& c = candidates[sorted[i]];
                aoc::core::ProductionAlt alt{};
                alt.itemId = c.item.itemId;
                alt.score  = c.score;
                alt.kind   = static_cast<uint8_t>(mapKind(c.item.type));
                alts.push_back(alt);
            }

            log->logProduction(
                static_cast<uint16_t>(gameState.currentTurn()),
                static_cast<uint8_t>(this->m_player),
                static_cast<uint16_t>(cityIndex),
                mapKind(candidates[chosenIdx].item.type),
                static_cast<uint32_t>(candidates[chosenIdx].item.itemId),
                candidates[chosenIdx].score,
                std::span<const aoc::core::ProductionAlt>(alts.data(), alts.size()));
        }

        queue.queue.push_back(std::move(chosen));
        ++cityIndex;
    }
}

// ============================================================================
// Diplomacy
// ============================================================================


// ============================================================================
// Economy management
// ============================================================================

// ============================================================================
// Government management
// ============================================================================

void AIController::manageMonetarySystem(aoc::game::GameState& gameState,
                                         aoc::map::HexGrid& grid,
                                         const DiplomacyManager& /*diplomacy*/) {
    aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    if (gsPlayer == nullptr) { return; }

    aoc::sim::MonetaryStateComponent& myState = gsPlayer->monetary();
    const int32_t cityCount = gsPlayer->ownedCityCount();
    const int32_t playerCount = gameState.playerCount();

    // Dynamic gold allocation: raise goldAllocation when treasury is low,
    // lower it when flush with gold to boost science/luxury. If any city is
    // unhappy, force luxury slider up regardless of treasury pressure -- unrest
    // costs more than a temporary budget dip.
    {
        const CurrencyAmount treasury = gsPlayer->treasury();
        bool anyUnhappy = false;
        for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
            if (city->happiness().happiness < 1.0f) { anyUnhappy = true; break; }
        }
        if (anyUnhappy && treasury > -1000) {
            // Prioritise luxury when citizens restless AND we can still afford it
            myState.luxuryAllocation  = std::min(myState.luxuryAllocation + 0.02f, 0.28f);
            myState.scienceAllocation = std::max(myState.scienceAllocation - 0.015f, 0.05f);
            myState.goldAllocation    = 1.0f - myState.luxuryAllocation - myState.scienceAllocation;
        } else if (treasury < 0) {
            myState.goldAllocation    = std::min(myState.goldAllocation + 0.05f, 0.85f);
            myState.scienceAllocation = std::max(myState.scienceAllocation - 0.03f, 0.05f);
            myState.luxuryAllocation  = 1.0f - myState.goldAllocation - myState.scienceAllocation;
        } else if (treasury > 2000) {
            myState.goldAllocation    = std::max(myState.goldAllocation - 0.02f, 0.40f);
            myState.scienceAllocation = std::min(myState.scienceAllocation + 0.01f, 0.40f);
            myState.luxuryAllocation  = 1.0f - myState.goldAllocation - myState.scienceAllocation;
        }
        myState.luxuryAllocation = std::max(myState.luxuryAllocation, 0.10f);
    }

    // Count distinct trade partners from global trade routes. PlayerId is a
    // small bounded index, so a stack bitset replaces the per-call heap-backed
    // unordered_set while producing the identical distinct count (audit WP-10 #6).
    int32_t tradePartnerCount = 0;
    {
        std::array<bool, MAX_PLAYERS> partnerSeen{};
        auto markPartner = [&](PlayerId pid) {
            if (pid < MAX_PLAYERS && !partnerSeen[pid]) {
                partnerSeen[pid] = true;
                ++tradePartnerCount;
            }
        };
        // Live Traders, major civs only: markPartner's MAX_PLAYERS guard
        // leaves city-state destinations out.
        const aoc::game::Player* me = gameState.player(this->m_player);
        if (me != nullptr) {
            for (const std::unique_ptr<aoc::game::Unit>& unit : me->units()) {
                const aoc::sim::TraderComponent& trader = unit->trader();
                if (trader.owner != INVALID_PLAYER && trader.destOwner != INVALID_PLAYER &&
                    trader.destOwner != this->m_player) {
                    markPartner(trader.destOwner);
                }
            }
        }
    }

    // Compute GDP rank among all players
    int32_t gdpRank = 1;
    for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
        if (other->id() != this->m_player && other->monetary().gdp > myState.gdp) {
            ++gdpRank;
        }
    }

    // Fiat money printing: if on fiat and in deficit, print money to cover
    // shortfall. But only if inflation is below 10% — don't hyperinflate.
    // This represents governments deficit-spending by printing money, which is
    // the key behavior of fiat economies (for better or worse).
    if ((myState.system == MonetarySystemType::FiatMoney
         || myState.system == MonetarySystemType::Digital)
        && gsPlayer->treasury() < 0
        && myState.inflationRate < 0.10f) {
        const CurrencyAmount shortfall = -gsPlayer->treasury();
        // Print up to half the shortfall — don't cover everything, force some austerity
        const CurrencyAmount toPrint = std::max(
            static_cast<CurrencyAmount>(1), shortfall / 2);
        const CurrencyAmount printed = myState.printMoney(toPrint);
        if (printed > 0) {
            LOG_INFO("AI %u printed %lld fiat money (inflation now %.2f%%)",
                     static_cast<unsigned>(this->m_player),
                     static_cast<long long>(printed),
                     static_cast<double>(myState.inflationRate * 100.0f));
        }
    }

    // Debasement: the coinage-era answer to a shortfall, and the counterpart to
    // printing. It was fully implemented with zero callers, so nothing could
    // ever debase, debasementRatio stayed 0 forever, and the whole discovery /
    // Gresham's-law / remint machinery below it was unreachable code.
    //
    // The trade is real: more coins now from the same bullion, against a
    // discovery risk and a reputational trade penalty once found out. An AI
    // reaches for it under the same pressure that makes a fiat civ print.
    if (myState.system == MonetarySystemType::CommodityMoney && gsPlayer->treasury() < 0
        && myState.debasement.debasementRatio < 0.25f) {
        if (requestDebaseCurrency(gameState, this->m_player, 0.05f) == ErrorCode::Ok) {
            LOG_INFO("AI %u debased its coinage to %.0f%% base metal (treasury %lld)",
                     static_cast<unsigned>(this->m_player),
                     static_cast<double>(myState.debasement.debasementRatio * 100.0f),
                     static_cast<long long>(gsPlayer->treasury()));
        }
    }

    // And the escape valve: once solvent again, restrike at full content rather
    // than carry the penalty forever. Requires real money to do it, which is
    // what makes debasement a loan against reputation rather than free gold.
    if (myState.debasement.debasementRatio > 0.0f && gsPlayer->treasury() > 400) {
        if (requestRemintCurrency(gameState, this->m_player) == ErrorCode::Ok) {
            LOG_INFO("AI %u reminted its coinage at full metal content",
                     static_cast<unsigned>(this->m_player));
        }
    }

    MonetarySystemType nextTarget = MonetarySystemType::Count;
    switch (myState.system) {
        case MonetarySystemType::Barter:
            nextTarget = MonetarySystemType::CommodityMoney;
            break;
        case MonetarySystemType::CommodityMoney:
            nextTarget = MonetarySystemType::GoldStandard;
            break;
        case MonetarySystemType::GoldStandard:
            nextTarget = MonetarySystemType::FiatMoney;
            break;
        case MonetarySystemType::FiatMoney:
            nextTarget = MonetarySystemType::Digital;
            break;
        default:
            return;
    }

    if ((nextTarget == MonetarySystemType::FiatMoney
         || nextTarget == MonetarySystemType::Digital)
        && gdpRank > 2) {
        return;
    }

    // Digital requires a working electric grid: at least one city with
    // energySupply >= energyDemand (i.e. actually powered, not just built).
    if (nextTarget == MonetarySystemType::Digital) {
        bool hasPower = false;
        for (const std::unique_ptr<aoc::game::City>& cityPtr : gsPlayer->cities()) {
            if (cityPtr == nullptr) { continue; }
            const CityPowerComponent pw = computeCityPower(gameState, grid, *cityPtr);
            if (pw.energySupply > 0 && pw.energySupply >= pw.energyDemand) {
                hasPower = true;
                break;
            }
        }
        if (!hasPower) {
            if (myState.turnsInCurrentSystem % 50 == 0 && myState.turnsInCurrentSystem > 0) {
                LOG_INFO("AI player %u cannot transition to Digital: no power plants built",
                         static_cast<unsigned>(this->m_player));
            }
            return;
        }
    }

    const aoc::game::Player* techPlayer = gameState.player(this->m_player);
    const ErrorCode result               = myState.canTransition(
        nextTarget, cityCount,
        [techPlayer](aoc::TechId t) {
            return techPlayer != nullptr && techPlayer->hasResearched(t);
        },
        tradePartnerCount, gdpRank, playerCount);
    if (result == ErrorCode::Ok) {
        myState.transitionTo(nextTarget);

        // Bootstrap treasury on first monetization: coins in circulation
        // become the initial government spending power.  Without this, the
        // player would start the monetary era with 0 treasury while already
        // owing maintenance on all the units built during barter.
        if (nextTarget == MonetarySystemType::CommodityMoney) {
            // Bootstrap treasury: coins in circulation + population savings.
            // Represents the accumulated wealth that gets monetized when coins
            // are introduced. Without this the player starts with 0 treasury
            // while owing maintenance on all barter-era units immediately.
            if (gsPlayer != nullptr && gsPlayer->treasury() <= 0) {
                const CurrencyAmount coinValue =
                    static_cast<CurrencyAmount>(myState.totalCoinValue());
                const CurrencyAmount popSavings =
                    static_cast<CurrencyAmount>(gsPlayer->totalPopulation() * 4);
                gsPlayer->setTreasury(coinValue + popSavings, aoc::sim::MoneyFlow::unbacked());
            }
        }

        LOG_INFO("AI player %u transitioned to %.*s",
                 static_cast<unsigned>(this->m_player),
                 static_cast<int>(monetarySystemName(nextTarget).size()),
                 monetarySystemName(nextTarget).data());
    } else {
        if (myState.turnsInCurrentSystem % 50 == 0 && myState.turnsInCurrentSystem > 0) {
            LOG_INFO("AI player %u cannot transition to %.*s: "
                     "strength=%d coins=%d cities=%d trades=%d inflation=%.2f",
                     static_cast<unsigned>(this->m_player),
                     static_cast<int>(monetarySystemName(nextTarget).size()),
                     monetarySystemName(nextTarget).data(),
                     myState.currencyStrength(),
                     myState.totalCoinCount(),
                     cityCount, tradePartnerCount,
                     static_cast<double>(myState.inflationRate));
        }
    }
}

// ============================================================================
// Gold purchasing: buy units or buildings when it makes strategic sense
// ============================================================================

void AIController::considerPurchases(aoc::game::GameState& gameState) {
    aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    if (gsPlayer == nullptr) { return; }

    const CurrencyAmount treasury = gsPlayer->treasury();
    if (treasury < 100) { return; }

    const aoc::sim::ai::AIBlackboard& bb = gsPlayer->blackboard();
    const LeaderPersonalityDef& personality =
        leaderPersonality(gsPlayer->civId());
    const LeaderBehavior& beh = personality.behavior;

    // Military purchase: buy when below minimum garrison OR under threat.
    // Aggressive leaders (Montezuma: aggression=1.7) buy military more eagerly;
    // peaceful leaders (Gandhi: aggression=0.2) only buy when critically threatened.
    const int32_t milCount = gsPlayer->militaryUnitCount();
    const int32_t cityCount = gsPlayer->ownedCityCount();
    const int32_t desiredGarrison = static_cast<int32_t>(
        static_cast<float>(cityCount) * 2.0f * beh.militaryAggression);
    const bool needsMilitary = milCount < std::max(desiredGarrison, cityCount);
    const float threatThreshold = 0.5f - beh.militaryAggression * 0.2f;
    const bool underThreat = bb.threatLevel > std::max(threatThreshold, 0.1f);
    if ((needsMilitary || underThreat)
        && treasury >= 200
        && !gsPlayer->cities().empty()) {
        aoc::game::City& capital = *gsPlayer->cities().front();
        // Find best affordable military unit (prefer strongest that we can afford).
        UnitTypeId bestId{0};
        int32_t bestStrength = 0;
        for (const UnitTypeDef& def : UNIT_TYPE_DEFS) {
            if (!isMilitary(def.unitClass) || isNaval(def.unitClass)) { continue; }
            if (!canBuildUnit(gameState, this->m_player, def.id)) { continue; }
            const int32_t unitCost = purchaseCost(static_cast<float>(def.productionCost));
            if (treasury < static_cast<CurrencyAmount>(unitCost)) { continue; }
            const int32_t str = def.combatStrength + def.rangedStrength;
            if (str > bestStrength) {
                bestStrength = str;
                bestId = def.id;
            }
        }
        if (bestStrength > 0) {
            const int32_t cost = purchaseCost(static_cast<float>(unitTypeDef(bestId).productionCost));
            if (cost > 0 && treasury >= static_cast<CurrencyAmount>(cost)) {
                const ErrorCode result = purchaseInCity(gameState, *gsPlayer, capital,
                                                         ProductionItemType::Unit, bestId.value);
                if (result == ErrorCode::Ok) { return; }
            }
        }
    }

    // Settler purchase: buy when expanding and have surplus gold.
    // Buy if blackboard says expand, OR if we have few cities and a healthy treasury.
    // Suppress entirely when the advisor reports no viable sites -- buying a
    // settler that cannot be placed would just drain the treasury.
    const bool wantsExpansion = !bb.expansionExhausted
                             && (bb.expansionOpportunity > 0.3f
                                 || (cityCount < 4 && treasury >= 500));
    if (wantsExpansion && !gsPlayer->cities().empty()) {
        // Check no settler already exists.
        bool hasSettler = false;
        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : gsPlayer->units()) {
            if (unitTypeDef(unitPtr->typeId()).unitClass == UnitClass::Settler) {
                hasSettler = true;
                break;
            }
        }
        if (!hasSettler) {
            aoc::game::City& capital = *gsPlayer->cities().front();
            const int32_t settlerCost = purchaseCost(static_cast<float>(unitTypeDef(UNIT_SETTLER).productionCost));
            if (treasury >= static_cast<CurrencyAmount>(settlerCost)) {
                const ErrorCode result = purchaseInCity(gameState, *gsPlayer, capital,
                                                         ProductionItemType::Unit, 3);
                if (result == ErrorCode::Ok) { return; }
            }
        }
    }

    // ROI-based building purchase: buy if payback period is reasonable.
    // Economic leaders (Cleopatra: economicFocus=1.8) accept longer payback periods.
    float maxPaybackTurns = 30.0f * beh.economicFocus;
    if (treasury > 5000) { maxPaybackTurns = 60.0f * beh.economicFocus; }
    if (treasury > 10000) { maxPaybackTurns = 100.0f * beh.economicFocus; }

    for (const std::unique_ptr<aoc::game::City>& cityPtr : gsPlayer->cities()) {
        for (const BuildingDef& bdef : BUILDING_DEFS) {
            if (!canBuildBuilding(gameState, this->m_player, *cityPtr, bdef.id)) { continue; }

            const int32_t goldCost = purchaseCost(static_cast<float>(bdef.productionCost));
            if (goldCost <= 0 || treasury < static_cast<CurrencyAmount>(goldCost)) { continue; }

            // Estimate yield per turn, weighted by leader's priorities.
            const float yieldPerTurn = static_cast<float>(bdef.goldBonus) * beh.economicFocus
                                     + static_cast<float>(bdef.scienceBonus) * 0.5f * beh.scienceFocus
                                     + static_cast<float>(bdef.productionBonus) * 0.8f;
            if (yieldPerTurn <= 0.0f) { continue; }

            const float paybackTurns = static_cast<float>(goldCost) / yieldPerTurn;
            if (paybackTurns <= maxPaybackTurns) {
                const ErrorCode result = purchaseInCity(gameState, *gsPlayer, *cityPtr,
                                                         ProductionItemType::Building, bdef.id.value);
                if (result == ErrorCode::Ok) { return; }
            }
        }
    }
}

// ============================================================================
// Canal building: scan owned isthmus/chokepoint tiles for canal opportunities
// ============================================================================

void AIController::considerCanalBuilding(aoc::game::GameState& gameState,
                                          aoc::map::HexGrid& grid,
                                          const aoc::map::FogOfWar* fogOfWar) {
    aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    if (gsPlayer == nullptr) { return; }

    // Canal requires Industrial Era — gate on base Industrialization (TechId{11}).
    constexpr TechId INDUSTRIALIZATION_TECH = TechId{11};
    if (!gsPlayer->tech().hasResearched(INDUSTRIALIZATION_TECH)) {
        return;
    }

    constexpr CurrencyAmount CANAL_GOLD_COST = 300;
    if (gsPlayer->treasury() < CANAL_GOLD_COST) {
        return;
    }

    // ---- Step 1: Measure trade traffic visible to this player ----
    // Only count traders the player can actually see via fog of war.
    // When fog of war is unavailable (headless mode), fall back to tile ownership.
    const int32_t totalTiles = grid.width() * grid.height();
    std::vector<int32_t> tradeProximity(static_cast<std::size_t>(totalTiles), 0);
    int32_t tradeTrafficTiles = 0;
    int32_t visibleActiveTraders = 0;

    const auto tileIsVisible = [&](int32_t tileIdx) -> bool {
        if (fogOfWar != nullptr) {
            aoc::map::TileVisibility vis = fogOfWar->visibility(this->m_player, tileIdx);
            return vis == aoc::map::TileVisibility::Visible;
        }
        // Fallback: treat owned tiles as visible
        return grid.owner(tileIdx) == this->m_player;
    };

    for (const std::unique_ptr<aoc::game::Player>& pPtr : gameState.players()) {
        for (const std::unique_ptr<aoc::game::Unit>& u : pPtr->units()) {
            if (unitTypeDef(u->typeId()).unitClass != UnitClass::Trader) { continue; }
            const TraderComponent& trader = u->trader();
            if (trader.path.empty()) { continue; }

            bool traderVisible = false;
            for (const aoc::hex::AxialCoord& pathTile : trader.path) {
                if (!grid.isValid(pathTile)) { continue; }
                int32_t idx = grid.toIndex(pathTile);
                if (!tileIsVisible(idx)) { continue; }

                // Tile is visible — we can observe this trader
                traderVisible = true;
                ++tradeTrafficTiles;

                // Build proximity heatmap: only for visible tiles and their
                // visible neighbors. This ensures the heatmap only reflects
                // what the player actually observes.
                tradeProximity[static_cast<std::size_t>(idx)] += 2;
                std::array<aoc::hex::AxialCoord, 6> ring1 = aoc::hex::neighbors(pathTile);
                for (const aoc::hex::AxialCoord& n1 : ring1) {
                    if (!grid.isValid(n1)) { continue; }
                    int32_t n1Idx = grid.toIndex(n1);
                    if (tileIsVisible(n1Idx)) {
                        tradeProximity[static_cast<std::size_t>(n1Idx)] += 1;
                    }
                }
            }
            if (traderVisible) { ++visibleActiveTraders; }
        }
    }

    // Need meaningful observed trade traffic before investing in canals.
    // At least 2 traders visible in our territory and 4+ route tiles.
    if (visibleActiveTraders < 2 || tradeTrafficTiles < 4) {
        return;
    }

    const LeaderPersonalityDef& personality = leaderPersonality(gsPlayer->civId());
    const LeaderBehavior& beh = personality.behavior;

    // ---- Step 3: Score canal candidate tiles ----
    struct CanalCandidate {
        int32_t tileIndex;
        float   score;
    };
    std::vector<CanalCandidate> candidates;

    for (int32_t i = 0; i < totalTiles; ++i) {
        if (grid.owner(i) != this->m_player) { continue; }
        if (!aoc::sim::canBuildTerrainProject(grid, i, aoc::sim::TerrainProjectType::Canal)) {
            continue;
        }

        // Skip tiles with zero trade proximity — no traders nearby, canal is useless
        int32_t proximity = tradeProximity[static_cast<std::size_t>(i)];
        if (proximity == 0) { continue; }

        // Count adjacent canals — tiles next to existing canals are just
        // extending a canal field, not creating a new strategic shortcut.
        aoc::hex::AxialCoord center = grid.toAxial(i);
        std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(center);
        int32_t adjacentCanals = 0;
        for (const aoc::hex::AxialCoord& n : nbrs) {
            if (!grid.isValid(n)) { continue; }
            if (grid.improvement(grid.toIndex(n)) == aoc::map::ImprovementType::Canal) {
                ++adjacentCanals;
            }
        }
        // Skip if already bordered by a canal — prevents canal sprawl
        if (adjacentCanals > 0) { continue; }

        aoc::map::ChokepointType cpType = grid.chokepoint(i);
        float score = 0.0f;

        // Base score from geography
        if (cpType == aoc::map::ChokepointType::Isthmus) {
            score = 10.0f;
        } else if (cpType == aoc::map::ChokepointType::LandChokepoint) {
            score = 4.0f;
        } else {
            score = 1.0f;
        }

        // Trade proximity multiplier: more trade traffic = more valuable canal
        score *= (1.0f + static_cast<float>(proximity) * 0.3f);

        // Economic leaders value canals more (toll revenue)
        score *= (0.5f + beh.economicFocus);

        // Naval-focused leaders also value canals (fleet mobility)
        score *= (0.5f + beh.techNaval * 0.5f);

        // Minimum score threshold
        if (score >= 3.0f) {
            candidates.push_back({i, score});
        }
    }

    if (candidates.empty()) { return; }

    // Pick the best candidate
    CanalCandidate best = candidates[0];
    for (std::size_t c = 1; c < candidates.size(); ++c) {
        if (candidates[c].score > best.score) {
            best = candidates[c];
        }
    }

    // Build one canal per turn (expensive, strategic decision)
    if (gsPlayer->spendGold(CANAL_GOLD_COST, aoc::sim::MoneyFlow::unbacked())) {
        ErrorCode result = aoc::sim::executeTerrainProject(
            grid, best.tileIndex, aoc::sim::TerrainProjectType::Canal);
        if (result == ErrorCode::Ok) {
            aoc::hex::AxialCoord pos = grid.toAxial(best.tileIndex);
            LOG_INFO("AI %u built canal at (%d, %d) -- score %.1f, traffic %d, cost %d gold",
                     static_cast<unsigned>(this->m_player),
                     static_cast<int>(pos.q), static_cast<int>(pos.r),
                     static_cast<double>(best.score),
                     static_cast<int>(tradeTrafficTiles),
                     static_cast<int>(CANAL_GOLD_COST));
        } else {
            gsPlayer->addGold(CANAL_GOLD_COST, aoc::sim::MoneyFlow::unbacked());
        }
    }
}

} // namespace aoc::sim::ai
