/**
 * @file DiplomacyExtensions.cpp
 * @brief Era dedications, emergency system, extended World Congress.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/diplomacy/DiplomacyExtensions.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/AllianceTypes.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

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
            const float lvl = 1.0f + static_cast<float>(static_cast<uint8_t>(a.level));
            const float bump = 0.05f * lvl;  // L1: +10%, L2: +15%, L3: +20%
            switch (a.type) {
                case AllianceType::Research:  out.scienceMult += bump; break;
                case AllianceType::Cultural:  out.cultureMult += bump; break;
                case AllianceType::Economic:  out.goldMult    += bump; break;
                case AllianceType::Religious: out.faithMult   += bump; break;
                case AllianceType::Military:  out.combatBonus += 1.0f * lvl; break;
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
