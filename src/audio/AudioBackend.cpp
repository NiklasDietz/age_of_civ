/**
 * @file AudioBackend.cpp
 * @brief Silent no-op audio backend; see AudioBackend.hpp for wiring miniaudio in.
 */

#include "aoc/audio/AudioBackend.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::audio {

bool initialize() {
#ifdef AOC_AUDIO_ENABLED
    LOG_ERROR("Audio: AOC_AUDIO_ENABLED is defined but no backend is integrated");
#else
    LOG_INFO("Audio disabled (AOC_AUDIO_ENABLED not defined)");
#endif
    return false;
}

void shutdown() {}

void playSFX(const std::string& /*filepath*/, float /*volume*/) {}

void playMusic(const std::string& /*filepath*/, float /*volume*/, bool /*loop*/) {}

void stopMusic() {}

void setMasterVolume(float /*volume*/) {}

void setSFXVolume(float /*volume*/) {}

void setMusicVolume(float /*volume*/) {}

bool isAvailable() {
    return false;
}

} // namespace aoc::audio
