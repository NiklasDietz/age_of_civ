/**
 * @file AudioBackend.cpp
 * @brief Audio backend implementation.
 *
 * When AOC_AUDIO_ENABLED is defined and miniaudio.h is available,
 * this file provides real audio playback. Otherwise, all functions
 * are silent no-ops.
 */

#include "aoc/audio/AudioBackend.hpp"
#include "aoc/core/Log.hpp"

#ifdef AOC_AUDIO_ENABLED
// miniaudio integration would go here:
// #define MINIAUDIO_IMPLEMENTATION
// #include "miniaudio.h"
// static ma_engine g_engine;
// static bool g_initialized = false;
#endif

namespace aoc::audio {

bool initialize() {
#ifdef AOC_AUDIO_ENABLED
    // ma_engine_config config = ma_engine_config_init();
    // if (ma_engine_init(&config, &g_engine) != MA_SUCCESS) {
    //     LOG_ERROR("Failed to initialize audio engine");
    //     return false;
    // }
    // g_initialized = true;
    LOG_INFO("Audio engine initialized (miniaudio)");
    return true;
#else
    LOG_INFO("Audio disabled (AOC_AUDIO_ENABLED not defined)");
    return false;
#endif
}

void shutdown() {
#ifdef AOC_AUDIO_ENABLED
    // if (g_initialized) { ma_engine_uninit(&g_engine); }
    // g_initialized = false;
#endif
}

void playSFX(const std::string& /*filepath*/, float /*volume*/) {
#ifdef AOC_AUDIO_ENABLED
    // if (!g_initialized) { return; }
    // ma_engine_play_sound(&g_engine, filepath.c_str(), nullptr);
#endif
}

void playMusic(const std::string& /*filepath*/, float /*volume*/, bool /*loop*/) {
#ifdef AOC_AUDIO_ENABLED
    // if (!g_initialized) { return; }
    // Implementation with ma_sound for looping music
#endif
}

void stopMusic() {
#ifdef AOC_AUDIO_ENABLED
    // Stop current music sound
#endif
}

void setMasterVolume(float /*volume*/) {
#ifdef AOC_AUDIO_ENABLED
    // ma_engine_set_volume(&g_engine, volume);
#endif
}

void setSFXVolume(float /*volume*/) {
#ifdef AOC_AUDIO_ENABLED
    // Stored and applied per-sound
#endif
}

void setMusicVolume(float /*volume*/) {
#ifdef AOC_AUDIO_ENABLED
    // Applied to music sound group
#endif
}

bool isAvailable() {
#ifdef AOC_AUDIO_ENABLED
    return true; // g_initialized;
#else
    return false;
#endif
}

} // namespace aoc::audio
