#pragma once

/**
 * @file MusicManager.hpp
 * @brief Music track management stub.
 *
 * Tracks which music track should be playing based on the current game era.
 * Actual audio playback will be added when a backend library is integrated.
 */

#include <cstdint>

namespace aoc::audio {

enum class MusicTrack : uint8_t {
    MainMenu,
    Ancient,
    Classical,
    Medieval,
    Renaissance,
    Industrial,
    Modern,
    Atomic,
    Victory,
    Defeat,
    Count
};

struct MusicManager {
    MusicTrack currentTrack = MusicTrack::MainMenu;
    float volume = 0.7f;

    void setTrack(MusicTrack track) { this->currentTrack = track; }
    void setVolume(float vol) { this->volume = vol; }
    [[nodiscard]] MusicTrack track() const { return this->currentTrack; }
};

} // namespace aoc::audio
