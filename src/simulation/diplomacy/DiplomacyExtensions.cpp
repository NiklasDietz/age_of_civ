/**
 * @file DiplomacyExtensions.cpp
 * @brief Era dedications, emergency system, extended World Congress.
 */

#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/simulation/diplomacy/DiplomacyExtensions.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/AllianceTypes.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void grantResearchAllianceBoons(aoc::game::GameState& gameState, DiplomacyManager& diplomacy,
                                PlayerId player, int32_t currentTurn) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return;
    }
    const uint8_t seats = diplomacy.playerCount();
    if (player >= seats) {
        return;
    }
    for (uint8_t other = 0; other < seats; ++other) {
        if (other == player) {
            continue;
        }
        PairwiseRelation& rel = diplomacy.relation(player, static_cast<PlayerId>(other));
        for (std::size_t i = 1; i < rel.alliances.size(); ++i) {
            AllianceState& a = rel.alliances[i];
            if (!a.isActive() || a.type != AllianceType::Research) {
                continue;
            }
            const uint8_t level = static_cast<uint8_t>(a.level);

            // Level 2: a eureka, on its interval.
            if (level >= static_cast<uint8_t>(AllianceLevel::Level2) &&
                currentTurn - a.lastEurekaGrantTurn >= RESEARCH_EUREKA_INTERVAL) {
                a.lastEurekaGrantTurn = currentTurn;
                const TechId researching = gsPlayer->tech().currentResearch;
                if (researching.isValid()) {
                    // A shared breakthrough: worth a slice of the current tech.
                    gsPlayer->tech().researchProgress +=
                        effectiveResearchCost(gsPlayer->tech(), researching) * 0.25f;
                    LOG_INFO("Research alliance: player %u received a eureka from its ally %u",
                             static_cast<unsigned>(player), static_cast<unsigned>(other));
                }
            }

            // Level 3: a free tech, on its own longer interval, and only when
            // the ally is genuinely ahead -- the table says "if ally has more
            // techs", which is the point of the clause.
            if (level >= static_cast<uint8_t>(AllianceLevel::Level3) &&
                currentTurn - a.lastTechGrantTurn >= RESEARCH_TECH_INTERVAL) {
                const aoc::game::Player* ally = gameState.player(static_cast<PlayerId>(other));
                if (ally != nullptr &&
                    ally->tech().completedTechs.size() > 0) {
                    int32_t mine = 0;
                    int32_t theirs = 0;
                    for (bool b : gsPlayer->tech().completedTechs) { mine += b ? 1 : 0; }
                    for (bool b : ally->tech().completedTechs)     { theirs += b ? 1 : 0; }
                    if (theirs > mine) {
                        const TechId researching = gsPlayer->tech().currentResearch;
                        if (researching.isValid()) {
                            a.lastTechGrantTurn = currentTurn;
                            gsPlayer->tech().completeResearch();
                            LOG_INFO("Research alliance: player %u was given a tech by its ally %u",
                                     static_cast<unsigned>(player), static_cast<unsigned>(other));
                        }
                    }
                }
            }
        }
    }
}

AllianceYieldModifiers computeAllianceYieldModifiers(
        const DiplomacyManager& diplomacy, PlayerId player, uint8_t playerCount) {
    AllianceYieldModifiers out{};
    if (player >= playerCount) { return out; }
    for (uint8_t other = 0; other < playerCount; ++other) {
        if (other == player) { continue; }
        const PairwiseRelation& rel =
            diplomacy.relation(player, static_cast<PlayerId>(other));
        // Walk the per-type alliance slots; entry 0 is reserved (None).
        for (std::size_t i = 1; i < rel.alliances.size(); ++i) {
            const AllianceState& a = rel.alliances[i];
            if (!a.isActive()) { continue; }
            // The payloads ALLIANCE_TYPE_DEFS actually describes, rather than
            // the flat 0.05-per-level bump that used to stand in for all five.
            // level2Bonus and level3Bonus had no readers at all, so choosing an
            // alliance type changed nothing but which yield the bump landed on.
            const std::size_t defIdx = static_cast<std::size_t>(a.type) - 1;
            if (defIdx >= ALLIANCE_TYPE_DEFS.size()) { continue; }
            const AllianceTypeDef& def = ALLIANCE_TYPE_DEFS[defIdx];
            const uint8_t level        = static_cast<uint8_t>(a.level);
            const bool atLeast2        = level >= static_cast<uint8_t>(AllianceLevel::Level2);
            const bool atLeast3        = level >= static_cast<uint8_t>(AllianceLevel::Level3);

            // Level 1 keeps a small generic bump: the table names no level-1
            // payload, and its own description calls L1 "Open Borders +
            // Defensive Pact", which is a relation state rather than a yield.
            constexpr float LEVEL1_BUMP = 0.05f;
            switch (a.type) {
                case AllianceType::Research:
                    // Level 1 only. The level-2 eureka and level-3 free tech are
                    // PERIODIC grants, paid by grantResearchAllianceBoons on
                    // their own intervals rather than as a standing multiplier.
                    out.scienceMult += LEVEL1_BUMP;
                    break;
                case AllianceType::Cultural:
                    out.cultureMult += LEVEL1_BUMP;
                    if (atLeast2) { out.tourismMult += def.level2Bonus.bonusValue; }
                    if (atLeast3) {
                        out.sharedGreatWorkSlots +=
                            static_cast<int32_t>(def.level3Bonus.bonusValue);
                    }
                    break;
                case AllianceType::Economic:
                    out.goldMult += LEVEL1_BUMP;
                    if (atLeast3) { out.goldMult += def.level3Bonus.bonusValue; }
                    break;
                case AllianceType::Religious:
                    if (atLeast2) { out.faithMult += def.level2Bonus.bonusValue; }
                    else          { out.faithMult += LEVEL1_BUMP; }
                    break;
                case AllianceType::Military:
                    if (atLeast2) { out.sharedVisibility = true; }
                    if (atLeast3) { out.combatBonus += def.level3Bonus.bonusValue; }
                    break;
                default: break;
            }
        }
    }
    return out;
}

void triggerEmergency(aoc::game::GameState& /*gameState*/, GlobalEmergencyTracker& tracker,
                      EmergencyType type, PlayerId target) {
    if (tracker.emergencyCount >= 4) {
        return;  // Max concurrent emergencies
    }

    const EmergencyDef& def = EMERGENCY_DEFS[static_cast<uint8_t>(type)];
    ActiveEmergency& em = tracker.emergencies[tracker.emergencyCount];
    em.type = type;
    em.target = target;
    em.turnsRemaining = def.duration;
    em.participantCount = 0;
    em.resolved = false;
    ++tracker.emergencyCount;

    LOG_INFO("EMERGENCY: %.*s triggered against player %u!",
             static_cast<int>(def.name.size()), def.name.data(),
             static_cast<unsigned>(target));
}

void processEmergencies(aoc::game::GameState& /*gameState*/, GlobalEmergencyTracker& tracker) {
    for (int32_t i = 0; i < tracker.emergencyCount; ++i) {
        ActiveEmergency& em = tracker.emergencies[i];
        if (em.resolved) { continue; }

        --em.turnsRemaining;
        if (em.turnsRemaining <= 0) {
            em.resolved = true;
            LOG_INFO("Emergency against player %u has ended",
                     static_cast<unsigned>(em.target));
        }
    }

    // Remove resolved emergencies
    int32_t write = 0;
    for (int32_t i = 0; i < tracker.emergencyCount; ++i) {
        if (!tracker.emergencies[i].resolved) {
            if (write != i) {
                tracker.emergencies[write] = tracker.emergencies[i];
            }
            ++write;
        }
    }
    tracker.emergencyCount = write;
}

} // namespace aoc::sim
