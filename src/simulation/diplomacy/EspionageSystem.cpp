/**
 * @file EspionageSystem.cpp
 * @brief Spy mission execution logic.
 */

#include "aoc/simulation/diplomacy/EspionageSystem.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>
#include <vector>

namespace aoc::sim {

void processSpyMissions(aoc::ecs::World& world, aoc::Random& rng) {
    // Collect spy entities first to avoid mutation during iteration
    std::vector<EntityId> spyEntities;

    aoc::ecs::ComponentPool<SpyComponent>* spyPool = world.getPool<SpyComponent>();
    if (spyPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < spyPool->size(); ++i) {
        spyEntities.push_back(spyPool->entities()[i]);
    }

    std::vector<EntityId> toDestroy;

    for (EntityId spyEntity : spyEntities) {
        if (!world.isAlive(spyEntity)) {
            continue;
        }

        SpyComponent& spy = world.getComponent<SpyComponent>(spyEntity);

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
        const float baseRate = baseMissionSuccessRate(spy.currentMission);
        const float successChance = baseRate * (1.0f + static_cast<float>(spy.experience) * 0.05f);
        const bool success = rng.chance(successChance);

        if (success) {
            // Apply mission effect
            switch (spy.currentMission) {
                case SpyMission::GatherIntelligence: {
                    // Reveal tiles within 3 hexes of the spy's city for the spy's owner
                    LOG_INFO("Spy (Player %u) gathered intelligence at (%d,%d)",
                             static_cast<unsigned>(spy.owner),
                             spy.location.q, spy.location.r);
                    // Note: Actual fog reveal requires FogOfWar reference; the spy's owner
                    // gets visibility updated next turn via normal updateVisibility.
                    // We mark the spy as experienced instead.
                    ++spy.experience;
                    break;
                }

                case SpyMission::StealTechnology: {
                    // Add 20% of a random incomplete tech's cost as progress
                    world.forEach<PlayerTechComponent>(
                        [&spy](EntityId /*id*/, PlayerTechComponent& tech) {
                            if (tech.owner != spy.owner) {
                                return;
                            }
                            if (!tech.currentResearch.isValid()) {
                                return;
                            }
                            const TechDef& def = techDef(tech.currentResearch);
                            const float bonus = 0.20f * static_cast<float>(def.researchCost);
                            tech.researchProgress += bonus;
                            LOG_INFO("Spy (Player %u) stole technology: +%.0f science toward %.*s",
                                     static_cast<unsigned>(spy.owner),
                                     static_cast<double>(bonus),
                                     static_cast<int>(def.name.size()),
                                     def.name.data());
                        });
                    ++spy.experience;
                    break;
                }

                case SpyMission::SabotageProduction: {
                    // Reduce target city's production by 50% for 3 turns
                    // For simplicity, we reduce the current production progress
                    world.forEach<CityComponent>(
                        [&spy](EntityId /*id*/, CityComponent& city) {
                            if (city.location == spy.location && city.owner != spy.owner) {
                                const float reduction = city.productionProgress * 0.5f;
                                city.productionProgress -= reduction;
                                LOG_INFO("Spy (Player %u) sabotaged production in %s (-%.0f)",
                                         static_cast<unsigned>(spy.owner),
                                         city.name.c_str(),
                                         static_cast<double>(reduction));
                            }
                        });
                    ++spy.experience;
                    break;
                }

                case SpyMission::Recruit: {
                    // Very difficult; for now just award gold
                    world.forEach<PlayerEconomyComponent>(
                        [&spy](EntityId /*id*/, PlayerEconomyComponent& econ) {
                            if (econ.owner == spy.owner) {
                                constexpr CurrencyAmount RECRUIT_GOLD_BONUS = 100;
                                econ.treasury += RECRUIT_GOLD_BONUS;
                                LOG_INFO("Spy (Player %u) recruited assets: +%lld gold",
                                         static_cast<unsigned>(spy.owner),
                                         static_cast<long long>(RECRUIT_GOLD_BONUS));
                            }
                        });
                    spy.experience += 2;
                    break;
                }

                case SpyMission::CounterIntelligence: {
                    // Passive; handled elsewhere
                    break;
                }
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
                toDestroy.push_back(spyEntity);
                continue;
            }
        }

        // Reset for next mission cycle
        spy.turnsRemaining = missionDuration(spy.currentMission);
    }

    // Destroy captured spies
    for (EntityId entity : toDestroy) {
        if (world.isAlive(entity)) {
            world.destroyEntity(entity);
        }
    }
}

ErrorCode assignSpyMission(aoc::ecs::World& world,
                           EntityId spy,
                           SpyMission mission) {
    if (!world.isAlive(spy)) {
        return ErrorCode::EntityNotFound;
    }

    SpyComponent* spyComp = world.tryGetComponent<SpyComponent>(spy);
    if (spyComp == nullptr) {
        return ErrorCode::ComponentNotFound;
    }

    spyComp->currentMission = mission;
    spyComp->turnsRemaining = missionDuration(mission);
    spyComp->isRevealed = false;

    LOG_INFO("Spy (Player %u) assigned to mission %u at (%d,%d), duration %d turns",
             static_cast<unsigned>(spyComp->owner),
             static_cast<unsigned>(mission),
             spyComp->location.q, spyComp->location.r,
             spyComp->turnsRemaining);

    return ErrorCode::Ok;
}

} // namespace aoc::sim
