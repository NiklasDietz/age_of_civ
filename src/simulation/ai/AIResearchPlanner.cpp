/**
 * @file AIResearchPlanner.cpp
 * @brief AI tech and civic research selection logic.
 *
 * Evaluates available technologies and civics, scoring them by what they
 * unlock (buildings, units, goods) and the current threat level, then
 * queues the highest-scoring option.
 */

#include "aoc/simulation/ai/AIResearchPlanner.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/ecs/World.hpp"

#include <limits>
#include <unordered_set>

namespace aoc::sim::ai {

// ============================================================================
// Helper: Count military units for threat assessment.
// ============================================================================

static int32_t countMilitaryUnits(const aoc::ecs::World& world, PlayerId player) {
    int32_t count = 0;
    const aoc::ecs::ComponentPool<UnitComponent>* unitPool =
        world.getPool<UnitComponent>();
    if (unitPool == nullptr) {
        return count;
    }
    for (uint32_t i = 0; i < unitPool->size(); ++i) {
        if (unitPool->data()[i].owner != player) {
            continue;
        }
        const UnitTypeDef& def = unitTypeDef(unitPool->data()[i].typeId);
        if (isMilitary(def.unitClass)) {
            ++count;
        }
    }
    return count;
}

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

void AIResearchPlanner::selectResearch(aoc::ecs::World& world) {
    const int32_t militaryCount = countMilitaryUnits(world, this->m_player);
    const bool threatened = militaryCount < 3;

    // Count owned cities for scaling thresholds
    int32_t ownedCityCount = 0;
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].owner == this->m_player) {
                ++ownedCityCount;
            }
        }
    }

    // Collect set of buildings already built across all cities
    std::unordered_set<uint16_t> ownedBuildings;
    const aoc::ecs::ComponentPool<CityDistrictsComponent>* distPool =
        world.getPool<CityDistrictsComponent>();
    if (distPool != nullptr && cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].owner != this->m_player) {
                continue;
            }
            const EntityId cityEntity = cityPool->entities()[i];
            const CityDistrictsComponent* districts =
                world.tryGetComponent<CityDistrictsComponent>(cityEntity);
            if (districts != nullptr) {
                for (const CityDistrictsComponent::PlacedDistrict& d : districts->districts) {
                    for (const BuildingId& bid : d.buildings) {
                        ownedBuildings.insert(bid.value);
                    }
                }
            }
        }
    }

    world.forEach<PlayerTechComponent>(
        [this, threatened, &ownedBuildings, ownedCityCount, &world](
            EntityId, PlayerTechComponent& tech) {
            if (tech.owner != this->m_player) {
                return;
            }
            if (tech.currentResearch.isValid()) {
                return;  // Already researching
            }
            const std::vector<TechId> available = tech.availableTechs();
            if (available.empty()) {
                return;
            }

            TechId best = available[0];
            int32_t bestScore = std::numeric_limits<int32_t>::min();

            const bool hardAI = (this->m_difficulty == aoc::ui::AIDifficulty::Hard);

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

                // Leader personality tech bias
                CivId myCiv = 0;
                const aoc::ecs::ComponentPool<PlayerCivilizationComponent>* cp =
                    world.getPool<PlayerCivilizationComponent>();
                if (cp != nullptr) {
                    for (uint32_t cc = 0; cc < cp->size(); ++cc) {
                        if (cp->data()[cc].owner == this->m_player) {
                            myCiv = cp->data()[cc].civId; break;
                        }
                    }
                }
                const LeaderBehavior& beh = leaderPersonality(myCiv).behavior;

                if (!def.unlockedUnits.empty()) {
                    score = static_cast<int32_t>(static_cast<float>(score) * beh.techMilitary);
                }
                if (!def.unlockedBuildings.empty()) {
                    for (const BuildingId& bid : def.unlockedBuildings) {
                        if (bid.value == 6 || bid.value == 20 || bid.value == 21 || bid.value == 24) {
                            score = static_cast<int32_t>(static_cast<float>(score) * beh.techEconomic);
                        }
                        if (bid.value == 3 || bid.value == 5 || bid.value == 10 || bid.value == 11) {
                            score = static_cast<int32_t>(static_cast<float>(score) * beh.techIndustrial);
                        }
                    }
                }
                if (def.era.value >= 5) {
                    score = static_cast<int32_t>(static_cast<float>(score) * beh.techInformation);
                }

                // Prefer cheaper techs as tiebreaker
                score -= def.researchCost;

                if (score > bestScore) {
                    bestScore = score;
                    best = tid;
                }
            }

            tech.currentResearch = best;
            LOG_INFO("AI %u Researching: %.*s",
                     static_cast<unsigned>(this->m_player),
                     static_cast<int>(techDef(best).name.size()),
                     techDef(best).name.data());
        });

    // Civic research
    world.forEach<PlayerCivicComponent>(
        [this](EntityId, PlayerCivicComponent& civic) {
            if (civic.owner != this->m_player) {
                return;
            }
            if (civic.currentResearch.isValid()) {
                return;
            }
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
        });
}

} // namespace aoc::sim::ai
