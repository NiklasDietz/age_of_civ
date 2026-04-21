/**
 * @file EurekaBoost.cpp
 * @brief Eureka/Inspiration boost definitions and trigger logic.
 *
 * Migrated from ECS to GameState object model.
 */

#include "aoc/simulation/tech/EurekaBoost.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/game/Player.hpp"
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

    // ----- Civic boosts (techId invalid, civicId valid) -----

    // Index 15: Foreign Trade -- "Meet another civilization" (50% boost)
    boosts.push_back({15, TechId{}, CivicId{2}, EurekaCondition::MeetCivilization,
                      "Meet another civilization", 0.5f});

    // Index 16: Code of Laws -- "Found a city"
    boosts.push_back({16, TechId{}, CivicId{0}, EurekaCondition::FoundCity,
                      "Found a city", 0.4f});

    // Index 17: Military Tradition -- "Kill a unit"
    boosts.push_back({17, TechId{}, CivicId{5}, EurekaCondition::KillUnit,
                      "Win a battle", 0.5f});

    // Index 18: Trade Routes -- "Meet another civilization"
    boosts.push_back({18, TechId{}, CivicId{4}, EurekaCondition::MeetCivilization,
                      "Meet a second civilization", 0.4f});

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

void checkEurekaConditions(aoc::game::Player& player,
                           EurekaCondition triggered) {
    const std::vector<EurekaBoostDef>& boosts = getBoostTable();

    PlayerEurekaComponent& eurekaComp = player.eureka();
    PlayerTechComponent& techComp = player.tech();
    PlayerCivicComponent& civicComp = player.civics();

    for (const EurekaBoostDef& boost : boosts) {
        if (boost.condition != triggered) {
            continue;
        }

        if (eurekaComp.hasTriggered(boost.boostIndex)) {
            continue;
        }

        // Apply boost to relevant tech or civic. If the player is not
        // currently researching the matching tech/civic, bank the boost
        // via pendingBoosts so it gets applied on research start.
        if (boost.techId.isValid()) {
            if (techComp.hasResearched(boost.techId)) {
                eurekaComp.markTriggered(boost.boostIndex);
                continue;
            }

            const TechDef& def = techDef(boost.techId);
            const float boostAmount = boost.boostFraction * static_cast<float>(def.researchCost);

            if (techComp.currentResearch == boost.techId) {
                techComp.researchProgress += boostAmount;
                LOG_INFO("Eureka! Player %u: %.*s (+%.0f%% toward %.*s)",
                         static_cast<unsigned>(player.id()),
                         static_cast<int>(boost.description.size()),
                         boost.description.data(),
                         static_cast<double>(boost.boostFraction * 100.0f),
                         static_cast<int>(def.name.size()),
                         def.name.data());
            } else {
                eurekaComp.markPending(boost.boostIndex);
                LOG_INFO("Eureka banked! Player %u: %.*s (pending for %.*s)",
                         static_cast<unsigned>(player.id()),
                         static_cast<int>(boost.description.size()),
                         boost.description.data(),
                         static_cast<int>(def.name.size()),
                         def.name.data());
            }

            eurekaComp.markTriggered(boost.boostIndex);
        } else if (boost.civicId.isValid()) {
            if (civicComp.hasCompleted(boost.civicId)) {
                eurekaComp.markTriggered(boost.boostIndex);
                continue;
            }

            const CivicDef& def = civicDef(boost.civicId);
            const float boostAmount = boost.boostFraction * static_cast<float>(def.cultureCost);

            if (civicComp.currentResearch == boost.civicId) {
                civicComp.researchProgress += boostAmount;
                LOG_INFO("Inspiration! Player %u: %.*s (+%.0f%% toward %.*s)",
                         static_cast<unsigned>(player.id()),
                         static_cast<int>(boost.description.size()),
                         boost.description.data(),
                         static_cast<double>(boost.boostFraction * 100.0f),
                         static_cast<int>(def.name.size()),
                         def.name.data());
            } else {
                eurekaComp.markPending(boost.boostIndex);
                LOG_INFO("Inspiration banked! Player %u: %.*s (pending for %.*s)",
                         static_cast<unsigned>(player.id()),
                         static_cast<int>(boost.description.size()),
                         boost.description.data(),
                         static_cast<int>(def.name.size()),
                         def.name.data());
            }

            eurekaComp.markTriggered(boost.boostIndex);
        }
    }
}

void consumePendingEurekaBoosts(aoc::game::Player& player) {
    PlayerEurekaComponent& eurekaComp = player.eureka();
    if (eurekaComp.pendingBoosts.none()) { return; }

    PlayerTechComponent& techComp   = player.tech();
    PlayerCivicComponent& civicComp = player.civics();

    const std::vector<EurekaBoostDef>& boosts = getBoostTable();
    for (const EurekaBoostDef& boost : boosts) {
        if (!eurekaComp.isPending(boost.boostIndex)) { continue; }

        if (boost.techId.isValid()
            && techComp.currentResearch == boost.techId
            && !techComp.hasResearched(boost.techId)) {
            const TechDef& def = techDef(boost.techId);
            const float boostAmount = boost.boostFraction * static_cast<float>(def.researchCost);
            techComp.researchProgress += boostAmount;
            eurekaComp.clearPending(boost.boostIndex);
            LOG_INFO("Pending eureka consumed! Player %u: +%.0f%% toward %.*s",
                     static_cast<unsigned>(player.id()),
                     static_cast<double>(boost.boostFraction * 100.0f),
                     static_cast<int>(def.name.size()),
                     def.name.data());
        } else if (boost.civicId.isValid()
                   && civicComp.currentResearch == boost.civicId
                   && !civicComp.hasCompleted(boost.civicId)) {
            const CivicDef& def = civicDef(boost.civicId);
            const float boostAmount = boost.boostFraction * static_cast<float>(def.cultureCost);
            civicComp.researchProgress += boostAmount;
            eurekaComp.clearPending(boost.boostIndex);
            LOG_INFO("Pending inspiration consumed! Player %u: +%.0f%% toward %.*s",
                     static_cast<unsigned>(player.id()),
                     static_cast<double>(boost.boostFraction * 100.0f),
                     static_cast<int>(def.name.size()),
                     def.name.data());
        }
    }
}

} // namespace aoc::sim
