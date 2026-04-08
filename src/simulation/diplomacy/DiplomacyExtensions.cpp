/**
 * @file DiplomacyExtensions.cpp
 * @brief Era dedications, emergency system, extended World Congress.
 */

#include "aoc/simulation/diplomacy/DiplomacyExtensions.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void triggerEmergency(aoc::ecs::World& /*world*/, GlobalEmergencyTracker& tracker,
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

void processEmergencies(aoc::ecs::World& /*world*/, GlobalEmergencyTracker& tracker) {
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
