/**
 * @file AIResearchPlanner.cpp
 * @brief AI tech and civic research selection logic.
 *
 * Evaluates available technologies and civics, scoring them by what they
 * unlock (buildings, units, goods) and the current threat level, then
 * queues the highest-scoring option.
 */

#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/ai/AIResearchPlanner.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/core/DecisionLog.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"

#include <algorithm>
#include <limits>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace aoc::sim::ai {

// ============================================================================
// Constructor
// ============================================================================

AIResearchPlanner::AIResearchPlanner(PlayerId player, aoc::ui::AIDifficulty difficulty)
    : m_player(player)
    , m_difficulty(difficulty)
{
}

// ============================================================================
// Research selection
// ============================================================================

void AIResearchPlanner::selectResearch(aoc::game::GameState& gameState) {
    aoc::game::Player* myPlayer = gameState.player(this->m_player);
    if (myPlayer == nullptr) { return; }

    const int32_t militaryCount = myPlayer->militaryUnitCount();
    const bool threatened = militaryCount < 3;

    const int32_t ownedCityCount = myPlayer->cityCount();

    // Collect set of buildings already built across all cities
    std::unordered_set<uint16_t> ownedBuildings;
    for (const std::unique_ptr<aoc::game::City>& cityPtr : myPlayer->cities()) {
        if (cityPtr == nullptr) { continue; }
        for (const CityDistrictsComponent::PlacedDistrict& d : cityPtr->districts().districts) {
            for (const BuildingId& bid : d.buildings) {
                ownedBuildings.insert(bid.value);
            }
        }
    }

    // Tech research selection
    PlayerTechComponent& tech = myPlayer->tech();
    if (!tech.currentResearch.isValid()) {
        const std::vector<TechId> available = tech.availableTechs();
        if (!available.empty()) {
            TechId best = available[0];
            int32_t bestScore = std::numeric_limits<int32_t>::min();

            const bool hardAI = (this->m_difficulty == aoc::ui::AIDifficulty::Hard);
            const CivId myCiv = myPlayer->civId();
            const LeaderBehavior& beh = leaderPersonality(myCiv).behavior;

            aoc::core::DecisionLog* log = aoc::core::currentDecisionLog();
            const bool logActive = log != nullptr && log->active();
            std::vector<std::pair<TechId, int32_t>> scored;
            if (logActive) { scored.reserve(available.size()); }

            for (const TechId& tid : available) {
                const TechDef& def = techDef(tid);
                int32_t score = 0;

                if (hardAI) {
                    score += 1000;
                }

                // Priority 1: Techs that unlock buildings we don't yet have
                if (!def.unlockedBuildings.empty()) {
                    bool unlocksNew = false;
                    for (const BuildingId& bid : def.unlockedBuildings) {
                        if (ownedBuildings.find(bid.value) == ownedBuildings.end()) {
                            unlocksNew = true;
                            if (bid.value < BUILDING_DEFS.size() &&
                                BUILDING_DEFS[bid.value].productionBonus > 0) {
                                score += 3000;
                            }
                        }
                    }
                    score += unlocksNew ? 10000 : 5000;
                }

                // Priority 2: Techs that unlock military units when threatened
                if (!def.unlockedUnits.empty()) {
                    score += threatened ? 8000 : 3000;
                    for (const UnitTypeId& uid : def.unlockedUnits) {
                        if (uid.value < UNIT_TYPE_DEFS.size()) {
                            score += UNIT_TYPE_DEFS[uid.value].combatStrength * 10;
                        }
                    }
                }

                // Priority 3: Techs that unlock goods (resource processing)
                if (!def.unlockedGoods.empty()) {
                    score += 2000 + static_cast<int32_t>(def.unlockedGoods.size()) * 500;
                }

                score += ownedCityCount * 100;

                // Leader personality tech bias. Each category contributes
                // at most ONE multiplier per tech — the previous per-building
                // loop could multiply techEconomic two or three times when a
                // single tech unlocked multiple economic buildings, yielding
                // score * techEconomic^N and wildly overweighting diverse
                // techs versus single-unlock ones.
                if (!def.unlockedUnits.empty()) {
                    score = static_cast<int32_t>(static_cast<float>(score) * beh.techMilitary);
                }
                if (!def.unlockedBuildings.empty()) {
                    bool hasEconomic   = false;
                    bool hasIndustrial = false;
                    for (const BuildingId& bid : def.unlockedBuildings) {
                        if (bid.value == 6 || bid.value == 20 || bid.value == 21 || bid.value == 24) {
                            hasEconomic = true;
                        }
                        if (bid.value == 3 || bid.value == 5 || bid.value == 10 || bid.value == 11) {
                            hasIndustrial = true;
                        }
                    }
                    if (hasEconomic) {
                        score = static_cast<int32_t>(static_cast<float>(score) * beh.techEconomic);
                    }
                    if (hasIndustrial) {
                        score = static_cast<int32_t>(static_cast<float>(score) * beh.techIndustrial);
                    }
                }
                if (def.era.value >= 5) {
                    score = static_cast<int32_t>(static_cast<float>(score) * beh.techInformation);
                }

                // Expanded techs (no unlocks) still have strategic value: they enable
                // terrain projects (canals, tunnels) and serve as prerequisites for
                // important military/economic branches. Without this base score,
                // expanded techs would never be researched because they score near-zero
                // while base techs with unlocks score 10000+.
                if (def.unlockedBuildings.empty() && def.unlockedUnits.empty()
                    && def.unlockedGoods.empty()) {
                    score += 8000 + static_cast<int32_t>(def.era.value) * 1000;
                    // Industrial+ era techs scaled by industrial tech preference
                    if (def.era.value >= 4) {
                        score = static_cast<int32_t>(static_cast<float>(score) * beh.techIndustrial);
                    }
                }

                // Prefer cheaper techs as tiebreaker (scaled to 10% of cost so
                // high-cost techs aren't disproportionately penalized)
                score -= def.researchCost / 10;

                if (score > bestScore) {
                    bestScore = score;
                    best = tid;
                }
                if (logActive) { scored.emplace_back(tid, score); }
            }

            tech.currentResearch = best;
            LOG_INFO("AI %u Researching: %.*s",
                     static_cast<unsigned>(this->m_player),
                     static_cast<int>(techDef(best).name.size()),
                     techDef(best).name.data());

            if (logActive) {
                std::sort(scored.begin(), scored.end(),
                          [](const auto& a, const auto& b) { return a.second > b.second; });
                std::vector<aoc::core::ResearchAlt> alts;
                alts.reserve(3);
                for (const auto& p : scored) {
                    if (p.first.value == best.value) { continue; }
                    aoc::core::ResearchAlt a{};
                    a.techId = p.first.value;
                    a.score  = static_cast<float>(p.second);
                    alts.push_back(a);
                    if (alts.size() >= 3) { break; }
                }
                log->logResearch(
                    static_cast<uint16_t>(gameState.currentTurn()),
                    static_cast<uint8_t>(this->m_player),
                    best.value, static_cast<float>(bestScore),
                    std::span<const aoc::core::ResearchAlt>(alts.data(), alts.size()));
            }
        }
    }

    // Civic research selection
    PlayerCivicComponent& civic = myPlayer->civics();
    if (!civic.currentResearch.isValid()) {
        const uint16_t count = civicCount();
        CivicId best{};
        int32_t bestScore = std::numeric_limits<int32_t>::min();
        for (uint16_t i = 0; i < count; ++i) {
            CivicId id{i};
            if (civic.canResearch(id)) {
                const CivicDef& def = civicDef(id);
                int32_t score = 0;
                if (!def.unlockedGovernmentIds.empty()) {
                    score += 5000;
                }
                if (!def.unlockedPolicyIds.empty()) {
                    score += 3000;
                }
                // Foreign Trade (CivicId{2}) enables Traders -- high early priority
                if (id == CivicId{2}) {
                    score += 6000;
                }
                score -= def.cultureCost;

                if (score > bestScore) {
                    bestScore = score;
                    best = id;
                }
            }
        }
        if (best.isValid()) {
            civic.currentResearch = best;
        }
    }
}

} // namespace aoc::sim::ai
