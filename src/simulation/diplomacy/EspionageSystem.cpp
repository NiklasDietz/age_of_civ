/**
 * @file EspionageSystem.cpp
 * @brief Spy mission execution logic with economic espionage.
 *
 * Processes all active spy missions each turn. Handles success/failure rolls,
 * graduated failure outcomes (escape/identified/captured/killed), experience
 * gain, and level-up with promotion selection.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/diplomacy/EspionageSystem.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>
#include <vector>

namespace aoc::sim {

// ============================================================================
// Helpers
// ============================================================================

/// Find the city at a spy's location owned by any enemy player.
static aoc::game::City* findEnemyCityAt(aoc::game::GameState& gameState,
                                         PlayerId spyOwner,
                                         aoc::hex::AxialCoord location) {
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        if (player->id() == spyOwner) { continue; }
        aoc::game::City* city = player->cityAt(location);
        if (city != nullptr) { return city; }
    }
    return nullptr;
}

/// Find the player who owns a city at the given location (excluding spy owner).
static aoc::game::Player* findCityOwner(aoc::game::GameState& gameState,
                                          PlayerId spyOwner,
                                          aoc::hex::AxialCoord location) {
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        if (player->id() == spyOwner) { continue; }
        if (player->cityAt(location) != nullptr) { return player.get(); }
    }
    return nullptr;
}

/// Check if any counter-spy is defending a location.
static int32_t counterSpyLevel(const aoc::game::GameState& gameState,
                                const aoc::map::HexGrid& grid,
                                PlayerId targetPlayer,
                                aoc::hex::AxialCoord location) {
    const aoc::game::Player* player = gameState.player(targetPlayer);
    if (player == nullptr) { return 0; }

    int32_t bestLevel = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unit : player->units()) {
        const SpyComponent& spy = unit->spy();
        if (spy.currentMission == SpyMission::CounterIntelligence
            && grid.distance(spy.location, location) <= 1) {
            int32_t lvl = spy.effectiveLevel(SpyMission::CounterIntelligence);
            if (lvl > bestLevel) { bestLevel = lvl; }
        }
    }
    return bestLevel;
}

// ============================================================================
// Mission execution: success effects
// ============================================================================

static void executeMissionSuccess(aoc::game::GameState& gameState,
                                   aoc::game::Player& ownerPlayer,
                                   SpyComponent& spy,
                                   aoc::Random& rng) {
    switch (spy.currentMission) {
        case SpyMission::GatherIntelligence: {
            LOG_INFO("Spy (%.*s, P%u) gathered intelligence at (%d,%d)",
                     static_cast<int>(spyLevelName(spy.level).size()),
                     spyLevelName(spy.level).data(),
                     static_cast<unsigned>(spy.owner),
                     spy.location.q, spy.location.r);
            break;
        }

        case SpyMission::MonitorTreasury: {
            // Passive: intelligence level set in the calling code.
            LOG_INFO("Spy (P%u) monitoring treasury at (%d,%d)",
                     static_cast<unsigned>(spy.owner),
                     spy.location.q, spy.location.r);
            break;
        }

        case SpyMission::MonitorResearch: {
            LOG_INFO("Spy (P%u) monitoring research at (%d,%d)",
                     static_cast<unsigned>(spy.owner),
                     spy.location.q, spy.location.r);
            break;
        }

        case SpyMission::StealTechnology: {
            PlayerTechComponent& tech = ownerPlayer.tech();
            if (tech.currentResearch.isValid()) {
                const TechDef& def = techDef(tech.currentResearch);
                // Master spies steal more: 20% base + 10% per level
                const float pct = 0.20f + static_cast<float>(static_cast<uint8_t>(spy.level)) * 0.10f;
                const float bonus = pct * static_cast<float>(def.researchCost);
                tech.researchProgress += bonus;
                LOG_INFO("Spy (P%u, %.*s) stole tech: +%.0f toward %.*s",
                         static_cast<unsigned>(spy.owner),
                         static_cast<int>(spyLevelName(spy.level).size()),
                         spyLevelName(spy.level).data(),
                         static_cast<double>(bonus),
                         static_cast<int>(def.name.size()), def.name.data());
            }
            break;
        }

        case SpyMission::SabotageProduction: {
            aoc::game::City* city = findEnemyCityAt(gameState, spy.owner, spy.location);
            if (city != nullptr) {
                const float reduction = city->productionProgress() * 0.5f;
                city->setProductionProgress(city->productionProgress() - reduction);
                LOG_INFO("Spy (P%u) sabotaged production in %s (-%.0f)",
                         static_cast<unsigned>(spy.owner),
                         city->name().c_str(), static_cast<double>(reduction));
            }
            break;
        }

        case SpyMission::SiphonFunds: {
            aoc::game::Player* target = findCityOwner(gameState, spy.owner, spy.location);
            if (target != nullptr) {
                // Steal 5-15% of target's income, scaled by spy level
                const float pct = 0.05f + static_cast<float>(static_cast<uint8_t>(spy.level)) * 0.025f;
                CurrencyAmount stolen = static_cast<CurrencyAmount>(
                    static_cast<float>(target->incomePerTurn()) * pct);
                stolen = std::max(stolen, static_cast<CurrencyAmount>(1));
                target->addGold(-stolen);
                ownerPlayer.addGold(stolen);
                LOG_INFO("Spy (P%u) siphoned %lld gold from P%u",
                         static_cast<unsigned>(spy.owner),
                         static_cast<long long>(stolen),
                         static_cast<unsigned>(target->id()));
            }
            break;
        }

        case SpyMission::MarketManipulation: {
            // Reduce target's GDP by 10-20% for 5 turns (modeled as direct treasury hit)
            aoc::game::Player* target = findCityOwner(gameState, spy.owner, spy.location);
            if (target != nullptr) {
                const CurrencyAmount damage = static_cast<CurrencyAmount>(
                    static_cast<float>(target->monetary().gdp) * 0.15f);
                target->addGold(-damage);
                LOG_INFO("Spy (P%u) manipulated market: P%u lost %lld gold",
                         static_cast<unsigned>(spy.owner),
                         static_cast<unsigned>(target->id()),
                         static_cast<long long>(damage));
            }
            break;
        }

        case SpyMission::CurrencyCounterfeit: {
            aoc::game::Player* target = findCityOwner(gameState, spy.owner, spy.location);
            if (target != nullptr) {
                // Reduce currency trust by 5-15 based on spy level
                const int32_t trustDamage = 5 + static_cast<int32_t>(spy.level) * 3;
                target->monetary().inflationRate += static_cast<float>(trustDamage) * 0.01f;
                LOG_INFO("Spy (P%u) counterfeited currency: P%u trust -%d",
                         static_cast<unsigned>(spy.owner),
                         static_cast<unsigned>(target->id()), trustDamage);
            }
            break;
        }

        case SpyMission::SupplyChainDisrupt: {
            // Reduce all production in target city by 30% for 10 turns
            aoc::game::City* city = findEnemyCityAt(gameState, spy.owner, spy.location);
            if (city != nullptr) {
                // Apply via happiness penalty (production modifier)
                city->happiness().modifiers -= 3.0f;
                LOG_INFO("Spy (P%u) disrupted supply chain in %s",
                         static_cast<unsigned>(spy.owner), city->name().c_str());
            }
            break;
        }

        case SpyMission::InsiderTrading: {
            // Award gold bonus based on target's stock market activity
            aoc::game::Player* target = findCityOwner(gameState, spy.owner, spy.location);
            if (target != nullptr) {
                const CurrencyAmount bonus = static_cast<CurrencyAmount>(
                    static_cast<float>(target->monetary().gdp) * 0.05f);
                ownerPlayer.addGold(bonus);
                LOG_INFO("Spy (P%u) insider trading: +%lld gold from P%u intel",
                         static_cast<unsigned>(spy.owner),
                         static_cast<long long>(bonus),
                         static_cast<unsigned>(target->id()));
            }
            break;
        }

        case SpyMission::StealTradeSecrets: {
            // If target has higher industrial revolution, gain 25% progress
            aoc::game::Player* target = findCityOwner(gameState, spy.owner, spy.location);
            if (target != nullptr
                && static_cast<uint8_t>(target->industrial().currentRevolution)
                   > static_cast<uint8_t>(ownerPlayer.industrial().currentRevolution)) {
                LOG_INFO("Spy (P%u) stole trade secrets from P%u (industrial advantage)",
                         static_cast<unsigned>(spy.owner),
                         static_cast<unsigned>(target->id()));
            }
            break;
        }

        case SpyMission::RecruitPartisans: {
            // Spawn 2-3 hostile units near the target city
            aoc::game::City* city = findEnemyCityAt(gameState, spy.owner, spy.location);
            if (city != nullptr) {
                const int32_t count = 2 + rng.nextInt(0, 1);
                // Spawn warriors owned by the spy's player near the city
                for (int32_t i = 0; i < count; ++i) {
                    ownerPlayer.addUnit(UnitTypeId{0}, spy.location);  // Warriors
                }
                LOG_INFO("Spy (P%u) recruited %d partisans near %s",
                         static_cast<unsigned>(spy.owner), count,
                         city->name().c_str());
            }
            break;
        }

        case SpyMission::FomentUnrest: {
            aoc::game::City* city = findEnemyCityAt(gameState, spy.owner, spy.location);
            if (city != nullptr) {
                // Reduce loyalty by 20-30 points
                const float loyaltyDamage = 20.0f + static_cast<float>(rng.nextInt(0, 10));
                city->loyalty().loyalty -= loyaltyDamage;
                if (city->loyalty().loyalty < 0.0f) {
                    city->loyalty().loyalty = 0.0f;
                }
                LOG_INFO("Spy (P%u) fomented unrest in %s: loyalty -%.0f",
                         static_cast<unsigned>(spy.owner),
                         city->name().c_str(), static_cast<double>(loyaltyDamage));
            }
            break;
        }

        case SpyMission::NeutralizeGovernor:
        case SpyMission::RecruitDoubleAgent:
        case SpyMission::EstablishEmbassy:
        case SpyMission::CounterIntelligence:
        case SpyMission::Count:
            break;  // Passive, handled elsewhere, or sentinel.
    }
}

// ============================================================================
// Main processing
// ============================================================================

void processSpyMissions(aoc::game::GameState& gameState,
                        const aoc::map::HexGrid& grid,
                        aoc::Random& rng,
                        DiplomacyManager* diplomacy) {
    struct SpyEntry {
        aoc::game::Player* player;
        aoc::game::Unit*   unit;
    };
    std::vector<SpyEntry> spyUnits;

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerPtr->units()) {
            const SpyComponent& spy = unitPtr->spy();
            if (spy.owner == INVALID_PLAYER) { continue; }
            if (spy.turnsRemaining > 0
                || spy.currentMission == SpyMission::CounterIntelligence
                || spyMissionDef(spy.currentMission).isPassive) {
                spyUnits.push_back({playerPtr.get(), unitPtr.get()});
            }
        }
    }

    std::vector<aoc::game::Unit*> toRemove;

    for (const SpyEntry& entry : spyUnits) {
        aoc::game::Unit* unitPtr = entry.unit;
        SpyComponent& spy = unitPtr->spy();

        // Passive missions continue indefinitely
        if (spyMissionDef(spy.currentMission).isPassive) {
            if (spy.currentMission != SpyMission::CounterIntelligence) {
                // Passive intel missions: gain small XP over time
                if (rng.chance(0.1f)) {
                    spy.addExperience(1);
                }
            }
            continue;
        }

        // Active missions: decrement timer
        if (spy.turnsRemaining > 0) {
            --spy.turnsRemaining;
        }
        if (spy.turnsRemaining > 0) {
            continue;  // Still in progress
        }

        // Mission complete — find counter-spy level at target
        aoc::game::Player* targetPlayer = findCityOwner(gameState, spy.owner, spy.location);
        const int32_t counterLvl = (targetPlayer != nullptr)
            ? counterSpyLevel(gameState, grid, targetPlayer->id(), spy.location) : 0;

        // Roll for success
        const float successChance = missionSuccessRate(spy, spy.currentMission, counterLvl);
        const bool success = rng.chance(successChance);

        if (success) {
            aoc::game::Player* ownerPlayer = gameState.player(spy.owner);
            if (ownerPlayer != nullptr) {
                executeMissionSuccess(gameState, *ownerPlayer, spy, rng);
            }

            // XP gain: 3 base + 2 for harder missions
            const int32_t xpGain = 3 + (spyMissionDef(spy.currentMission).baseProbabilityIndex < 3 ? 2 : 0);
            const bool leveledUp = spy.addExperience(xpGain);
            if (leveledUp) {
                LOG_INFO("Spy (P%u) leveled up to %.*s!",
                         static_cast<unsigned>(spy.owner),
                         static_cast<int>(spyLevelName(spy.level).size()),
                         spyLevelName(spy.level).data());
            }
        } else {
            // Graduated failure outcome
            const SpyFailureOutcome outcome = rollFailureOutcome(spy, rng.nextFloat());

            LOG_WARN("Spy (P%u, %.*s) failed %.*s at (%d,%d) — %.*s",
                     static_cast<unsigned>(spy.owner),
                     static_cast<int>(spyLevelName(spy.level).size()),
                     spyLevelName(spy.level).data(),
                     static_cast<int>(spyMissionDef(spy.currentMission).name.size()),
                     spyMissionDef(spy.currentMission).name.data(),
                     spy.location.q, spy.location.r,
                     static_cast<int>(spyFailureOutcomeName(outcome).size()),
                     spyFailureOutcomeName(outcome).data());

            // Political fallout: a caught spy always generates a grievance on
            // the victim and a relation penalty. Escaped undetected leaves no
            // trace (no grievance, no relation hit).
            auto applyFallout = [&](int32_t relationDelta, int32_t decayTurns) {
                if (targetPlayer == nullptr) { return; }
                targetPlayer->grievances().addGrievance(
                    GrievanceType::EspionageCaught, spy.owner);
                if (diplomacy != nullptr) {
                    RelationModifier mod{};
                    mod.reason         = "Spy caught in our territory";
                    mod.amount         = relationDelta;
                    mod.turnsRemaining = decayTurns;
                    diplomacy->addModifier(targetPlayer->id(), spy.owner, mod);
                }
            };

            switch (outcome) {
                case SpyFailureOutcome::EscapedUndetected:
                    // Spy returns safely, no consequences
                    break;

                case SpyFailureOutcome::Identified:
                    spy.isRevealed = true;
                    applyFallout(-5, 20);
                    break;

                case SpyFailureOutcome::Captured:
                    applyFallout(-15, 40);
                    toRemove.push_back(unitPtr);
                    continue;

                case SpyFailureOutcome::Killed:
                    applyFallout(-20, 40);
                    toRemove.push_back(unitPtr);
                    continue;
            }
        }

        // Reset timer for next mission cycle (active missions return spy after completion)
        if (!spyMissionDef(spy.currentMission).isPassive) {
            spy.currentMission = SpyMission::GatherIntelligence;
            spy.turnsRemaining = 0;
        }
    }

    // Remove captured/killed spies
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
    spy.currentMission = mission;
    spy.turnsRemaining = adjustedMissionDuration(spy, mission);
    spy.isRevealed = false;

    const SpyMissionDef& def = spyMissionDef(mission);
    LOG_INFO("Spy (P%u, %.*s) assigned to %.*s at (%d,%d), %d turns",
             static_cast<unsigned>(spy.owner),
             static_cast<int>(spyLevelName(spy.level).size()),
             spyLevelName(spy.level).data(),
             static_cast<int>(def.name.size()), def.name.data(),
             spy.location.q, spy.location.r,
             spy.turnsRemaining);

    return ErrorCode::Ok;
}

} // namespace aoc::sim
