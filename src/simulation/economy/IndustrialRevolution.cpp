/**
 * @file IndustrialRevolution.cpp
 * @brief Industrial revolution detection and progression.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

bool checkIndustrialRevolution(aoc::game::GameState& gameState, PlayerId player,
                               TurnNumber currentTurn) {
    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return false;
    }

    PlayerIndustrialComponent& ind = playerObj->industrial();

    uint8_t nextRevId = static_cast<uint8_t>(ind.currentRevolution) + 1;
    if (nextRevId > static_cast<uint8_t>(IndustrialRevolutionId::Fifth)) {
        return false;
    }

    const RevolutionDef& rev = REVOLUTION_DEFS[nextRevId - 1];

    // Check tech requirements
    const PlayerTechComponent& playerTech = playerObj->tech();
    for (int32_t i = 0; i < 3; ++i) {
        TechId reqTech = rev.requirements.requiredTechs[i];
        if (reqTech.isValid() && !playerTech.hasResearched(reqTech)) {
            return false;
        }
    }

    // Check city count requirement
    int32_t cityCount = static_cast<int32_t>(playerObj->cities().size());
    if (cityCount < rev.requirements.minCityCount) {
        return false;
    }

    // Check resource requirements. 2026-05-03: also accept "ever supplied"
    // via economy.totalSupply, not just current stockpile. Civs that produce
    // a good and consume it the same turn (e.g. Steel feeding Tools/Tank
    // production) used to fail the snapshot check despite having the
    // manufacturing capability. Tech reach already gates capability; goods
    // check now confirms the chain ran at least once.
    const aoc::sim::PlayerEconomyComponent& econ = playerObj->economy();
    for (int32_t i = 0; i < 3; ++i) {
        uint16_t reqGood = rev.requirements.requiredGoods[i];
        if (reqGood == 0xFFFF) { continue; }

        bool found = false;
        // Path A: in any city's stockpile right now.
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
            if (cityPtr == nullptr) { continue; }
            if (cityPtr->stockpile().getAmount(reqGood) > 0) {
                found = true;
                break;
            }
        }
        // Path B: or recently supplied at the player level (capture goods
        // produced and immediately consumed).
        if (!found) {
            auto it = econ.totalSupply.find(reqGood);
            if (it != econ.totalSupply.end() && it->second > 0) {
                found = true;
            }
        }
        if (!found) {
            return false;
        }
    }

    ind.currentRevolution    = static_cast<IndustrialRevolutionId>(nextRevId);
    ind.turnAchieved[nextRevId] = static_cast<int32_t>(currentTurn);

    LOG_INFO("Player %u achieved the %.*s (Industrial Revolution #%u) on turn %d!",
             static_cast<unsigned>(player),
             static_cast<int>(rev.name.size()), rev.name.data(),
             static_cast<unsigned>(nextRevId),
             static_cast<int>(currentTurn));

    return true;
}

float revolutionPollutionMultiplier(const PlayerIndustrialComponent& ind) {
    float mult = 1.0f;
    for (uint8_t r = 1; r <= static_cast<uint8_t>(ind.currentRevolution); ++r) {
        mult *= REVOLUTION_DEFS[r - 1].bonuses.pollutionMultiplier;
    }
    return mult;
}

} // namespace aoc::sim
