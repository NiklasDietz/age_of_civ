# Component: audio

## Responsibility

Audio playback backend. Currently a stub that logs its disabled/enabled state. When the
build defines `AOC_AUDIO_ENABLED` and `miniaudio.h` is present, the implementation would
initialize an `ma_engine` and provide `playSound()` / `playMusic()` functions; the
infrastructure is in place but the integration is not yet complete.

## Key files

- `src/audio/AudioBackend.cpp` /
  [include/aoc/audio/AudioBackend.hpp](../../../include/aoc/audio/AudioBackend.hpp) —
  `aoc::audio::initialize()`, `shutdown()`, `playSound(name)`, `playMusic(name)`.
  All functions are no-ops when `AOC_AUDIO_ENABLED` is not defined.

## Public surface

- `aoc::audio::initialize()` — called by `Application::run()` during startup.
- `aoc::audio::playSound(name)` / `playMusic(name)` — called from UI and simulation
  event handlers.

## Internal structure

Single file pair. The miniaudio integration is stubbed out with `#ifdef AOC_AUDIO_ENABLED`
guards. No CMake target currently enables this flag; it is prepared for a future work
package.
