#pragma once

/**
 * @file SoundEvent.hpp
 * @brief Sound event queue for deferred audio playback.
 *
 * Game systems push sound effects to this queue. When a real audio backend
 * (e.g. miniaudio) is integrated, it will consume and play queued events.
 */

#include <cstdint>
#include <vector>

namespace aoc::audio {

enum class SoundEffect : uint8_t {
    UIClick,
    UIOpen,
    UIClose,
    UnitMove,
    UnitAttack,
    UnitDeath,
    CityFounded,
    CityGrow,
    CityCapture,
    TechResearched,
    CivicCompleted,
    TurnEnd,
    TurnStart,
    WarDeclared,
    PeaceMade,
    WonderBuilt,
    GreatPersonRecruited,
    Victory,
    Count
};

struct SoundEventQueue {
    std::vector<SoundEffect> pending;

    void push(SoundEffect effect) { this->pending.push_back(effect); }
    void clear() { this->pending.clear(); }
    [[nodiscard]] bool empty() const { return this->pending.empty(); }
};

} // namespace aoc::audio
