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
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
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

namespace aoc::sim::ai {

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
static UnitTypeId bestAvailableMilitaryUnit(const aoc::game::GameState& gameState,
                                            PlayerId player) {
    const EnemyComposition enemyComp = analyzeEnemyComposition(gameState, player);

    // Aggregate player's city stockpiles so we can filter out units whose
    // resourceReqs we can't satisfy. Previously the AI enqueued Knights/Horsemen
    // the city lacked Horses/Iron for, production "completed" without output
    // (ProductionSystem drops stuck items silently), and the turn was wasted.
    const aoc::game::Player* gsPlayer = gameState.player(player);
    std::unordered_map<uint16_t, int32_t> totalStockpile;
    if (gsPlayer != nullptr) {
        for (const std::unique_ptr<aoc::game::City>& cityPtr : gsPlayer->cities()) {
            for (const std::pair<const uint16_t, int32_t>& entry
                    : cityPtr->stockpile().goods) {
                totalStockpile[entry.first] += entry.second;
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
                auto it = totalStockpile.find(req.goodId);
                if (it == totalStockpile.end() || it->second < req.amount) {
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
        if (u->typeId().value == 5) {
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
                                aoc::Random& rng,
                                GlobalDealTracker* dealTracker) {
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
            const char* postureName =
                POSTURE_NAMES[static_cast<std::size_t>(bb.posture)];
            LOG_INFO("AI %u Strategic posture changed to: %s",
                     static_cast<unsigned>(this->m_player), postureName);
        }
    }

    this->manageGovernment(gameState);
    this->m_researchPlanner.selectResearch(gameState);
    this->executeCityActions(gameState, grid);
    this->m_builderController.manageBuildersAndImprovements(gameState, grid);
    this->m_settlerController.executeSettlerActions(gameState, grid);
    this->m_militaryController.executeMilitaryActions(gameState, grid, rng, &diplomacy);
    this->manageEconomy(gameState, diplomacy, market);
    this->manageMonetarySystem(gameState, grid, diplomacy);
    aoc::sim::aiEconomicStrategy(gameState, grid, market, diplomacy, this->m_player,
                                  static_cast<int32_t>(this->m_difficulty));
    this->executeDiplomacyActions(gameState, grid, diplomacy, market, dealTracker);
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
                const float bubble = 1.0f; // Placeholder: MarketManipulation likes bubbles

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
                    aoc::game::Unit* launcher = nullptr;
                    for (const std::unique_ptr<aoc::game::Unit>& u : nukePlayer->units()) {
                        if (u->isMilitary() && !u->isDead()) {
                            launcher = u.get();
                            break;
                        }
                    }
                    if (haveTarget && launcher != nullptr) {
                        launcher->nuclear().equipped = true;
                        launcher->nuclear().type = NukeType::NuclearDevice;
                        LOG_INFO("AI %u NUCLEAR STRIKE decision: target p%u at (%d,%d) "
                                 "score=%.2f roll=%.2f",
                                 static_cast<unsigned>(this->m_player),
                                 static_cast<unsigned>(enemyId),
                                 targetLoc.q, targetLoc.r,
                                 static_cast<double>(launchScore),
                                 static_cast<double>(roll));
                        [[maybe_unused]] ErrorCode nec = launchNuclearStrike(
                            gameState, grid, this->m_player, targetLoc,
                            NukeType::NuclearDevice);
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
                const float progressRatio = tech.currentResearch.isValid()
                    ? tech.researchProgress
                      / (static_cast<float>(techDef(tech.currentResearch).researchCost) + 1.0f)
                    : 0.0f;
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
// Internal: scored production candidate
// -------------------------------------------------------------------------

struct ProductionCandidate {
    ProductionQueueItem item;
    float               score = 0.0f;
};

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
                           int32_t settlerCount,
                           int32_t militaryUnits,
                           float   treasury,
                           float   expansionOpportunity,
                           bool    expansionExhausted) {
    // Map is saturated: no viable city sites within scan radius.  Producing
    // another settler would strand it or force a disband.  Hard-zero the score
    // so the city picks military/infrastructure instead.
    if (expansionExhausted) { return 0.0f; }
    // expansion_need: desire falls from 1.0 (no cities) to 0.0 (at target)
    const aoc::sim::ai::UtilityConsideration expansionNeed{
        0.0f, static_cast<float>(targetCities),
        aoc::sim::ai::UtilityCurve::inverse()
    };
    // pop_ready: prefer pop >= 2 but allow pop 1 at reduced score
    const float popScore = (cityPop >= 2) ? 1.0f : 0.4f;

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
    // sharp boost to military score so production queue dominates with
    // military units. This is the "pump out tanks" mode for civs like
    // Tlatoani / Genghis. Without this they still build wonders/buildings
    // even when supposedly all-in on conquest.
    const float warmongerBoost = (behavior.militaryAggression >= 1.5f)
        ? 2.0f * behavior.militaryAggression
        : 1.0f;
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

static float scoreTrader(const LeaderBehavior& behavior,
                          bool    hasForeignTrade,
                          int32_t traderCount,
                          int32_t cityCount) {
    // has_trade_civic: hard prerequisite -- score is zero without it
    if (!hasForeignTrade) { return 0.0f; }

    // trade_need: want traders up to min(cityCount+1, 4). Cities=1 is
    // common early, so allow 2 traders even in a single-city empire so
    // trade routes actually form and tech diffusion has volume.
    const int32_t maxTraders = std::max(2, std::min(cityCount + 1, 4));
    const float tradeScore = (traderCount < maxTraders) ? 1.0f : 0.1f;

    // Lowered to 0.8 -- 1.2 caused 40+ traders per game, drowning out
    // military and builder production.  maxTraders already caps supply but
    // trader attrition kept traderCount below the cap, so the score stayed
    // at full 1.0 multiplier indefinitely.
    constexpr float BASE_WEIGHT = 0.8f;

    return BASE_WEIGHT * behavior.economicFocus * tradeScore;
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
                                       aoc::map::HexGrid& grid) {
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
    const int32_t ownedCityCount = gsPlayer->cityCount();

    const aoc::sim::CivId myCivId = gsPlayer->civId();
    const LeaderPersonalityDef& personality = leaderPersonality(myCivId);
    const AIScaledTargets targets = computeScaledTargets(personality.behavior);

    // Traders require Foreign Trade civic (CivicId{2})
    const bool hasForeignTrade = gsPlayer->civics().hasCompleted(CivicId{2});
    const bool playerHasCoins  = gsPlayer->monetary().totalCoinCount() > 0;

    const UnitTypeId bestMilitaryId  = bestAvailableMilitaryUnit(gameState, this->m_player);
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
        // Build the candidate list and score each option
        // ----------------------------------------------------------------

        std::vector<ProductionCandidate> candidates;
        candidates.reserve(32);

        // --- Settler ---
        if (ownedCityCount < targets.maxCities) {
            const float settlerScore = scoreSettler(
                personality.behavior,
                ownedCityCount,
                targets.maxCities,
                city.population(),
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
                candidate.item.name      = "Settler";
                candidate.item.totalCost = static_cast<float>(
                    unitTypeDef(UnitTypeId{3}).productionCost);
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
                candidate.item.name      = std::string(bestMilitaryDef.name);
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
                candidate.item.name      = "Builder";
                candidate.item.totalCost = static_cast<float>(
                    unitTypeDef(UnitTypeId{5}).productionCost);
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
                candidate.item.name      = "Scout";
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
                ownedCityCount
            );
            if (traderScore > 0.0f) {
                ProductionCandidate candidate{};
                candidate.item.type      = ProductionItemType::Unit;
                candidate.item.itemId    = 30u;
                candidate.item.name      = "Trader";
                candidate.item.totalCost = static_cast<float>(
                    unitTypeDef(UnitTypeId{30}).productionCost);
                candidate.item.progress  = 0.0f;
                candidate.score          = traderScore
                    * postureMultiplier(currentPosture,
                                        false, false, false, false, false, true);
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
                candidate.item.name      = "Supply Wagon";
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
            aiCtx.hasMint          = districts.hasBuilding(BuildingId{24});
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
                    candidate.item.name      = std::string(bdef.name);
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
            const float holySiteEraMult = std::clamp(1.0f + religionCoefNow, 0.2f, 1.8f);
            const std::array<DistrictOption, 7> districtOptions = {{
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
            }};

            for (const DistrictOption& opt : districtOptions) {
                if (opt.utilityScore <= 0.0f) { continue; }
                if (districts.hasDistrict(opt.type)) { continue; }

                ProductionCandidate candidate{};
                candidate.item.type      = ProductionItemType::District;
                candidate.item.itemId    = static_cast<uint16_t>(opt.type);
                candidate.item.name      = std::string(districtTypeName(opt.type));
                candidate.item.totalCost = opt.baseCost;
                candidate.item.progress  = 0.0f;
                candidate.score          = opt.utilityScore;
                candidates.push_back(std::move(candidate));
            }
        }

        // --- Mint priority ---
        // Capital must build a Mint before anything else when in Barter.
        // Settlers score ~2.14 in expansion phase, so Mint needs to score higher.
        // Score 4.0 ensures Mint is always first production in the capital.
        // BuildingId 24 = Mint. Only CityCenter district required (always present).
        if (city.isOriginalCapital()
            && !city.hasBuilding(BuildingId{24})
            && canBuildBuilding(gameState, this->m_player, city, BuildingId{24})
            && gsPlayer->monetary().system == MonetarySystemType::Barter) {
            ProductionCandidate candidate{};
            candidate.item.type      = ProductionItemType::Building;
            candidate.item.itemId    = 24u;
            candidate.item.name      = "Mint";
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
                candidate.item.name      = cp.name;
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
            candidate.item.name      = "Ancient Walls";
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
                    candidate.item.name      = std::string(spyDef.name);
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
                        candidate.item.name      = std::string(mdef.name);
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
                candidate.item.name      = std::string(wdef.name);
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
            // Absolute last resort: produce the best available military unit
            if (bestMilitaryId.isValid()) {
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

        // Gather all candidates within 10% of the top score
        std::vector<std::size_t> topIndices;
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
                    case ProductionItemType::Building: return aoc::core::ProductionItemKind::Building;
                    case ProductionItemType::District: return aoc::core::ProductionItemKind::District;
                    case ProductionItemType::Wonder:   return aoc::core::ProductionItemKind::Wonder;
                }
                return aoc::core::ProductionItemKind::Unknown;
            };

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

void AIController::executeDiplomacyActions(aoc::game::GameState& gameState,
                                            aoc::map::HexGrid& grid,
                                            DiplomacyManager& diplomacy,
                                            const Market& market,
                                            GlobalDealTracker* dealTracker) {
    constexpr uint8_t MAX_PLAYER_COUNT = 16;
    std::array<int32_t, MAX_PLAYER_COUNT> militaryCounts{};
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        const PlayerId pid = p->id();
        if (pid < MAX_PLAYER_COUNT) {
            militaryCounts[static_cast<std::size_t>(pid)] =
                static_cast<int32_t>(p->militaryUnitCount());
        }
    }

    const int32_t ourMilitary = militaryCounts[static_cast<std::size_t>(this->m_player)];
    const uint8_t playerCount = diplomacy.playerCount();

    // Leader personality drives war/peace thresholds.
    // Aggressive leaders (Montezuma: aggression=1.7, warThreshold=1.0) declare
    // war easily; peaceful leaders (Gandhi: aggression=0.2, warThreshold=5.0)
    // almost never do.
    const aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    const LeaderPersonalityDef& personality =
        leaderPersonality(gsPlayer != nullptr ? gsPlayer->civId() : CivId{0});
    const LeaderBehavior& beh = personality.behavior;

    for (uint8_t other = 0; other < playerCount; ++other) {
        if (other == this->m_player) { continue; }

        PairwiseRelation& rel = diplomacy.relation(this->m_player, other);

        // Cannot interact with players we haven't met yet
        if (!rel.hasMet) { continue; }

        const int32_t theirMilitary = militaryCounts[static_cast<std::size_t>(other)];
        const int32_t relationScore = rel.totalScore();

        if (rel.isAtWar) {
            // Peace threshold: leaders with high peaceAcceptanceThreshold accept
            // peace readily; grudge-holding leaders fight on even when outmatched.
            const float peaceMilRatio = (ourMilitary > 0)
                ? static_cast<float>(theirMilitary) / static_cast<float>(ourMilitary)
                : 10.0f;
            // Gandhi (peace=0.2, grudge=0.2) sues for peace at ratio ~1.3
            // Montezuma (peace=0.8, grudge=0.9) fights until ratio 2.5+
            // Base bumped 1.0 -> 1.5 so wars last long enough for nuke-tech
            // civs to actually find at-war targets. Short wars also made
            // secession, attrition, and spy missions under-trigger.
            const float peaceThreshold = 1.5f + beh.grudgeHolding - beh.peaceAcceptanceThreshold;
            if (peaceMilRatio > std::max(peaceThreshold, 0.8f)) {
                // War reparations: the weaker side (proposing peace) pays 10% of
                // their treasury to the stronger side. This makes war economically
                // meaningful — winning wars pays for the military investment.
                aoc::game::Player* loser = gameState.player(this->m_player);
                aoc::game::Player* winner = gameState.player(other);
                if (loser != nullptr && winner != nullptr && loser->treasury() > 0) {
                    const CurrencyAmount reparations = std::max(
                        static_cast<CurrencyAmount>(1),
                        loser->treasury() / 10);
                    loser->addGold(-reparations);
                    winner->addGold(reparations);
                    LOG_INFO("AI %u paid %lld gold in war reparations to player %u",
                             static_cast<unsigned>(this->m_player),
                             static_cast<long long>(reparations),
                             static_cast<unsigned>(other));
                }
                diplomacy.makePeace(this->m_player, other);
                LOG_INFO("AI %u Proposed peace with player %u (ratio %.2f > threshold %.2f)",
                         static_cast<unsigned>(this->m_player),
                         static_cast<unsigned>(other),
                         static_cast<double>(peaceMilRatio),
                         static_cast<double>(peaceThreshold));
            }
        } else {
            const bool easyAI = (this->m_difficulty == aoc::ui::AIDifficulty::Easy);
            const bool hardAI = (this->m_difficulty == aoc::ui::AIDifficulty::Hard);

            // War declaration threshold: personality-driven.
            // Low warDeclarationThreshold (Montezuma=1.0) = easy to trigger war.
            // High warDeclarationThreshold (Gandhi=5.0) = almost never declares war.
            // militaryAggression lowers the required advantage, but always needs
            // at least 1.2:1 even for the most aggressive leaders.
            // Gandhi (0.2): needs 1.5/sqrt(0.2)=3.35:1 -- almost never.
            // Montezuma (1.7): needs 1.5/sqrt(1.7)=1.15:1 -- at slight advantage.
            // Frederick (1.5): needs 1.5/sqrt(1.5)=1.22:1.
            const float baseMilRatio = hardAI ? 1.15f : 1.3f;
            const float milRatioThreshold = std::max(1.1f,
                baseMilRatio / std::sqrt(std::max(beh.militaryAggression, 0.1f)));
            // Relation threshold: aggressive leaders tolerate worse relations less.
            // Gandhi needs relations below -100; Montezuma triggers at -30.
            const int32_t baseRelThreshold = hardAI ? -5 : -15;
            const int32_t relationThreshold = static_cast<int32_t>(
                static_cast<float>(baseRelThreshold)
                / std::sqrt(std::max(beh.militaryAggression, 0.1f)));
            // War chance per turn: tuned down so 1000-turn games don't see
            // ~140 declarations. Montezuma: 2 * 1.7 = 3.4 (~34%/elig turn);
            // Gandhi still ~0.
            const int32_t baseWarChance = hardAI ? 3 : 2;
            const int32_t warChanceThreshold = static_cast<int32_t>(
                static_cast<float>(baseWarChance) * beh.militaryAggression);

            // Peace cooldown: cannot re-declare war within 40 turns of a peace
            // treaty. Was 15 — too short for 1000-turn games (allowed up to
            // ~66 wars between same pair).
            constexpr int32_t WAR_COOLDOWN_TURNS = 40;
            if (rel.turnsSincePeace < WAR_COOLDOWN_TURNS) { continue; }

            // Periphery gate: refuse wars against civs beyond our projection
            // range. Low-peripheryTolerance leaders (isolationists) restrict
            // themselves to wars with near neighbours; high-tolerance
            // (colonial) leaders happily declare across oceans.
            //
            // Range scales 10..30 hexes by gene (baseline 20 at 1.0).
            {
                const int32_t maxWarRange = static_cast<int32_t>(std::clamp(
                    20.0f * beh.peripheryTolerance, 10.0f, 30.0f));
                const aoc::game::Player* ourPlayerPtr   = gameState.player(this->m_player);
                const aoc::game::Player* theirPlayerPtr = gameState.player(other);
                if (ourPlayerPtr == nullptr || theirPlayerPtr == nullptr) { continue; }
                if (ourPlayerPtr->cities().empty() || theirPlayerPtr->cities().empty()) {
                    continue;
                }
                int32_t closestCityDist = std::numeric_limits<int32_t>::max();
                for (const std::unique_ptr<aoc::game::City>& ourCity : ourPlayerPtr->cities()) {
                    for (const std::unique_ptr<aoc::game::City>& theirCity : theirPlayerPtr->cities()) {
                        const int32_t d = aoc::hex::distance(
                            ourCity->location(), theirCity->location());
                        if (d < closestCityDist) { closestCityDist = d; }
                    }
                }
                if (closestCityDist > maxWarRange) {
                    continue;  // Too far -- would overextend supply + yield nothing.
                }
            }

            // Standard war declaration: military advantage + strained relations.
            if (!easyAI && !rel.isAtWar && ourMilitary > 0 && theirMilitary >= 0 &&
                static_cast<float>(ourMilitary) >
                    milRatioThreshold * static_cast<float>(std::max(1, theirMilitary)) &&
                relationScore < relationThreshold) {
                const int32_t warChance =
                    ((ourMilitary * 7 + theirMilitary * 13 +
                      static_cast<int32_t>(this->m_player) * 31 +
                      gameState.currentTurn() * 53 +
                      static_cast<int32_t>(other) * 71) % 100);
                if (warChance < warChanceThreshold) {
                    diplomacy.declareWar(this->m_player, other,
                                         rel.casusBelliGranted()
                                             ? aoc::sim::CasusBelliType::FormalWar
                                             : aoc::sim::CasusBelliType::SurpriseWar,
                                         nullptr, &gameState,
                                         gameState.currentTurn());
                    LOG_INFO("AI %u Declared war on player %u (military %d vs %d, "
                             "relations %d, aggression %.2f)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             ourMilitary, theirMilitary, relationScore,
                             static_cast<double>(beh.militaryAggression));
                }
            }

            // Opportunistic war: personality modulates the threshold.
            // Aggressive leaders (aggression > 1.5) attack at ~1.5:1 ratio.
            // Peaceful leaders (aggression < 0.5) never attack opportunistically.
            if (beh.militaryAggression < 0.5f) {
                // Peaceful leaders skip opportunistic wars entirely.
            } else {
                const float oppoRatioThreshold = std::max(1.3f,
                    2.0f / std::sqrt(beh.militaryAggression));
                const int32_t oppoMinUnits = 3;
                if (!easyAI && !rel.isAtWar && ourMilitary >= oppoMinUnits &&
                    static_cast<float>(ourMilitary) >=
                        oppoRatioThreshold * static_cast<float>(std::max(1, theirMilitary))) {
                    const int32_t warChance =
                        ((ourMilitary * 11 + theirMilitary * 17 +
                          static_cast<int32_t>(this->m_player) * 37 +
                          gameState.currentTurn() * 59 +
                          static_cast<int32_t>(other) * 73) % 100);
                    const int32_t threshold = hardAI ? 3 : 2;
                    if (warChance < threshold) {
                        diplomacy.declareWar(this->m_player, other,
                                             aoc::sim::CasusBelliType::SurpriseWar,
                                             nullptr, &gameState,
                                             gameState.currentTurn());
                        LOG_INFO("AI %u Declared opportunistic war on player %u "
                                 "(%.1f:1 advantage: %d vs %d, aggression %.2f)",
                                 static_cast<unsigned>(this->m_player),
                                 static_cast<unsigned>(other),
                                 static_cast<double>(
                                     static_cast<float>(ourMilitary) /
                                     static_cast<float>(std::max(1, theirMilitary))),
                                 ourMilitary, theirMilitary,
                                 static_cast<double>(beh.militaryAggression));
                    }
                }
            }

            if (!rel.hasOpenBorders && relationScore > 10) {
                diplomacy.grantOpenBorders(this->m_player, other);
                LOG_INFO("AI %u Opened borders with player %u (relations %d)",
                         static_cast<unsigned>(this->m_player),
                         static_cast<unsigned>(other), relationScore);
            }

            // H6.4: alliance formation is now personality-gated. Without this,
            // every AI with relations > 20 plus 3 complementary goods formed an
            // economic alliance on turn 1, flattening behavioral distinction.
            // Common gate: alliance desire / diplomatic openness above 0.5.
            // Per-type gate: matching focus > 0.5 so a warmonger doesn't chase
            // a cultural alliance and a zealot doesn't chase a research one.
            const bool openToAlliance =
                beh.allianceDesire > 0.5f && beh.diplomaticOpenness > 0.5f;

            if (openToAlliance && !rel.hasEconomicAlliance && relationScore > 20
                && beh.economicFocus > 0.5f) {
                int32_t complementaryGoods = 0;
                const uint16_t totalGoods = market.goodsCount();
                for (uint16_t g = 0; g < totalGoods; ++g) {
                    const int32_t currentPrice = market.price(g);
                    const int32_t basePrice = goodDef(g).basePrice;
                    if (basePrice > 0) {
                        const float priceRatio = static_cast<float>(currentPrice) /
                                                 static_cast<float>(basePrice);
                        if (priceRatio > 1.3f || priceRatio < 0.7f) {
                            ++complementaryGoods;
                        }
                    }
                }
                if (complementaryGoods >= 3) {
                    const aoc::ErrorCode ec = diplomacy.formEconomicAlliance(
                        this->m_player, other, gameState.currentTurn());
                    if (ec == aoc::ErrorCode::Ok) {
                        LOG_INFO("AI %u Formed economic alliance with player %u "
                                 "(relations %d, %d complementary goods)",
                                 static_cast<unsigned>(this->m_player),
                                 static_cast<unsigned>(other),
                                 relationScore, complementaryGoods);
                    }
                }
            }

            // H6.4: research agreement — science-focused leaders at warm relations.
            if (openToAlliance && !rel.hasResearchAgreement && relationScore > 25
                && beh.scienceFocus > 0.8f) {
                const aoc::ErrorCode ec = diplomacy.formResearchAgreement(
                    this->m_player, other, gameState.currentTurn());
                if (ec == aoc::ErrorCode::Ok) {
                    LOG_INFO("AI %u Formed research agreement with player %u "
                             "(relations %d, scienceFocus %.2f)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             relationScore,
                             static_cast<double>(beh.scienceFocus));
                }
            }

            // H6.4: military alliance — requires aggressive or defensive profile
            // AND strong trust. Warmongers seek allies; peaceniks don't.
            if (openToAlliance && !rel.hasMilitaryAlliance && relationScore > 35
                && beh.militaryAggression > 0.8f) {
                const aoc::ErrorCode ec = diplomacy.formMilitaryAlliance(
                    this->m_player, other, gameState.currentTurn());
                if (ec == aoc::ErrorCode::Ok) {
                    LOG_INFO("AI %u Formed military alliance with player %u "
                             "(relations %d, aggression %.2f)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             relationScore,
                             static_cast<double>(beh.militaryAggression));
                }
            }

            // Defensive alliance — low-aggression / diplomatic profiles that
            // want the war-deterrent of mutual defense without the force
            // projection of a full military alliance. A lower aggression
            // gate (< 0.4) complements the militaryAggression > 0.8 path
            // above, so warmongers and peaceniks pick different alliance
            // types instead of competing for the same slot.
            if (openToAlliance && !rel.hasDefensiveAlliance
                && !rel.hasMilitaryAlliance && relationScore > 30
                && beh.militaryAggression < 0.4f
                && beh.diplomaticOpenness > 0.7f) {
                const aoc::ErrorCode ec = diplomacy.formDefensiveAlliance(
                    this->m_player, other, gameState.currentTurn());
                if (ec == aoc::ErrorCode::Ok) {
                    LOG_INFO("AI %u Formed defensive alliance with player %u "
                             "(relations %d, aggression %.2f, openness %.2f)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             relationScore,
                             static_cast<double>(beh.militaryAggression),
                             static_cast<double>(beh.diplomaticOpenness));
                }
            }

            // H6.4: cultural alliance — culture-focused leaders.
            if (openToAlliance && !rel.hasCulturalAlliance && relationScore > 25
                && beh.cultureFocus > 0.8f) {
                const aoc::ErrorCode ec = diplomacy.formCulturalAlliance(
                    this->m_player, other, gameState.currentTurn());
                if (ec == aoc::ErrorCode::Ok) {
                    LOG_INFO("AI %u Formed cultural alliance with player %u "
                             "(relations %d, cultureFocus %.2f)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             relationScore,
                             static_cast<double>(beh.cultureFocus));
                }
            }

            // H6.4: religious alliance — religious-zealot leaders only.
            if (openToAlliance && !rel.hasReligiousAlliance && relationScore > 25
                && beh.religiousZeal > 0.8f) {
                const aoc::ErrorCode ec = diplomacy.formReligiousAlliance(
                    this->m_player, other, gameState.currentTurn());
                if (ec == aoc::ErrorCode::Ok) {
                    LOG_INFO("AI %u Formed religious alliance with player %u "
                             "(relations %d, religiousZeal %.2f)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             relationScore,
                             static_cast<double>(beh.religiousZeal));
                }
            }

            // ----------------------------------------------------------------
            // Bilateral trade deal proposal (AI-only path).
            // ----------------------------------------------------------------
            // Lighter commitment than an alliance: -20% tariffs and an auto
            // spawned Trader every 5 turns along the pair. Fire when relations
            // are warm and we do not already share one. Uses the same partner
            // loop, so every AI reconsiders each turn.
            if (relationScore > 15) {
                aoc::game::Player* selfPlayer = gameState.player(this->m_player);
                bool alreadyPaired = false;
                if (selfPlayer != nullptr) {
                    for (const aoc::sim::TradeAgreementDef& agr :
                         selfPlayer->tradeAgreements().agreements) {
                        if (!agr.isActive) { continue; }
                        if (agr.type != aoc::sim::TradeAgreementType::BilateralDeal) { continue; }
                        for (PlayerId m : agr.members) {
                            if (m == other) { alreadyPaired = true; break; }
                        }
                        if (alreadyPaired) { break; }
                    }
                }
                if (!alreadyPaired) {
                    const ErrorCode rc = aoc::sim::proposeBilateralDeal(
                        gameState, this->m_player, other);
                    if (rc == ErrorCode::Ok) {
                        LOG_INFO("AI %u proposed bilateral trade deal with player %u "
                                 "(relations %d)",
                                 static_cast<unsigned>(this->m_player),
                                 static_cast<unsigned>(other), relationScore);
                    }
                }
            }

            // ----------------------------------------------------------------
            // Loan offers / requests (IOUContract path).
            // ----------------------------------------------------------------
            // Flush AIs lend to friendly partners who are short on cash. The
            // processIOUPayments turn tick amortises the loan with interest,
            // which is Civ 6's "gold per turn" mechanic.
            {
                aoc::game::Player* selfPlayer = gameState.player(this->m_player);
                aoc::game::Player* partnerPlayer = gameState.player(other);
                if (selfPlayer != nullptr && partnerPlayer != nullptr
                    && relationScore > 10) {
                    const CurrencyAmount myTreas = selfPlayer->treasury();
                    const CurrencyAmount theirTreas = partnerPlayer->treasury();
                    // Do not stack too many loans with the same partner.
                    int32_t existingWithPartner = 0;
                    for (const aoc::sim::IOUContract& c : selfPlayer->ious().loansGiven) {
                        if (c.debtor == other && c.remaining > 0) { ++existingWithPartner; }
                    }
                    // Treasury scale in-sim is ~0-2000 most of the game.
                    // Flush: at least 4x the partner's shortfall and > 300 floor.
                    const bool flush = myTreas > 300 && myTreas > theirTreas * 4;
                    const bool partnerBroke = theirTreas < 50;
                    if (flush && partnerBroke && existingWithPartner < 2) {
                        // Lend up to 25% of our treasury, capped at 500.
                        // 8% per-turn interest, 15-turn term.
                        const CurrencyAmount principal = std::min(
                            myTreas / 4,
                            static_cast<CurrencyAmount>(500));
                        if (principal > 50) {
                            ErrorCode rc = aoc::sim::createIOU(
                                gameState, this->m_player, other,
                                principal, 0.08f, 15);
                            if (rc == ErrorCode::Ok) {
                                LOG_INFO("AI %u offered loan to player %u: %d gold @ 8%% for 15 turns (relation %d)",
                                         static_cast<unsigned>(this->m_player),
                                         static_cast<unsigned>(other),
                                         static_cast<int>(principal),
                                         relationScore);
                            }
                        }
                    }
                }
            }

            // ----------------------------------------------------------------
            // City sale (CedeCity for GoldLump).
            // ----------------------------------------------------------------
            // A civ deep in debt with a spare city proposes: "Take this city,
            // here's the transfer; pay me X gold." Fires every ~30 turns per
            // pair. Requires warm relations and a willing buyer.
            if (dealTracker != nullptr && relationScore > 15) {
                const int32_t curTurn = gameState.currentTurn();
                const int32_t saleTick = curTurn + this->m_player * 7 + other * 11;
                if (saleTick % 25 == 0) {
                    aoc::game::Player* seller = gameState.player(this->m_player);
                    aoc::game::Player* buyer  = gameState.player(other);
                    // Empire trimming: 5+ cities OR broke with 3+ cities.
                    const std::size_t numCities = seller != nullptr ? seller->cities().size() : 0;
                    const CurrencyAmount sellerGold = seller != nullptr ? seller->treasury() : 0;
                    const bool surplus = numCities >= 5;
                    const bool broke   = numCities >= 3 && sellerGold < 100;
                    if (seller != nullptr && buyer != nullptr && (surplus || broke)) {
                        // Pick the smallest city (cheapest to part with).
                        aoc::game::City* victim = nullptr;
                        int32_t smallestPop = INT32_MAX;
                        for (const std::unique_ptr<aoc::game::City>& c : seller->cities()) {
                            if (c->population() < smallestPop) {
                                smallestPop = c->population();
                                victim = c.get();
                            }
                        }
                        const int32_t price = 200 + smallestPop * 50;
                        if (victim != nullptr && buyer->treasury() > price + 100) {
                            const aoc::hex::AxialCoord loc = victim->location();

                            DiplomaticDeal deal{};
                            deal.playerA = this->m_player;
                            deal.playerB = other;
                            deal.turnsRemaining = 0;

                            DealTerm cede{};
                            cede.type = DealTermType::CedeCity;
                            cede.fromPlayer = this->m_player;
                            cede.toPlayer   = other;
                            cede.tileCoord  = loc;
                            deal.terms.push_back(cede);

                            DealTerm payment{};
                            payment.type = DealTermType::GoldLump;
                            payment.fromPlayer = other;
                            payment.toPlayer   = this->m_player;
                            payment.goldLump   = price;
                            deal.terms.push_back(payment);

                            const std::size_t dealIdx = dealTracker->activeDeals.size();
                            ErrorCode rcP = aoc::sim::proposeDeal(gameState, *dealTracker, deal);
                            if (rcP == ErrorCode::Ok) {
                                ErrorCode rcA = aoc::sim::acceptDeal(
                                    gameState, grid, *dealTracker,
                                    static_cast<int32_t>(dealIdx));
                                if (rcA == ErrorCode::Ok) {
                                    LOG_INFO("AI %u sold city %s to player %u for %d gold (relation %d)",
                                             static_cast<unsigned>(this->m_player),
                                             victim->name().c_str(),
                                             static_cast<unsigned>(other),
                                             price, relationScore);
                                }
                            }
                        }
                    }
                }
            }

            // ----------------------------------------------------------------
            // Border tile sale (CedeTile for GoldLump).
            // ----------------------------------------------------------------
            // Transfer one of our hexes that is adjacent to a neighbour's
            // territory. Small price, ~every 20 turns per pair.
            if (dealTracker != nullptr && relationScore > 15) {
                const int32_t curTurn = gameState.currentTurn();
                const int32_t tileTick = curTurn + this->m_player * 3 + other * 5;
                if (tileTick % 60 == 0) {
                    aoc::game::Player* seller = gameState.player(this->m_player);
                    aoc::game::Player* buyer  = gameState.player(other);
                    // Only sell if seller is short on gold: avoids border thrash
                    // where both sides keep re-buying from each other.
                    if (seller != nullptr && buyer != nullptr
                        && seller->treasury() < 300
                        && buyer->treasury() > 150) {
                        // Scan grid for a self-owned tile adjacent to `other`.
                        const int32_t tileCount = grid.tileCount();
                        aoc::hex::AxialCoord foundTile{0, 0};
                        bool have = false;
                        for (int32_t idx = 0; idx < tileCount && !have; ++idx) {
                            if (grid.owner(idx) != this->m_player) { continue; }
                            const aoc::hex::AxialCoord c = grid.toAxial(idx);
                            const std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(c);
                            for (const aoc::hex::AxialCoord& n : nbrs) {
                                const int32_t nIdx = grid.toIndex(n);
                                if (nIdx < 0 || nIdx >= tileCount) { continue; }
                                if (grid.owner(nIdx) == other) {
                                    foundTile = c;
                                    have = true;
                                    break;
                                }
                            }
                        }
                        if (have) {
                            DiplomaticDeal deal{};
                            deal.playerA = this->m_player;
                            deal.playerB = other;
                            deal.turnsRemaining = 0;

                            DealTerm cede{};
                            cede.type = DealTermType::CedeTile;
                            cede.fromPlayer = this->m_player;
                            cede.toPlayer   = other;
                            cede.tileCoord  = foundTile;
                            deal.terms.push_back(cede);

                            DealTerm payment{};
                            payment.type = DealTermType::GoldLump;
                            payment.fromPlayer = other;
                            payment.toPlayer   = this->m_player;
                            payment.goldLump   = 100;
                            deal.terms.push_back(payment);

                            const std::size_t dealIdx = dealTracker->activeDeals.size();
                            ErrorCode rcP = aoc::sim::proposeDeal(gameState, *dealTracker, deal);
                            if (rcP == ErrorCode::Ok) {
                                ErrorCode rcA = aoc::sim::acceptDeal(
                                    gameState, grid, *dealTracker,
                                    static_cast<int32_t>(dealIdx));
                                if (rcA == ErrorCode::Ok) {
                                    LOG_INFO("AI %u ceded tile (%d,%d) to player %u for 100 gold (relation %d)",
                                             static_cast<unsigned>(this->m_player),
                                             foundTile.q, foundTile.r,
                                             static_cast<unsigned>(other),
                                             relationScore);
                                }
                            }
                        }
                    }
                }
            }

            // ----------------------------------------------------------------
            // Border violation response (personality-driven)
            // ----------------------------------------------------------------
            // Check if 'other' has units in OUR territory (we are territory owner).
            const PairwiseRelation& violatorRel = diplomacy.relation(other, this->m_player);
            if (violatorRel.unitsInTerritory > 0 && violatorRel.turnsWithViolation > 0) {
                // Tolerance thresholds vary by aggression:
                //   Aggressive (>1.2): 0-2 turns tolerance -> ultimatum -> war
                //   Defensive (0.6-1.2): 3-5 turns -> demand withdrawal, fortify
                //   Diplomatic (0.3-0.6): 5-10 turns -> protest, seek allies
                //   Passive (<0.3): 10+ turns -> complain but tolerate
                int32_t baseTolerance = 5;
                if (beh.militaryAggression > 1.2f) {
                    baseTolerance = 2;
                } else if (beh.militaryAggression > 0.6f) {
                    baseTolerance = 4;
                } else if (beh.militaryAggression > 0.3f) {
                    baseTolerance = 8;
                } else {
                    baseTolerance = 12;
                }

                // Power ratio modulates tolerance: if violator is 2x+ stronger,
                // double tolerance. Small nations endure what they must.
                const float powerRatio = (ourMilitary > 0)
                    ? static_cast<float>(theirMilitary) / static_cast<float>(ourMilitary)
                    : 10.0f;
                if (powerRatio > 2.0f) {
                    baseTolerance *= 2;
                }

                const int32_t violationTurns = violatorRel.turnsWithViolation;

                if (violationTurns > baseTolerance && violatorRel.casusBelliGranted()) {
                    // Beyond tolerance and casus belli granted: declare war
                    // (if not already at war and we have military capability)
                    if (ourMilitary > 0 && beh.militaryAggression > 0.3f) {
                        diplomacy.declareWar(this->m_player, other,
                                             aoc::sim::CasusBelliType::FormalWar,
                                             nullptr, &gameState,
                                             gameState.currentTurn());
                        LOG_INFO("AI %u Declared war on Player %u for border violation "
                                 "(%d turns, tolerance %d, aggression %.2f)",
                                 static_cast<unsigned>(this->m_player),
                                 static_cast<unsigned>(other),
                                 violationTurns, baseTolerance,
                                 static_cast<double>(beh.militaryAggression));
                    }
                } else if (violationTurns > baseTolerance / 2) {
                    // Past half-tolerance: add relation penalty
                    diplomacy.addModifier(this->m_player, other,
                        {"Troops in our territory", -5, 10});
                }

                // Set higher toll rates against violators
                aoc::game::Player* ourPlayer = gameState.player(this->m_player);
                if (ourPlayer != nullptr) {
                    float violatorToll = 0.25f + beh.militaryAggression * 0.10f;
                    violatorToll = std::min(violatorToll, 0.50f);
                    ourPlayer->tariffs().perPlayerTollRates[other] = violatorToll;
                }
            }

            // ----------------------------------------------------------------
            // Hostile economic actions: resource embargo + bond dump.
            // ----------------------------------------------------------------
            // Fire when war is declared OR relations are deeply negative.
            // These are one-shots (per turn per pair), not continuous, and
            // gated on the reputation/aggression personality to keep
            // peaceful AIs from torching economic ties.
            if ((rel.isAtWar || relationScore < -40) && beh.militaryAggression > 0.5f) {
                // Resource embargo: blanket the largest export flow to
                // this rival. MVP picks the first good we produce that
                // they are price-dependent on (market price > 1.2x base).
                aoc::game::Player* selfP = gameState.player(this->m_player);
                if (selfP != nullptr) {
                    const uint16_t totalGoods = market.goodsCount();
                    uint16_t targetGood = 0xFFFF;
                    for (uint16_t g = 0; g < totalGoods; ++g) {
                        if (diplomacy.hasResourceEmbargo(this->m_player, other, g)) { continue; }
                        // We-hold check: at least one of our cities has
                        // a non-trivial stockpile of this good (proxy
                        // for "we could deny it to them").
                        bool weHold = false;
                        for (const std::unique_ptr<aoc::game::City>& c : selfP->cities()) {
                            if (c != nullptr && c->stockpile().getAmount(g) >= 10) {
                                weHold = true; break;
                            }
                        }
                        if (!weHold) { continue; }
                        const int32_t currentPrice = market.price(g);
                        const int32_t basePrice    = goodDef(g).basePrice;
                        if (basePrice <= 0) { continue; }
                        const float ratio =
                            static_cast<float>(currentPrice) / static_cast<float>(basePrice);
                        if (ratio > 1.2f) {
                            targetGood = g;
                            break;
                        }
                    }
                    if (targetGood != 0xFFFF) {
                        diplomacy.setResourceEmbargo(this->m_player, other, targetGood, true);
                        LOG_INFO("AI %u imposed resource embargo on player %u (good %u, relation %d)",
                                 static_cast<unsigned>(this->m_player),
                                 static_cast<unsigned>(other),
                                 static_cast<unsigned>(targetGood), relationScore);
                    }
                }

                // Bond dump: if we hold bonds issued by the rival, sell
                // them all to spike their yields. Fires on the turn war
                // is declared / relations cross the threshold; the
                // dumpBonds implementation itself handles single-shot
                // semantics when no bonds are held.
                if (selfP != nullptr) {
                    CurrencyAmount held = selfP->bonds().bondsHeldFrom(other);
                    if (held > 0) {
                        ErrorCode bc = aoc::sim::dumpBonds(gameState, this->m_player, other);
                        if (bc == ErrorCode::Ok) {
                            LOG_INFO("AI %u dumped %lld bond debt of player %u (relation %d)",
                                     static_cast<unsigned>(this->m_player),
                                     static_cast<long long>(held),
                                     static_cast<unsigned>(other), relationScore);
                        }
                    }
                }
            }

            // ----------------------------------------------------------------
            // AI toll rate management (based on relation + reputation)
            // ----------------------------------------------------------------
            {
                aoc::game::Player* ourPlayer = gameState.player(this->m_player);
                if (ourPlayer != nullptr && violatorRel.unitsInTerritory == 0) {
                    const int32_t repScore = rel.reputationScore();
                    float tollRate = 0.10f;  // Neutral default
                    if (relationScore > 10) {
                        tollRate = 0.05f;    // Friendly
                    }
                    if (relationScore > 40 || rel.hasDefensiveAlliance) {
                        tollRate = 0.0f;     // Allied
                    }
                    if (relationScore < -10) {
                        tollRate = 0.20f;    // Unfriendly
                    }
                    if (relationScore < -40) {
                        tollRate = 0.35f + beh.militaryAggression * 0.05f;
                        tollRate = std::min(tollRate, 0.50f);  // Hostile
                    }
                    // Reputation modulates: untrustworthy players pay more
                    if (repScore < -20) {
                        tollRate += 0.05f;
                        tollRate = std::min(tollRate, 0.50f);
                    }
                    // C25: reciprocal tariff — mirror partner's rate if they
                    // chose a higher one. Without this, trade wars are
                    // one-sided: AI A hikes tolls, AI B keeps being nice.
                    // Add a small premium above their rate so retaliation is
                    // visible and converges to escalation signal.
                    const aoc::game::Player* theirPlayer = gameState.player(other);
                    if (theirPlayer != nullptr) {
                        std::unordered_map<PlayerId, float>::const_iterator mirrorIt =
                            theirPlayer->tariffs().perPlayerTollRates.find(this->m_player);
                        if (mirrorIt != theirPlayer->tariffs().perPlayerTollRates.end()
                            && mirrorIt->second > tollRate + 0.05f) {
                            tollRate = std::min(0.50f, mirrorIt->second + 0.02f);
                        }
                    }
                    ourPlayer->tariffs().perPlayerTollRates[other] = tollRate;

                    // Canal toll: premium pricing for canal transit.
                    // Economically-focused AIs charge more (canals are investments),
                    // allies get discounts, hostiles pay maximum.
                    float canalToll = 0.20f + beh.economicFocus * 0.05f;
                    if (relationScore > 10) {
                        canalToll = 0.10f;   // Friendly discount
                    }
                    if (relationScore > 40 || rel.hasDefensiveAlliance) {
                        canalToll = 0.05f;   // Allied: near-free access
                    }
                    if (relationScore < -10) {
                        canalToll = 0.30f + beh.economicFocus * 0.05f;
                    }
                    if (relationScore < -40) {
                        canalToll = 0.45f;   // Hostile: near-maximum
                    }
                    canalToll = std::min(canalToll, 0.50f);
                    ourPlayer->tariffs().perPlayerCanalTollRates[other] = canalToll;
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Free-trade-zone / customs-union formation.
    // ------------------------------------------------------------------
    // Warm, open-trading AIs gather 2+ economic partners into a free
    // trade zone (-50% tariff) or customs union (common external
    // tariff). Gate on economicFocus and diplomaticOpenness; rate-limit
    // to one attempt every ~50 turns per AI.
    {
        const aoc::game::Player* me = gameState.player(this->m_player);
        const bool openTrader =
            beh.economicFocus > 0.6f && beh.diplomaticOpenness > 0.5f;
        const int32_t ftzTick =
            gameState.currentTurn() + static_cast<int32_t>(this->m_player) * 11;
        if (me != nullptr && openTrader && (ftzTick % 50 == 0)) {
            std::vector<PlayerId> members;
            members.push_back(this->m_player);
            for (uint8_t other = 0; other < playerCount; ++other) {
                if (other == this->m_player) { continue; }
                const aoc::game::Player* o = gameState.player(other);
                if (o == nullptr) { continue; }
                if (o->victoryTracker().isEliminated) { continue; }
                const PairwiseRelation& r = diplomacy.relation(this->m_player, other);
                if (!r.hasMet || r.isAtWar) { continue; }
                if (r.totalScore() < 15) { continue; }
                // Skip if already in any FTZ/customs with us.
                bool alreadyInBloc = false;
                for (const aoc::sim::TradeAgreementDef& agr : me->tradeAgreements().agreements) {
                    if (!agr.isActive) { continue; }
                    if (agr.type == aoc::sim::TradeAgreementType::BilateralDeal) { continue; }
                    for (PlayerId mem : agr.members) {
                        if (mem == other) { alreadyInBloc = true; break; }
                    }
                    if (alreadyInBloc) { break; }
                }
                if (alreadyInBloc) { continue; }
                members.push_back(other);
                if (members.size() >= 4) { break; }  // Keep blocs tractable.
            }
            if (members.size() >= 3) {
                ErrorCode ec = ErrorCode::InvalidArgument;
                const char* kind = "FTZ";
                // Protectionist + high-aggression personalities prefer a
                // customs union (projects power via common tariff).
                // Everyone else forms a softer FTZ.
                if (beh.militaryAggression > 0.8f) {
                    ec = aoc::sim::formCustomsUnion(gameState, members, 0.15f);
                    kind = "Customs Union";
                } else {
                    ec = aoc::sim::createFreeTradeZone(gameState, members);
                }
                if (ec == ErrorCode::Ok) {
                    LOG_INFO("AI %u formed %s with %zu members (economicFocus %.2f)",
                             static_cast<unsigned>(this->m_player), kind,
                             members.size(),
                             static_cast<double>(beh.economicFocus));
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Electricity import proposals.
    // ------------------------------------------------------------------
    // If our total district power demand exceeds domestic supply, try to
    // buy the deficit from a neighbour whose supply comfortably exceeds
    // their own demand. Per-city 40% import cap in computeCityPower
    // keeps this a complement, not a crutch. Firing limited to once per
    // ~25 turns per AI to avoid hammering the agreement list.
    {
        const aoc::game::Player* me = gameState.player(this->m_player);
        const int32_t elecTick =
            gameState.currentTurn() + static_cast<int32_t>(this->m_player) * 3;
        const bool industrial = (me != nullptr && aoc::sim::effectiveEraFromTech(*me).value >= 4);
        if (industrial && (elecTick % 25 == 0)) {
            // Crude per-player energy balance — supply = sum of
            // power-plant building outputs regardless of fuel gating (the
            // tick on processElectricityAgreements uses lastDelivered so
            // this over-estimates at worst, which is fine for gating).
            auto energyBalance = [&](const aoc::game::Player* p) -> std::pair<int32_t,int32_t> {
                int32_t sup = 0;
                int32_t dem = 0;
                if (p == nullptr) { return {0, 0}; }
                for (const std::unique_ptr<aoc::game::City>& c : p->cities()) {
                    if (c == nullptr) { continue; }
                    for (const CityDistrictsComponent::PlacedDistrict& d
                             : c->districts().districts) {
                        for (BuildingId bid : d.buildings) {
                            dem += buildingEnergyDemand(bid);
                            for (const PowerPlantDef& pd : POWER_PLANT_DEFS) {
                                if (pd.buildingId == bid) {
                                    sup += pd.energyOutput;
                                    break;
                                }
                            }
                        }
                    }
                }
                return {sup, dem};
            };

            auto [mySup, myDem] = energyBalance(me);
            const int32_t myDeficit = myDem - mySup;
            if (myDeficit > 0) {
                // Find best seller candidate: largest positive surplus,
                // met, not at war, no existing agreement in this direction.
                PlayerId bestSeller = INVALID_PLAYER;
                int32_t  bestSurplus = 0;
                for (uint8_t other = 0; other < playerCount; ++other) {
                    if (other == this->m_player) { continue; }
                    const aoc::game::Player* o = gameState.player(other);
                    if (o == nullptr) { continue; }
                    if (o->victoryTracker().isEliminated) { continue; }
                    const PairwiseRelation& r = diplomacy.relation(this->m_player, other);
                    if (!r.hasMet || r.isAtWar) { continue; }
                    if (r.totalScore() < 0) { continue; }  // Hostile sellers refuse

                    bool duplicateDir = false;
                    for (const aoc::sim::ElectricityAgreementComponent& a
                             : gameState.electricityAgreements()) {
                        if (a.isActive && a.buyer == this->m_player && a.seller == other) {
                            duplicateDir = true; break;
                        }
                    }
                    if (duplicateDir) { continue; }

                    auto [oSup, oDem] = energyBalance(o);
                    const int32_t surplus = oSup - oDem;
                    if (surplus > bestSurplus) {
                        bestSurplus = surplus;
                        bestSeller  = other;
                    }
                }

                if (bestSeller != INVALID_PLAYER && bestSurplus > 0) {
                    // Cover the deficit but no more than the seller's
                    // surplus. Gold: 2 gold per MW per turn — MVP price
                    // anchor; market tuning can come later.
                    const int32_t mw = std::min(myDeficit, bestSurplus);
                    const int32_t goldPerTurn = std::max(1, mw * 2);
                    const int32_t duration    = 30;
                    const aoc::ErrorCode ec = aoc::sim::proposeElectricityImport(
                        gameState, this->m_player, bestSeller,
                        mw, goldPerTurn, gameState.currentTurn(), duration);
                    if (ec == aoc::ErrorCode::Ok) {
                        LOG_INFO("AI %u bought %d MW electricity from player %u "
                                 "(deficit %d, surplus %d, %d gold/turn, %d turns)",
                                 static_cast<unsigned>(this->m_player),
                                 mw, static_cast<unsigned>(bestSeller),
                                 myDeficit, bestSurplus, goldPerTurn, duration);
                    }
                }
            }
        }
    }

    // City-state interactions: bully when desperate for gold + aggressive,
    // levy when suzerain and at war. Rate-limited by CS cooldowns.
    {
        const aoc::game::Player* me = gameState.player(this->m_player);
        if (me != nullptr) {
            const CurrencyAmount treasury = me->treasury();
            bool atWar = false;
            for (uint8_t other = 0; other < playerCount; ++other) {
                if (other == this->m_player) { continue; }
                if (diplomacy.relation(this->m_player, other).isAtWar) {
                    atWar = true; break;
                }
            }
            auto& cityStates = gameState.cityStates();
            for (std::size_t i = 0; i < cityStates.size(); ++i) {
                CityStateComponent& cs = cityStates[i];
                if (!cs.hasMet(this->m_player)) { continue; }

                // Bully: only if we have no envoys stake and low treasury.
                const bool weAreSuzerain = (cs.suzerain == this->m_player);
                const bool hasOtherSuzerain =
                    (cs.suzerain != INVALID_PLAYER && !weAreSuzerain);
                const bool lowTreasury = (treasury < 100);
                const bool aggressive  = (beh.militaryAggression > 1.2f);
                if (!weAreSuzerain && !hasOtherSuzerain &&
                    lowTreasury && aggressive) {
                    (void)bullyCityState(gameState, this->m_player, i);
                }

                // Levy: suzerain at war with full treasury.
                if (weAreSuzerain && atWar && treasury > 300 &&
                    cs.levyPlayer == INVALID_PLAYER) {
                    (void)levyCityStateMilitary(gameState, this->m_player, i);
                }
            }
        }
    }
}

// ============================================================================
// Economy management
// ============================================================================

void AIController::manageEconomy(aoc::game::GameState& gameState,
                                  DiplomacyManager& diplomacy,
                                  const Market& market) {
    aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    if (gsPlayer == nullptr) {
        return;
    }

    std::unordered_map<uint16_t, int32_t> totalStockpile;
    for (const std::unique_ptr<aoc::game::City>& cityPtr : gsPlayer->cities()) {
        for (const std::pair<const uint16_t, int32_t>& entry : cityPtr->stockpile().goods) {
            totalStockpile[entry.first] += entry.second;
        }
    }

    constexpr float SELL_THRESHOLD = 1.5f;
    constexpr float BUY_THRESHOLD  = 0.7f;
    constexpr int32_t MIN_SURPLUS_TO_SELL = 3;

    struct TradeDesire {
        uint16_t goodId;
        int32_t  amount;
        bool     wantToSell;
    };
    std::vector<TradeDesire> desires;

    const uint16_t totalGoods = market.goodsCount();
    for (uint16_t g = 0; g < totalGoods; ++g) {
        const int32_t currentPrice = market.price(g);
        const GoodDef& def = goodDef(g);
        if (def.basePrice <= 0) { continue; }

        const float priceRatio = static_cast<float>(currentPrice) /
                                 static_cast<float>(def.basePrice);
        const int32_t held = totalStockpile[g];

        if (priceRatio > SELL_THRESHOLD && held > MIN_SURPLUS_TO_SELL) {
            const int32_t sellAmount = held / 2;
            if (sellAmount > 0) {
                desires.push_back({g, sellAmount, true});
            }
        } else if (priceRatio < BUY_THRESHOLD && held == 0) {
            desires.push_back({g, 2, false});
        }
    }

    if (desires.empty()) { return; }

    const uint8_t playerCount = diplomacy.playerCount();
    for (const TradeDesire& desire : desires) {
        if (!desire.wantToSell) { continue; }

        for (uint8_t other = 0; other < playerCount; ++other) {
            if (other == this->m_player) { continue; }
            const PairwiseRelation& rel = diplomacy.relation(this->m_player, other);
            if (rel.isAtWar || rel.hasEmbargo) { continue; }
            if (rel.totalScore() < -10) { continue; }

            int32_t partnerHoldings = 0;
            const aoc::game::Player* partnerPlayer = gameState.player(other);
            if (partnerPlayer != nullptr) {
                for (const std::unique_ptr<aoc::game::City>& partnerCity :
                         partnerPlayer->cities()) {
                    partnerHoldings += partnerCity->stockpile().getAmount(desire.goodId);
                }
            }

            if (partnerHoldings < 2) {
                LOG_INFO("AI %u wants to sell %d of good %u (price ratio %.2f) to player %u",
                         static_cast<unsigned>(this->m_player),
                         desire.amount,
                         static_cast<unsigned>(desire.goodId),
                         static_cast<double>(
                             static_cast<float>(market.price(desire.goodId)) /
                             static_cast<float>(goodDef(desire.goodId).basePrice)),
                         static_cast<unsigned>(other));

                diplomacy.addModifier(this->m_player, other,
                    RelationModifier{"Trade interest", 1, 10});
                break;
            }
        }
    }
}

// ============================================================================
// Government management
// ============================================================================

void AIController::manageGovernment(aoc::game::GameState& gameState) {
    aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    if (gsPlayer == nullptr) { return; }

    aoc::sim::PlayerGovernmentComponent& gov = gsPlayer->government();
    const LeaderBehavior& bh = leaderPersonality(gsPlayer->civId()).behavior;

    // Default: keep the highest-id unlocked gov (unlock order = tech progression).
    GovernmentType bestGov = gov.government;
    for (uint8_t g = 0; g < GOVERNMENT_COUNT; ++g) {
        const GovernmentType gt = static_cast<GovernmentType>(g);
        if (gov.isGovernmentUnlocked(gt)) {
            bestGov = gt;
        }
    }

    // Post-industrial ideological pick overrides the linear default when
    // available. High ideologicalFervor = strong commitment; low fervor keeps
    // the pragmatic (default) choice.
    const bool demUnlocked = gov.isGovernmentUnlocked(GovernmentType::Democracy);
    const bool comUnlocked = gov.isGovernmentUnlocked(GovernmentType::Communism);
    const bool fasUnlocked = gov.isGovernmentUnlocked(GovernmentType::Fascism);
    if ((demUnlocked || comUnlocked || fasUnlocked) && bh.ideologicalFervor > 0.8f) {
        // Each ideology has one conditional bonus matched to its character so
        // the final tally doesn't collapse onto one option. Without these,
        // evolved populations (most trustworthiness < 0.6) all pick Communism.
        const float fasScore = bh.militaryAggression + bh.ideologicalFervor
                             + (bh.nukeWillingness > 0.7f ? 0.5f : 0.0f);
        const float demScore = bh.economicFocus + bh.ideologicalFervor
                             + (bh.trustworthiness > 0.7f ? 0.5f : 0.0f);
        const float comScore = bh.expansionism + bh.ideologicalFervor
                             + (bh.trustworthiness < 0.6f ? 0.5f : 0.0f);
        float best = -1.0f;
        if (fasUnlocked && fasScore > best) { best = fasScore; bestGov = GovernmentType::Fascism; }
        if (demUnlocked && demScore > best) { best = demScore; bestGov = GovernmentType::Democracy; }
        if (comUnlocked && comScore > best) { best = comScore; bestGov = GovernmentType::Communism; }
    }
    if (bestGov != gov.government) {
        gov.government = bestGov;
        for (uint8_t s = 0; s < MAX_POLICY_SLOTS; ++s) {
            gov.activePolicies[s] = EMPTY_POLICY_SLOT;
        }
        LOG_INFO("AI %u Adopted government: %.*s",
                 static_cast<unsigned>(this->m_player),
                 static_cast<int>(governmentDef(bestGov).name.size()),
                 governmentDef(bestGov).name.data());
    }

    const GovernmentDef& gdef = governmentDef(gov.government);

    struct SlotInfo {
        uint8_t slotIndex;
        PolicySlotType slotType;
    };
    std::vector<SlotInfo> slots;
    uint8_t slotIdx = 0;
    for (uint8_t s = 0; s < gdef.militarySlots  && slotIdx < MAX_POLICY_SLOTS; ++s, ++slotIdx) {
        slots.push_back({slotIdx, PolicySlotType::Military});
    }
    for (uint8_t s = 0; s < gdef.economicSlots  && slotIdx < MAX_POLICY_SLOTS; ++s, ++slotIdx) {
        slots.push_back({slotIdx, PolicySlotType::Economic});
    }
    for (uint8_t s = 0; s < gdef.diplomaticSlots && slotIdx < MAX_POLICY_SLOTS; ++s, ++slotIdx) {
        slots.push_back({slotIdx, PolicySlotType::Diplomatic});
    }
    for (uint8_t s = 0; s < gdef.wildcardSlots  && slotIdx < MAX_POLICY_SLOTS; ++s, ++slotIdx) {
        slots.push_back({slotIdx, PolicySlotType::Wildcard});
    }

    std::array<bool, POLICY_CARD_COUNT> assigned{};

    for (const SlotInfo& slot : slots) {
        int8_t bestPolicy = EMPTY_POLICY_SLOT;
        float bestValue = -1.0f;

        for (uint8_t p = 0; p < POLICY_CARD_COUNT; ++p) {
            if (!gov.isPolicyUnlocked(p) || assigned[static_cast<std::size_t>(p)]) {
                continue;
            }
            const PolicyCardDef& pdef = policyCardDef(p);
            if (pdef.slotType != slot.slotType &&
                slot.slotType != PolicySlotType::Wildcard) {
                continue;
            }
            float value = pdef.modifiers.productionMultiplier +
                          pdef.modifiers.goldMultiplier +
                          pdef.modifiers.scienceMultiplier +
                          pdef.modifiers.cultureMultiplier +
                          pdef.modifiers.combatStrengthBonus * 0.1f;
            if (value > bestValue) {
                bestValue = value;
                bestPolicy = static_cast<int8_t>(p);
            }
        }

        gov.activePolicies[slot.slotIndex] = bestPolicy;
        if (bestPolicy != EMPTY_POLICY_SLOT) {
            assigned[static_cast<std::size_t>(bestPolicy)] = true;
        }
    }
}

// ============================================================================
// Trade route management
// ============================================================================

void AIController::manageTradeRoutes(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                                      const Market& market, const DiplomacyManager& diplomacy) {
    aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    if (gsPlayer == nullptr) { return; }

    // Collect idle Trader units: those whose TraderComponent has no route assigned yet
    std::vector<aoc::game::Unit*> idleTraders;
    for (const std::unique_ptr<aoc::game::Unit>& u : gsPlayer->units()) {
        if (unitTypeDef(u->typeId()).unitClass != UnitClass::Trader) { continue; }
        if (u->trader().owner == INVALID_PLAYER) {
            idleTraders.push_back(u.get());
        }
    }

    if (idleTraders.empty()) { return; }

    // 2026-05-03: skip the whole scoring loop when civ already saturated
    // its trade-route cap. Audit showed 82k "at cap" rejection logs per
    // 72-sim run because every idle trader probed every turn. Cheap up-front
    // check avoids the expensive city-scan + establishTradeRoute call.
    {
        const int32_t cap = computeTotalTradeSlots(*gsPlayer, grid);
        int32_t activeRoutes = 0;
        for (const std::unique_ptr<aoc::game::Unit>& u : gsPlayer->units()) {
            if (u == nullptr) { continue; }
            if (u->typeDef().unitClass != UnitClass::Trader) { continue; }
            if (u->trader().owner == INVALID_PLAYER) { continue; }
            ++activeRoutes;
        }
        if (activeRoutes >= cap) { return; }
    }

    const aoc::sim::PlayerEconomyComponent& myEcon = gsPlayer->economy();

    for (aoc::game::Unit* traderUnit : idleTraders) {
        // Score each city as a trade destination based on complementary resources
        aoc::game::City* bestCity = nullptr;
        float bestScore = -1.0f;

        for (const std::unique_ptr<aoc::game::Player>& pPtr : gameState.players()) {
            for (const std::unique_ptr<aoc::game::City>& cityPtr : pPtr->cities()) {
                if (cityPtr->location() == traderUnit->position()) { continue; }
                // 2026-05-02: skip razed / invalid-owner cities. Founder list
                // retains captured-then-razed cities with owner == INVALID;
                // every trade-route attempt against those rejected as
                // "no benefit / hostile" because gameState.player(255)==null.
                if (cityPtr->owner() == aoc::INVALID_PLAYER) { continue; }
                // 2026-05-02: skip cities owned by civs we're at war with or
                // embargoing. Audit: 14k Trade-route-rejected logs were all
                // from this collision — AI proposed routes to enemy capitals
                // because score formula ignored war state. Pre-filter here
                // so traders pick a peaceful partner immediately.
                if (cityPtr->owner() != this->m_player
                    && (diplomacy.isAtWar(this->m_player, cityPtr->owner())
                        || diplomacy.hasEmbargo(this->m_player, cityPtr->owner()))) {
                    continue;
                }

                float score = 0.0f;
                const int32_t dist =
                    grid.distance(traderUnit->position(), cityPtr->location());
                const float distPenalty =
                    1.0f / static_cast<float>(std::max(1, dist));

                if (cityPtr->owner() != this->m_player) {
                    const aoc::game::Player* destPlayerObj = gameState.player(cityPtr->owner());
                    if (destPlayerObj != nullptr) {
                        for (const std::pair<const uint16_t, int32_t>& need : myEcon.totalNeeds) {
                            const int32_t destHas =
                                cityPtr->stockpile().getAmount(need.first);
                            if (destHas > 1) {
                                score +=
                                    static_cast<float>(std::min(destHas, need.second))
                                    * static_cast<float>(
                                          market.marketData(need.first).currentPrice);
                            }
                        }
                        const aoc::sim::PlayerEconomyComponent& destEcon = destPlayerObj->economy();
                        for (const std::pair<const uint16_t, int32_t>& theirNeed :
                                 destEcon.totalNeeds) {
                            int32_t weHave = 0;
                            std::unordered_map<uint16_t, int32_t>::const_iterator supIt =
                                myEcon.totalSupply.find(theirNeed.first);
                            if (supIt != myEcon.totalSupply.end()) { weHave = supIt->second; }
                            if (weHave > 1) {
                                score +=
                                    static_cast<float>(std::min(weHave, theirNeed.second))
                                    * static_cast<float>(
                                          market.marketData(theirNeed.first).currentPrice)
                                    * 0.5f;
                            }
                        }
                    }
                    score += 50.0f;  // Foreign trade base bonus
                } else {
                    score += 10.0f;  // Internal trade: lower priority
                }

                score *= distPenalty;
                if (score > bestScore) {
                    bestScore = score;
                    bestCity = cityPtr.get();
                }
            }
        }

        if (bestCity != nullptr) {
            const ErrorCode result = establishTradeRoute(
                gameState, grid, market, &diplomacy, *traderUnit, *bestCity);
            if (result == ErrorCode::Ok) {
                // AI always auto-renews trade routes so tech diffusion and
                // economy stay active late-game (route lifetime is a few trips).
                traderUnit->autoRenewRoute = true;
                LOG_INFO("AI %u established trade route to %s (player %u, score %.0f)",
                         static_cast<unsigned>(this->m_player),
                         bestCity->name().c_str(),
                         static_cast<unsigned>(bestCity->owner()),
                         static_cast<double>(bestScore));
            }
        }
    }
}

// ============================================================================
// Monetary system management
// ============================================================================

void AIController::manageMonetarySystem(aoc::game::GameState& gameState,
                                         aoc::map::HexGrid& grid,
                                         const DiplomacyManager& /*diplomacy*/) {
    aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    if (gsPlayer == nullptr) { return; }

    aoc::sim::MonetaryStateComponent& myState = gsPlayer->monetary();
    const int32_t cityCount = gsPlayer->cityCount();
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

    // Count distinct trade partners from global trade routes
    int32_t tradePartnerCount = 0;
    {
        std::unordered_set<PlayerId> partners;
        for (const aoc::sim::TradeRouteComponent& route : gameState.tradeRoutes()) {
            if (route.sourcePlayer == this->m_player &&
                route.destPlayer != this->m_player) {
                partners.insert(route.destPlayer);
            }
            if (route.destPlayer == this->m_player &&
                route.sourcePlayer != this->m_player) {
                partners.insert(route.sourcePlayer);
            }
        }
        tradePartnerCount = static_cast<int32_t>(partners.size());
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

    const ErrorCode result = myState.canTransition(
        nextTarget, cityCount, tradePartnerCount, gdpRank, playerCount);
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
                gsPlayer->setTreasury(coinValue + popSavings);
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
    const int32_t cityCount = gsPlayer->cityCount();
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
            const int32_t settlerCost = purchaseCost(static_cast<float>(unitTypeDef(UnitTypeId{3}).productionCost));
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
    if (gsPlayer->spendGold(CANAL_GOLD_COST)) {
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
            gsPlayer->addGold(CANAL_GOLD_COST);
        }
    }
}

} // namespace aoc::sim::ai
