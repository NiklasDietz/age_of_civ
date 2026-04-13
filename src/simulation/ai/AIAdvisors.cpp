/**
 * @file AIAdvisors.cpp
 * @brief AI advisor subsystem implementations that post domain assessments
 *        to the player's AIBlackboard.
 *
 * See AIAdvisors.hpp for the call-frequency contract.  Each advisor is
 * intentionally self-contained: it reads the public Player/GameState APIs
 * and writes only its own blackboard fields to avoid coupling.
 */

#include "aoc/simulation/ai/AIAdvisors.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/ai/AIBlackboard.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace aoc::sim::ai {

// ============================================================================
// Internal helpers
// ============================================================================

/// Score a candidate city site using the same weighted yield formula as the
/// settler controller, scanning ring-1 and ring-2 tiles.
///
/// Returns a large negative sentinel for uninhabitable positions so callers
/// can filter them without a separate validity check.
[[nodiscard]] static float scoreCandidate(aoc::hex::AxialCoord pos,
                                           const aoc::map::HexGrid& grid,
                                           const aoc::game::GameState& gameState,
                                           PlayerId player) {
    if (!grid.isValid(pos)) {
        return -9999.0f;
    }

    const int32_t centerIdx = grid.toIndex(pos);
    const aoc::map::TerrainType terrain = grid.terrain(centerIdx);

    // Water, ocean, and mountains cannot host a city.
    if (aoc::map::isWater(terrain) || terrain == aoc::map::TerrainType::Mountain) {
        return -9999.0f;
    }

    // Tile owned by a different player is off-limits.
    const PlayerId owner = grid.owner(centerIdx);
    if (owner != INVALID_PLAYER && owner != player) {
        return -9999.0f;
    }

    float score = 0.0f;

    // Scan ring-1 and ring-2 yields using spiral.
    std::vector<aoc::hex::AxialCoord> tiles;
    tiles.reserve(18);
    aoc::hex::spiral(pos, 2, std::back_inserter(tiles));

    for (const aoc::hex::AxialCoord& tile : tiles) {
        if (!grid.isValid(tile)) {
            continue;
        }
        const int32_t idx = grid.toIndex(tile);
        const aoc::map::TileYield yields = grid.tileYield(idx);
        score += static_cast<float>(yields.food)       * 3.0f;
        score += static_cast<float>(yields.production)  * 2.0f;
        score += static_cast<float>(yields.gold)        * 1.0f;
        score += static_cast<float>(yields.science)     * 1.5f;

        // Coastal and fresh-water bonuses.
        const aoc::map::TerrainType tileT = grid.terrain(idx);
        if (aoc::map::isWater(tileT)) {
            score += 6.0f;
        }
    }

    // Penalise positions too close to existing cities.
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : p->cities()) {
            const int32_t dist = aoc::hex::distance(pos, city->location());
            if (dist < 3) {
                score -= 40.0f;
            } else if (dist > 10) {
                score -= static_cast<float>(dist - 10) * 3.0f;
            }
        }
    }

    return score;
}

// ============================================================================
// MilitaryAdvisor
// ============================================================================

void updateMilitaryAssessment(const aoc::game::GameState& gameState,
                               aoc::game::Player& player) {
    aoc::sim::ai::AIBlackboard& bb = player.blackboard();

    // Sum own military combat strength.
    float ownStrength = 0.0f;
    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        if (isMilitary(unitTypeDef(unit->typeId()).unitClass)) {
            ownStrength += static_cast<float>(unitTypeDef(unit->typeId()).combatStrength);
        }
    }

    // Sum enemy military strength within 10 tiles of any own city.
    float enemyThreat = 0.0f;
    bb.attackTargets.clear();
    bb.defendPriorities.clear();

    for (const std::unique_ptr<aoc::game::Player>& enemy : gameState.players()) {
        if (enemy->id() == player.id()) {
            continue;
        }
        for (const std::unique_ptr<aoc::game::Unit>& eUnit : enemy->units()) {
            if (!isMilitary(unitTypeDef(eUnit->typeId()).unitClass)) {
                continue;
            }
            // Count each enemy unit at most once even if it threatens multiple cities.
            bool threatensSomeCity = false;
            for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
                if (aoc::hex::distance(eUnit->position(), city->location()) <= 10) {
                    threatensSomeCity = true;
                    break;
                }
            }
            if (threatensSomeCity) {
                enemyThreat += static_cast<float>(
                    unitTypeDef(eUnit->typeId()).combatStrength);
            }
        }

        // Flag weak enemy cities as attack targets when we have strength advantage.
        for (const std::unique_ptr<aoc::game::City>& eCity : enemy->cities()) {
            bb.attackTargets.push_back(eCity->location());
        }
    }

    // Threat level: ratio of nearby enemy strength to own strength, scaled to [0,1].
    if (ownStrength > 0.0f) {
        bb.threatLevel = std::min(1.0f, (enemyThreat / ownStrength) * 0.5f);
    } else {
        bb.threatLevel = (enemyThreat > 0.0f) ? 1.0f : 0.0f;
    }

    // Desired military: at least 2 units per city, never less than 2 total.
    bb.desiredMilitaryUnits = std::max(2, player.cityCount() * 3);

    // Mark own cities with nearby threats as defend priorities.
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (bb.threatLevel > 0.3f) {
            bb.defendPriorities.push_back(city->location());
        }
    }

    LOG_DEBUG("AI %u [MilitaryAdvisor] ownStr=%.1f enemyThreat=%.1f threatLevel=%.2f desiredUnits=%d",
              static_cast<unsigned>(player.id()),
              static_cast<double>(ownStrength),
              static_cast<double>(enemyThreat),
              static_cast<double>(bb.threatLevel),
              bb.desiredMilitaryUnits);
}

// ============================================================================
// EconomyAdvisor
// ============================================================================

void updateEconomyAssessment(aoc::game::Player& player) {
    aoc::sim::ai::AIBlackboard& bb = player.blackboard();

    const CurrencyAmount income    = player.incomePerTurn();
    const CurrencyAmount treasury  = player.treasury();

    // Sum unit maintenance (military only pay; civilians are free).
    CurrencyAmount unitMaintenance = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        const int32_t cost = unit->typeDef().maintenanceGold();
        if (cost > 0) {
            unitMaintenance += static_cast<CurrencyAmount>(cost);
        }
    }

    // Sum building maintenance from all cities.
    CurrencyAmount buildingMaintenance = 0;
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        const aoc::sim::CityDistrictsComponent& districts = city->districts();
        for (uint16_t bidx = 0;
             bidx < static_cast<uint16_t>(BUILDING_DEFS.size()); ++bidx) {
            if (districts.hasBuilding(BuildingId{bidx})) {
                buildingMaintenance +=
                    static_cast<CurrencyAmount>(BUILDING_DEFS[bidx].maintenanceCost);
            }
        }
    }

    const CurrencyAmount totalMaintenance = unitMaintenance + buildingMaintenance;

    // Gold pressure: how much maintenance exceeds income (0 = break-even, 1 = double income).
    if (income > 0) {
        const float ratio =
            static_cast<float>(totalMaintenance) / static_cast<float>(income);
        bb.goldPressure = std::max(0.0f, std::min(1.0f, ratio - 0.5f));
    } else {
        // No income at all: treat as high pressure unless treasury is comfortable.
        bb.goldPressure = (treasury < 100) ? 0.8f : 0.3f;
    }

    // Tax rate recommendation: raise taxes when treasury is low.
    if (treasury < 50) {
        bb.recommendedTaxRate = 0.30f;
    } else if (treasury < 200) {
        bb.recommendedTaxRate = 0.20f;
    } else {
        bb.recommendedTaxRate = 0.15f;
    }

    LOG_DEBUG("AI %u [EconomyAdvisor] income=%lld maintenance=%lld goldPressure=%.2f taxRate=%.2f",
              static_cast<unsigned>(player.id()),
              static_cast<long long>(income),
              static_cast<long long>(totalMaintenance),
              static_cast<double>(bb.goldPressure),
              static_cast<double>(bb.recommendedTaxRate));
}

// ============================================================================
// ExpansionAdvisor
// ============================================================================

void updateExpansionAssessment(const aoc::game::GameState& gameState,
                                aoc::game::Player& player,
                                const aoc::map::HexGrid& grid) {
    aoc::sim::ai::AIBlackboard& bb = player.blackboard();

    const aoc::sim::CivId myCivId = player.civId();
    const aoc::sim::LeaderPersonalityDef& personality =
        aoc::sim::leaderPersonality(myCivId);
    const aoc::sim::AIScaledTargets targets =
        aoc::sim::computeScaledTargets(personality.behavior);

    const int32_t targetCities = targets.maxCities;
    const int32_t ownCities    = player.cityCount();

    // expansionOpportunity: fraction of desired empire still unfounded.
    const float ratio =
        (targetCities > 0)
        ? static_cast<float>(ownCities) / static_cast<float>(targetCities)
        : 1.0f;
    bb.expansionOpportunity = std::max(0.0f, std::min(1.0f, 1.0f - ratio));

    // Scan candidate city sites within a moderate radius of each owned city.
    // Collect candidates, deduplicate, score, keep top 3.
    constexpr int32_t SCAN_RADIUS = 12;
    constexpr int32_t TOP_N       = 3;

    struct ScoredSite {
        aoc::hex::AxialCoord coord;
        float                score;
    };

    std::vector<ScoredSite> scored;
    scored.reserve(200);

    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        std::vector<aoc::hex::AxialCoord> candidates;
        candidates.reserve(static_cast<std::size_t>(SCAN_RADIUS * SCAN_RADIUS * 3));
        aoc::hex::spiral(city->location(), SCAN_RADIUS,
                          std::back_inserter(candidates));

        for (const aoc::hex::AxialCoord& candidate : candidates) {
            const float siteScore =
                scoreCandidate(candidate, grid, gameState, player.id());
            if (siteScore > 0.0f) {
                scored.push_back(ScoredSite{candidate, siteScore});
            }
        }
    }

    // If the player has no cities yet, scan from the origin region.
    if (player.cities().empty()) {
        constexpr aoc::hex::AxialCoord ORIGIN{0, 0};
        std::vector<aoc::hex::AxialCoord> candidates;
        candidates.reserve(static_cast<std::size_t>(SCAN_RADIUS * SCAN_RADIUS * 3));
        aoc::hex::spiral(ORIGIN, SCAN_RADIUS, std::back_inserter(candidates));
        for (const aoc::hex::AxialCoord& candidate : candidates) {
            const float siteScore =
                scoreCandidate(candidate, grid, gameState, player.id());
            if (siteScore > 0.0f) {
                scored.push_back(ScoredSite{candidate, siteScore});
            }
        }
    }

    // Sort descending by score and take the top N unique sites.
    std::sort(scored.begin(), scored.end(),
              [](const ScoredSite& a, const ScoredSite& b) {
                  return a.score > b.score;
              });

    bb.bestCitySites.clear();
    for (const ScoredSite& site : scored) {
        if (static_cast<int32_t>(bb.bestCitySites.size()) >= TOP_N) {
            break;
        }
        // Ensure minimum spacing between recommended sites.
        bool tooClose = false;
        for (const aoc::hex::AxialCoord& existing : bb.bestCitySites) {
            if (aoc::hex::distance(site.coord, existing) < 3) {
                tooClose = true;
                break;
            }
        }
        if (!tooClose) {
            bb.bestCitySites.push_back(site.coord);
        }
    }

    LOG_DEBUG("AI %u [ExpansionAdvisor] cities=%d/%d expansionOpportunity=%.2f sites=%zu",
              static_cast<unsigned>(player.id()),
              ownCities, targetCities,
              static_cast<double>(bb.expansionOpportunity),
              bb.bestCitySites.size());
}

// ============================================================================
// ResearchAdvisor
// ============================================================================

void updateResearchAssessment(const aoc::game::GameState& gameState,
                               aoc::game::Player& player) {
    aoc::sim::ai::AIBlackboard& bb = player.blackboard();

    // Count this player's completed techs.
    int32_t ownTechs = 0;
    const std::vector<bool>& completed = player.tech().completedTechs;
    for (bool done : completed) {
        if (done) {
            ++ownTechs;
        }
    }

    // Compute average completed tech count across all players.
    int32_t totalTechs = 0;
    int32_t playerCount = 0;
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        ++playerCount;
        for (bool done : p->tech().completedTechs) {
            if (done) {
                ++totalTechs;
            }
        }
    }

    const float avgTechs =
        (playerCount > 0)
        ? static_cast<float>(totalTechs) / static_cast<float>(playerCount)
        : static_cast<float>(ownTechs);

    // techGap: how far below average, scaled to [0,1].
    if (avgTechs > 0.0f) {
        const float gap = avgTechs - static_cast<float>(ownTechs);
        bb.techGap = std::max(0.0f, std::min(1.0f, gap / avgTechs));
    } else {
        bb.techGap = 0.0f;
    }

    LOG_DEBUG("AI %u [ResearchAdvisor] ownTechs=%d avgTechs=%.1f techGap=%.2f",
              static_cast<unsigned>(player.id()),
              ownTechs,
              static_cast<double>(avgTechs),
              static_cast<double>(bb.techGap));
}

// ============================================================================
// DiplomacyAdvisor
// ============================================================================

void updateDiplomacyAssessment(const aoc::game::GameState& gameState,
                                aoc::game::Player& player,
                                const aoc::sim::DiplomacyManager& diplomacy) {
    aoc::sim::ai::AIBlackboard& bb = player.blackboard();

    int32_t activeWars = 0;
    for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
        if (other->id() != player.id() &&
            diplomacy.isAtWar(player.id(), other->id())) {
            ++activeWars;
        }
    }

    // Saturate at 3 simultaneous wars (diplomaticDanger = 1.0 at >= 3 wars).
    bb.diplomaticDanger = std::max(0.0f,
                          std::min(1.0f,
                          static_cast<float>(activeWars) / 3.0f));

    LOG_DEBUG("AI %u [DiplomacyAdvisor] activeWars=%d diplomaticDanger=%.2f",
              static_cast<unsigned>(player.id()),
              activeWars,
              static_cast<double>(bb.diplomaticDanger));
}

// ============================================================================
// Strategic posture evaluation
// ============================================================================

void evaluateStrategicPosture(aoc::game::Player& player) {
    aoc::sim::ai::AIBlackboard& bb = player.blackboard();

    const int32_t currentMilitaryUnits = player.militaryUnitCount();

    // Priority order: Defense > Aggression > Expansion > Development > Economic
    if (bb.threatLevel > 0.7f) {
        bb.posture = StrategicPosture::Defense;
    } else if (bb.threatLevel < 0.2f &&
               bb.desiredMilitaryUnits <= currentMilitaryUnits * 2 &&
               bb.diplomaticDanger > 0.3f) {
        bb.posture = StrategicPosture::Aggression;
    } else if (bb.expansionOpportunity > 0.5f) {
        bb.posture = StrategicPosture::Expansion;
    } else if (bb.techGap > 0.4f) {
        bb.posture = StrategicPosture::Development;
    } else if (bb.goldPressure > 0.5f) {
        bb.posture = StrategicPosture::Economic;
    } else {
        bb.posture = StrategicPosture::Development;
    }

    LOG_DEBUG("AI %u [Posture] threat=%.2f expansion=%.2f techGap=%.2f gold=%.2f diplo=%.2f -> %d",
              static_cast<unsigned>(player.id()),
              static_cast<double>(bb.threatLevel),
              static_cast<double>(bb.expansionOpportunity),
              static_cast<double>(bb.techGap),
              static_cast<double>(bb.goldPressure),
              static_cast<double>(bb.diplomaticDanger),
              static_cast<int>(bb.posture));
}

// ============================================================================
// Posture multiplier
// ============================================================================

float postureMultiplier(StrategicPosture posture,
                         bool isMilitary,
                         bool isSettler,
                         bool isBuilder,
                         bool isScience,
                         bool isGold,
                         bool isTrader) {
    switch (posture) {
        case StrategicPosture::Expansion:
            if (isSettler)  { return 2.0f; }
            if (isBuilder)  { return 1.5f; }
            return 1.0f;

        case StrategicPosture::MilitaryBuildup:
            if (isMilitary) { return 2.0f; }
            if (isSettler)  { return 0.5f; }
            return 1.0f;

        case StrategicPosture::Aggression:
            if (isMilitary) { return 2.5f; }
            if (isSettler)  { return 0.3f; }
            return 1.0f;

        case StrategicPosture::Defense:
            if (isMilitary) { return 2.0f; }
            return 1.0f;

        case StrategicPosture::Development:
            if (isScience)  { return 1.8f; }
            return 1.0f;

        case StrategicPosture::Economic:
            if (isGold)     { return 2.0f; }
            if (isTrader)   { return 1.5f; }
            return 1.0f;

        default:
            return 1.0f;
    }
}

} // namespace aoc::sim::ai
