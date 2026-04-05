/**
 * @file EurekaBoost.cpp
 * @brief Eureka/Inspiration boost definitions and trigger logic.
 */

#include "aoc/simulation/tech/EurekaBoost.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>

namespace aoc::sim {

namespace {

std::vector<EurekaBoostDef> buildBoostTable() {
    std::vector<EurekaBoostDef> boosts;

    // Index 0: Mining -- "Build a quarry"
    boosts.push_back({0, TechId{0}, CivicId{}, EurekaCondition::BuildQuarry,
                      "Build a quarry", 0.5f});

    // Index 1: Writing -- "Meet another civilization"
    boosts.push_back({1, TechId{3}, CivicId{}, EurekaCondition::MeetCivilization,
                      "Meet another civilization", 0.5f});

    // Index 2: Animal Husbandry -- "Found a city"
    boosts.push_back({2, TechId{1}, CivicId{}, EurekaCondition::FoundCity,
                      "Found a city", 0.4f});

    // Index 3: Bronze Working -- "Kill a unit"
    boosts.push_back({3, TechId{4}, CivicId{}, EurekaCondition::KillUnit,
                      "Kill an enemy unit", 0.5f});

    // Index 4: Currency -- "Found a city" (simplified)
    boosts.push_back({4, TechId{5}, CivicId{}, EurekaCondition::FoundCity,
                      "Found a second city", 0.4f});

    // Index 5: Engineering -- "Build an ancient wonder"
    boosts.push_back({5, TechId{6}, CivicId{}, EurekaCondition::BuildWonder,
                      "Build a wonder", 0.5f});

    // Index 6: Pottery -- "Train a unit"
    boosts.push_back({6, TechId{2}, CivicId{}, EurekaCondition::TrainUnit,
                      "Train a unit", 0.5f});

    // Index 7: Apprenticeship -- "Build a campus district"
    boosts.push_back({7, TechId{7}, CivicId{}, EurekaCondition::BuildCampus,
                      "Build a campus", 0.5f});

    // Index 8: Electricity -- "Build a campus district" (simplified from 3 factories)
    boosts.push_back({8, TechId{14}, CivicId{}, EurekaCondition::BuildCampus,
                      "Build 3 campuses", 0.4f});

    // Index 9: Metallurgy -- "Kill a unit"
    boosts.push_back({9, TechId{8}, CivicId{}, EurekaCondition::KillUnit,
                      "Kill a unit with a bronze unit", 0.5f});

    // Index 10: Banking -- "Found a city"
    boosts.push_back({10, TechId{9}, CivicId{}, EurekaCondition::FoundCity,
                      "Establish a trade route", 0.4f});

    // Index 11: Gunpowder -- "Build a wonder"
    boosts.push_back({11, TechId{10}, CivicId{}, EurekaCondition::BuildWonder,
                      "Build a medieval wonder", 0.5f});

    // Index 12: Industrialization -- "Build a quarry"
    boosts.push_back({12, TechId{11}, CivicId{}, EurekaCondition::BuildQuarry,
                      "Build a quarry and mine", 0.4f});

    // Index 13: Computers -- "Research a tech"
    boosts.push_back({13, TechId{16}, CivicId{}, EurekaCondition::ResearchTech,
                      "Research Electricity", 0.5f});

    // Index 14: Mass Production -- "Train a unit"
    boosts.push_back({14, TechId{15}, CivicId{}, EurekaCondition::TrainUnit,
                      "Train a naval unit", 0.5f});

    return boosts;
}

const std::vector<EurekaBoostDef>& getBoostTable() {
    static const std::vector<EurekaBoostDef> table = buildBoostTable();
    return table;
}

} // anonymous namespace

const std::vector<EurekaBoostDef>& getEurekaBoosts() {
    return getBoostTable();
}

void checkEurekaConditions(aoc::ecs::World& world,
                           PlayerId player,
                           EurekaCondition triggered) {
    const std::vector<EurekaBoostDef>& boosts = getBoostTable();

    // Find the player's eureka and tech components
    PlayerEurekaComponent* eurekaComp = nullptr;
    PlayerTechComponent* techComp = nullptr;
    PlayerCivicComponent* civicComp = nullptr;

    world.forEach<PlayerEurekaComponent>(
        [player, &eurekaComp](EntityId /*id*/, PlayerEurekaComponent& eureka) {
            if (eureka.owner == player) {
                eurekaComp = &eureka;
            }
        });

    world.forEach<PlayerTechComponent>(
        [player, &techComp](EntityId /*id*/, PlayerTechComponent& tech) {
            if (tech.owner == player) {
                techComp = &tech;
            }
        });

    world.forEach<PlayerCivicComponent>(
        [player, &civicComp](EntityId /*id*/, PlayerCivicComponent& civic) {
            if (civic.owner == player) {
                civicComp = &civic;
            }
        });

    if (eurekaComp == nullptr || techComp == nullptr) {
        return;
    }

    for (const EurekaBoostDef& boost : boosts) {
        if (boost.condition != triggered) {
            continue;
        }

        if (eurekaComp->hasTriggered(boost.boostIndex)) {
            continue;
        }

        // Apply boost to relevant tech or civic
        if (boost.techId.isValid()) {
            // Check if tech is not yet completed
            if (techComp->hasResearched(boost.techId)) {
                continue;
            }

            const TechDef& def = techDef(boost.techId);
            float boostAmount = boost.boostFraction * static_cast<float>(def.researchCost);

            // If this is the currently researched tech, add to progress directly
            if (techComp->currentResearch == boost.techId) {
                techComp->researchProgress += boostAmount;
            }
            // Otherwise still mark it as triggered (the boost is conceptually
            // "banked" for when the player starts researching this tech).
            // For simplicity, if the tech is the current research, apply immediately.

            eurekaComp->markTriggered(boost.boostIndex);
            LOG_INFO("Eureka! Player %u: %.*s (+%.0f%% toward %.*s)",
                     static_cast<unsigned>(player),
                     static_cast<int>(boost.description.size()),
                     boost.description.data(),
                     static_cast<double>(boost.boostFraction * 100.0f),
                     static_cast<int>(def.name.size()),
                     def.name.data());
        } else if (boost.civicId.isValid() && civicComp != nullptr) {
            if (civicComp->hasCompleted(boost.civicId)) {
                continue;
            }

            const CivicDef& def = civicDef(boost.civicId);
            float boostAmount = boost.boostFraction * static_cast<float>(def.cultureCost);

            if (civicComp->currentResearch == boost.civicId) {
                civicComp->researchProgress += boostAmount;
            }

            eurekaComp->markTriggered(boost.boostIndex);
            LOG_INFO("Inspiration! Player %u: %.*s (+%.0f%% toward %.*s)",
                     static_cast<unsigned>(player),
                     static_cast<int>(boost.description.size()),
                     boost.description.data(),
                     static_cast<double>(boost.boostFraction * 100.0f),
                     static_cast<int>(def.name.size()),
                     def.name.data());
        }
    }
}

} // namespace aoc::sim
