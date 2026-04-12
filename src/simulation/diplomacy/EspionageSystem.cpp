/**
 * @file EspionageSystem.cpp
 * @brief Spy mission execution logic.
 *
 * Spy state lives on Unit::spy(). Spies are iterated through player->units()
 * filtered by unit class. All ECS World access has been removed.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/diplomacy/EspionageSystem.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>
#include <vector>

namespace aoc::sim {

void processSpyMissions(aoc::game::GameState& gameState, aoc::Random& rng) {
    // Collect all spy units across all players (copy pointers; mutations are safe
    // because we do not add or remove units from within this function except via
    // player->removeUnit(), which we call after the iteration below).
    struct SpyEntry {
        aoc::game::Player* player;
        aoc::game::Unit*   unit;
    };
    std::vector<SpyEntry> spyUnits;

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerPtr->units()) {
            // A unit is treated as an active spy when it has non-default spy state:
            // either it has turns remaining on a mission, or it was explicitly
            // assigned to CounterIntelligence duty.
            const SpyComponent& spy = unitPtr->spy();
            if (spy.turnsRemaining > 0
                || spy.currentMission == SpyMission::CounterIntelligence) {
                spyUnits.push_back({playerPtr.get(), unitPtr.get()});
            }
        }
    }

    std::vector<aoc::game::Unit*> toRemove;

    for (const SpyEntry& entry : spyUnits) {
        aoc::game::Unit* unitPtr = entry.unit;
        SpyComponent&    spy     = unitPtr->spy();

        // CounterIntelligence is passive; skip turn processing
        if (spy.currentMission == SpyMission::CounterIntelligence) {
            continue;
        }

        // Decrement turns remaining
        if (spy.turnsRemaining > 0) {
            --spy.turnsRemaining;
        }

        if (spy.turnsRemaining > 0) {
            continue;  // Still in progress
        }

        // Mission complete -- roll for success
        const float baseRate     = baseMissionSuccessRate(spy.currentMission);
        const float successChance = baseRate * (1.0f + static_cast<float>(spy.experience) * 0.05f);
        const bool  success      = rng.chance(successChance);

        if (success) {
            switch (spy.currentMission) {
                case SpyMission::GatherIntelligence: {
                    // Actual fog reveal happens via normal updateVisibility next turn.
                    // We mark experience here.
                    LOG_INFO("Spy (Player %u) gathered intelligence at (%d,%d)",
                             static_cast<unsigned>(spy.owner),
                             spy.location.q, spy.location.r);
                    ++spy.experience;
                    break;
                }

                case SpyMission::StealTechnology: {
                    // Add 20% of the current research cost as progress for the spy's owner.
                    aoc::game::Player* ownerPlayer = gameState.player(spy.owner);
                    if (ownerPlayer != nullptr) {
                        PlayerTechComponent& tech = ownerPlayer->tech();
                        if (tech.currentResearch.isValid()) {
                            const TechDef& def = techDef(tech.currentResearch);
                            const float bonus = 0.20f * static_cast<float>(def.researchCost);
                            tech.researchProgress += bonus;
                            LOG_INFO("Spy (Player %u) stole technology: +%.0f science toward %.*s",
                                     static_cast<unsigned>(spy.owner),
                                     static_cast<double>(bonus),
                                     static_cast<int>(def.name.size()),
                                     def.name.data());
                        }
                    }
                    ++spy.experience;
                    break;
                }

                case SpyMission::SabotageProduction: {
                    // Reduce target city's production progress by 50%.
                    for (const std::unique_ptr<aoc::game::Player>& targetPlayer : gameState.players()) {
                        if (targetPlayer->id() == spy.owner) { continue; }
                        aoc::game::City* city = targetPlayer->cityAt(spy.location);
                        if (city == nullptr) { continue; }
                        const float reduction = city->productionProgress() * 0.5f;
                        city->setProductionProgress(city->productionProgress() - reduction);
                        LOG_INFO("Spy (Player %u) sabotaged production in %s (-%.0f)",
                                 static_cast<unsigned>(spy.owner),
                                 city->name().c_str(),
                                 static_cast<double>(reduction));
                        break;
                    }
                    ++spy.experience;
                    break;
                }

                case SpyMission::Recruit: {
                    // Award gold to the spy's owner.
                    aoc::game::Player* ownerPlayer = gameState.player(spy.owner);
                    if (ownerPlayer != nullptr) {
                        constexpr CurrencyAmount RECRUIT_GOLD_BONUS = 100;
                        ownerPlayer->addGold(RECRUIT_GOLD_BONUS);
                        LOG_INFO("Spy (Player %u) recruited assets: +%lld gold",
                                 static_cast<unsigned>(spy.owner),
                                 static_cast<long long>(RECRUIT_GOLD_BONUS));
                    }
                    spy.experience += 2;
                    break;
                }

                case SpyMission::CounterIntelligence:
                    break;  // Passive; handled elsewhere
            }
        } else {
            // Failure
            LOG_WARN("Spy (Player %u) failed mission %u at (%d,%d)",
                     static_cast<unsigned>(spy.owner),
                     static_cast<unsigned>(spy.currentMission),
                     spy.location.q, spy.location.r);

            // 30% chance spy is captured (destroyed)
            constexpr float CAPTURE_CHANCE = 0.30f;
            if (rng.chance(CAPTURE_CHANCE)) {
                LOG_WARN("Spy (Player %u) was captured and eliminated!",
                         static_cast<unsigned>(spy.owner));
                toRemove.push_back(unitPtr);
                continue;
            }
        }

        // Reset for next mission cycle
        spy.turnsRemaining = missionDuration(spy.currentMission);
    }

    // Remove captured spies via the owning player.
    for (aoc::game::Unit* captured : toRemove) {
        aoc::game::Player* ownerPlayer = gameState.player(captured->spy().owner);
        if (ownerPlayer != nullptr) {
            ownerPlayer->removeUnit(captured);
        }
    }
}

ErrorCode assignSpyMission(aoc::game::GameState& /*gameState*/,
                           aoc::game::Unit& spyUnit,
                           SpyMission mission) {
    SpyComponent& spy = spyUnit.spy();
    spy.currentMission  = mission;
    spy.turnsRemaining  = missionDuration(mission);
    spy.isRevealed      = false;

    LOG_INFO("Spy (Player %u) assigned to mission %u at (%d,%d), duration %d turns",
             static_cast<unsigned>(spy.owner),
             static_cast<unsigned>(mission),
             spy.location.q, spy.location.r,
             spy.turnsRemaining);

    return ErrorCode::Ok;
}

} // namespace aoc::sim
