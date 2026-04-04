#pragma once

/**
 * @file AudioManager.hpp
 * @brief No-op audio stub. All methods are empty -- audio integration
 *        (miniaudio or similar) will be added later without changing the API.
 */

#include <cstdint>

namespace aoc::audio {

using SoundId = uint16_t;
using MusicId = uint16_t;

class AudioManager {
public:
    AudioManager() = default;
    ~AudioManager() = default;

    void initialize() {}
    void shutdown() {}

    void playSound(SoundId /*id*/) {}
    void stopSound(SoundId /*id*/) {}
    void playMusic(MusicId /*id*/) {}
    void stopMusic() {}

    void setMasterVolume(float /*volume*/) {}
    void setSfxVolume(float /*volume*/) {}
    void setMusicVolume(float /*volume*/) {}

    [[nodiscard]] float masterVolume() const { return this->m_masterVolume; }
    [[nodiscard]] float sfxVolume() const { return this->m_sfxVolume; }
    [[nodiscard]] float musicVolume() const { return this->m_musicVolume; }

private:
    float m_masterVolume = 1.0f;
    float m_sfxVolume    = 1.0f;
    float m_musicVolume  = 0.7f;
};

} // namespace aoc::audio
