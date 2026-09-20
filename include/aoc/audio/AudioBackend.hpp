#pragma once

/**
 * @file AudioBackend.hpp
 * @brief Audio playback interface; no backend is wired up.
 *
 * Every function is a silent no-op and initialize() returns false: no backend
 * is integrated. Wiring one up means vendoring miniaudio, writing the bodies in
 * AudioBackend.cpp, and defining AOC_AUDIO_ENABLED; the macro alone does nothing.
 *
 * Audio assets should be placed in assets/audio/:
 *   assets/audio/music/     -- Background music tracks (WAV/MP3/OGG)
 *   assets/audio/sfx/       -- Sound effects
 *   assets/audio/ambient/   -- Ambient background sounds
 */

#include <cstdint>
#include <string>

namespace aoc::audio {

/**
 * @brief Initialize the audio backend.
 *
 * Creates the audio engine, sets up device output.
 * Returns false if initialization fails (audio will be disabled).
 */
bool initialize();

/**
 * @brief Shut down the audio backend. Frees all resources.
 */
void shutdown();

/**
 * @brief Play a sound effect.
 *
 * @param filepath  Path to the audio file (relative to assets/audio/sfx/).
 * @param volume    Volume multiplier (0.0 to 1.0).
 */
void playSFX(const std::string& filepath, float volume = 1.0f);

/**
 * @brief Start playing background music.
 *
 * If music is already playing, crossfades to the new track.
 *
 * @param filepath  Path to the music file (relative to assets/audio/music/).
 * @param volume    Volume multiplier (0.0 to 1.0).
 * @param loop      Whether to loop the track.
 */
void playMusic(const std::string& filepath, float volume = 0.5f, bool loop = true);

/**
 * @brief Stop music playback.
 */
void stopMusic();

/**
 * @brief Set master volume (affects all audio).
 */
void setMasterVolume(float volume);

/**
 * @brief Set SFX volume multiplier.
 */
void setSFXVolume(float volume);

/**
 * @brief Set music volume multiplier.
 */
void setMusicVolume(float volume);

/**
 * @brief Check if audio is available (backend initialized successfully).
 */
[[nodiscard]] bool isAvailable();

} // namespace aoc::audio
