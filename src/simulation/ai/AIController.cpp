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
#include "aoc/simulation/ai/AIAdvisors.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/economy/Market.hpp"
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
#include "aoc/simulation/ai/UtilityAI.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"

#include <unordered_set>

namespace aoc::sim::ai {

// ============================================================================
// Helper: Find the best military unit type ID the player can produce.
// ============================================================================

static UnitTypeId bestAvailableMilitaryUnit(const aoc::game::GameState& gameState,
                                            PlayerId player) {
    UnitTypeId bestId{0};
    int32_t bestStrength = 0;

    for (const UnitTypeDef& def : UNIT_TYPE_DEFS) {
        if (!isMilitary(def.unitClass) || isNaval(def.unitClass)) {
            continue;
        }
        if (!canBuildUnit(gameState, player, def.id)) {
            continue;
        }
        const int32_t strength = def.combatStrength + def.rangedStrength;
        if (strength > bestStrength) {
            bestStrength = strength;
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
        if (def.unitClass == UnitClass::Civilian) {
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
                                DiplomacyManager& diplomacy,
                                const Market& market,
                                aoc::Random& rng) {
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
    this->m_militaryController.executeMilitaryActions(gameState, grid, rng);
    this->manageEconomy(gameState, diplomacy, market);
    this->manageMonetarySystem(gameState, diplomacy);
    aoc::sim::aiEconomicStrategy(gameState, grid, market, diplomacy, this->m_player,
                                  static_cast<int32_t>(this->m_difficulty));
    this->executeDiplomacyActions(gameState, diplomacy, market);
    this->manageTradeRoutes(gameState, grid, market, diplomacy);

    refreshMovement(gameState, this->m_player);
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
                           float   expansionOpportunity) {
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
    const int32_t desiredPerCity = 3;
    const int32_t desiredTotal   = ownedCities * desiredPerCity;

    // military_need: inverse -- want more when below 3 per city
    const aoc::sim::ai::UtilityConsideration militaryNeed{
        0.0f, static_cast<float>(std::max(1, desiredTotal)),
        aoc::sim::ai::UtilityCurve::inverse()
    };

    // threat_exists: doubled score when an enemy is nearby
    const float threatScore = enemyNearby ? 1.0f : 0.5f;

    // can_afford: soft gold check -- very low treasury is a blocker
    const aoc::sim::ai::UtilityConsideration canAfford{
        0.0f, 50.0f,
        aoc::sim::ai::UtilityCurve::logistic(8.0f, 0.5f)
    };

    // zero_military_emergency: double score when completely defenseless
    const float emergencyMultiplier = (militaryUnits == 0) ? 2.0f : 1.0f;

    // threatLevel [0,1] from the military advisor amplifies the score when the
    // blackboard confirms nearby enemy forces; combines with the local enemyNearby
    // flag for a layered threat response.
    const float threatLevelBoost = 1.0f + threatLevel;

    constexpr float BASE_WEIGHT = 0.8f;

    return BASE_WEIGHT
           * behavior.prodMilitary
           * behavior.militaryAggression
           * militaryNeed.score(static_cast<float>(militaryUnits))
           * threatScore
           * canAfford.score(treasury)
           * emergencyMultiplier
           * threatLevelBoost;
}

// -------------------------------------------------------------------------
// Internal: score a builder candidate using utility curves
// -------------------------------------------------------------------------

static float scoreBuilder(const LeaderBehavior& behavior,
                           int32_t unimprovedTiles,
                           int32_t workedTiles,
                           int32_t builderCount) {
    // tiles_need: fraction of worked tiles that are unimproved
    const aoc::sim::ai::UtilityConsideration tilesNeed{
        0.0f, static_cast<float>(std::max(1, workedTiles)),
        aoc::sim::ai::UtilityCurve::linear(1.0f, 0.0f)
    };

    // no_builder: strongly prefer having at least one builder
    const float noBuilderScore = (builderCount == 0) ? 1.0f : 0.2f;

    constexpr float BASE_WEIGHT = 0.6f;

    return BASE_WEIGHT
           * behavior.prodBuilders
           * tilesNeed.score(static_cast<float>(unimprovedTiles))
           * noBuilderScore;
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

    // trade_need: want traders up to min(cityCount, 3)
    const int32_t maxTraders = std::min(cityCount, 3);
    const float tradeScore = (traderCount < maxTraders) ? 0.8f : 0.1f;

    constexpr float BASE_WEIGHT = 0.4f;

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
    constexpr float BUILDING_SCORE_MAX = 200.0f;
    constexpr float BASE_WEIGHT        = 0.5f;

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
                bbExpansionOpportunity
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
            if (militaryScore > 0.0f) {
                ProductionCandidate candidate{};
                candidate.item.type      = ProductionItemType::Unit;
                candidate.item.itemId    = bestMilitaryId.value;
                candidate.item.name      = std::string(bestMilitaryDef.name);
                candidate.item.totalCost = static_cast<float>(bestMilitaryDef.productionCost);
                candidate.item.progress  = 0.0f;
                candidate.score          = militaryScore
                    * postureMultiplier(currentPosture,
                                        true, false, false, false, false, false);
                candidates.push_back(std::move(candidate));
            }
        }

        // --- Builder ---
        {
            const float builderScore = scoreBuilder(
                personality.behavior,
                unimprovedTiles,
                RING1_TILES,
                unitCounts.builders
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

            for (uint16_t bidx = 0;
                     bidx < static_cast<uint16_t>(BUILDING_DEFS.size()); ++bidx) {
                const BuildingDef& bdef = BUILDING_DEFS[bidx];
                if (!canBuildBuilding(gameState, this->m_player, city, bdef.id)) {
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

            const std::array<DistrictOption, 5> districtOptions = {{
                { DistrictType::Industrial,
                  60.0f,
                  0.5f * personality.behavior.prodBuildings * personality.behavior.economicFocus },
                { DistrictType::Commercial,
                  60.0f,
                  0.45f * personality.behavior.economicFocus },
                { DistrictType::Campus,
                  55.0f,
                  0.45f * personality.behavior.scienceFocus },
                { DistrictType::Encampment,
                  55.0f,
                  0.35f * personality.behavior.militaryAggression },
                { DistrictType::Harbor,
                  70.0f,
                  isCityCoastal
                      ? 0.4f * personality.behavior.economicFocus
                      : 0.0f },
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

        queue.queue.push_back(std::move(chosen));
        ++cityIndex;
    }
}

// ============================================================================
// Diplomacy
// ============================================================================

void AIController::executeDiplomacyActions(aoc::game::GameState& gameState,
                                            DiplomacyManager& diplomacy,
                                            const Market& market) {
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

    for (uint8_t other = 0; other < playerCount; ++other) {
        if (other == this->m_player) { continue; }

        PairwiseRelation& rel = diplomacy.relation(this->m_player, other);
        const int32_t theirMilitary = militaryCounts[static_cast<std::size_t>(other)];
        const int32_t relationScore = rel.totalScore();

        if (rel.isAtWar) {
            if (theirMilitary > ourMilitary) {
                diplomacy.makePeace(this->m_player, other);
                LOG_INFO("AI %u Proposed peace with player %u (outmilitaried %d vs %d)",
                         static_cast<unsigned>(this->m_player),
                         static_cast<unsigned>(other),
                         ourMilitary, theirMilitary);
            }
        } else {
            const bool easyAI = (this->m_difficulty == aoc::ui::AIDifficulty::Easy);
            const bool hardAI = (this->m_difficulty == aoc::ui::AIDifficulty::Hard);

            const float militaryRatioThreshold = hardAI ? 1.2f : 1.5f;
            const int32_t relationThreshold = hardAI ? -10 : -20;
            const int32_t warChanceThreshold = hardAI ? 5 : 3;

            // Standard war declaration: military advantage + strained relations.
            if (!easyAI && ourMilitary > 0 && theirMilitary >= 0 &&
                static_cast<float>(ourMilitary) >
                    militaryRatioThreshold * static_cast<float>(theirMilitary) &&
                relationScore < relationThreshold) {
                const int32_t warChance =
                    ((ourMilitary * 7 + theirMilitary * 13 +
                      static_cast<int32_t>(this->m_player) * 31) % 10);
                if (warChance < warChanceThreshold) {
                    diplomacy.declareWar(this->m_player, other);
                    LOG_INFO("AI %u Declared war on player %u (military %d vs %d, relations %d)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             ourMilitary, theirMilitary, relationScore);
                }
            }

            // Opportunistic war declaration: 2:1 military advantage regardless of
            // relations.  This fires when the player has built a dominant force and
            // the opponent is clearly outmatched.  Requires at least 4 own units so
            // early scouting noise does not trigger wars.
            if (!easyAI && !rel.isAtWar && ourMilitary >= 4 &&
                static_cast<float>(ourMilitary) >=
                    2.0f * static_cast<float>(std::max(1, theirMilitary))) {
                const int32_t warChance =
                    ((ourMilitary * 11 + theirMilitary * 17 +
                      static_cast<int32_t>(this->m_player) * 37) % 10);
                const int32_t threshold = hardAI ? 4 : 2;
                if (warChance < threshold) {
                    diplomacy.declareWar(this->m_player, other);
                    LOG_INFO("AI %u Declared opportunistic war on player %u "
                             "(2:1 advantage: %d vs %d, relations %d)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             ourMilitary, theirMilitary, relationScore);
                }
            }

            if (!rel.hasOpenBorders && relationScore > 10) {
                diplomacy.grantOpenBorders(this->m_player, other);
                LOG_INFO("AI %u Opened borders with player %u (relations %d)",
                         static_cast<unsigned>(this->m_player),
                         static_cast<unsigned>(other), relationScore);
            }

            if (!rel.hasEconomicAlliance && relationScore > 20) {
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
                    diplomacy.formEconomicAlliance(this->m_player, other);
                    LOG_INFO("AI %u Formed economic alliance with player %u "
                             "(relations %d, %d complementary goods)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             relationScore, complementaryGoods);
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

    GovernmentType bestGov = gov.government;
    for (uint8_t g = 0; g < GOVERNMENT_COUNT; ++g) {
        const GovernmentType gt = static_cast<GovernmentType>(g);
        if (gov.isGovernmentUnlocked(gt)) {
            bestGov = gt;
        }
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
        if (!u->trader().owner != INVALID_PLAYER) {
            idleTraders.push_back(u.get());
        }
    }

    if (idleTraders.empty()) { return; }

    const aoc::sim::PlayerEconomyComponent& myEcon = gsPlayer->economy();

    for (aoc::game::Unit* traderUnit : idleTraders) {
        // Score each city as a trade destination based on complementary resources
        aoc::game::City* bestCity = nullptr;
        float bestScore = -1.0f;

        for (const std::unique_ptr<aoc::game::Player>& pPtr : gameState.players()) {
            for (const std::unique_ptr<aoc::game::City>& cityPtr : pPtr->cities()) {
                if (cityPtr->location() == traderUnit->position()) { continue; }

                float score = 0.0f;
                const int32_t dist =
                    aoc::hex::distance(traderUnit->position(), cityPtr->location());
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
                                         const DiplomacyManager& /*diplomacy*/) {
    aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    if (gsPlayer == nullptr) { return; }

    aoc::sim::MonetaryStateComponent& myState = gsPlayer->monetary();
    const int32_t cityCount = gsPlayer->cityCount();
    const int32_t playerCount = gameState.playerCount();

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
        default:
            return;
    }

    if (nextTarget == MonetarySystemType::FiatMoney && gdpRank > 2) {
        return;
    }

    const ErrorCode result = myState.canTransition(
        nextTarget, cityCount, tradePartnerCount, gdpRank, playerCount);
    if (result == ErrorCode::Ok) {
        myState.transitionTo(nextTarget);
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

} // namespace aoc::sim::ai
