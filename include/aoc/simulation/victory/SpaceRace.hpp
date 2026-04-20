#pragma once

/**
 * @file SpaceRace.hpp
 * @brief Science Victory path: Space Race project chain.
 *
 * To win a Science Victory (Classic mode), a player must complete all 4
 * space race projects in order. Each requires a Spaceport district and
 * specific technologies.
 *
 * Project chain:
 *   1. Launch Earth Satellite  — requires Rocketry tech, 1500 production
 *   2. Launch Moon Landing     — requires Satellites tech, 2000 production
 *   3. Mars Colony Ship        — requires Nuclear Fusion tech, 3000 production
 *   4. Exoplanet Expedition    — requires Nanotechnology tech, 4000 production
 *
 * Each project can only be worked on by one city (the one with a Spaceport).
 * Multiple Spaceport cities can work on different projects simultaneously.
 */

#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace aoc::game {
class GameState;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

enum class SpaceProjectId : uint8_t {
    EarthSatellite  = 0,
    MoonLanding     = 1,
    MarsColony      = 2,
    ExoplanetExpedition = 3,

    Count
};

inline constexpr int32_t SPACE_PROJECT_COUNT = static_cast<int32_t>(SpaceProjectId::Count);

struct SpaceProjectDef {
    SpaceProjectId   id;
    std::string_view name;
    TechId           requiredTech;
    float            productionCost;
};

inline constexpr std::array<SpaceProjectDef, SPACE_PROJECT_COUNT> SPACE_PROJECT_DEFS = {{
    {SpaceProjectId::EarthSatellite,     "Launch Earth Satellite",  TechId{18}, 1200.0f},
    {SpaceProjectId::MoonLanding,        "Launch Moon Landing",     TechId{20}, 1600.0f},
    {SpaceProjectId::MarsColony,         "Mars Colony Ship",        TechId{22}, 2400.0f},
    {SpaceProjectId::ExoplanetExpedition,"Exoplanet Expedition",    TechId{25}, 3200.0f},
}};

/// Per-player space race progress.
struct PlayerSpaceRaceComponent {
    PlayerId owner = INVALID_PLAYER;
    bool completed[SPACE_PROJECT_COUNT] = {};
    float progress[SPACE_PROJECT_COUNT] = {};

    [[nodiscard]] bool allCompleted() const {
        for (int32_t i = 0; i < SPACE_PROJECT_COUNT; ++i) {
            if (!this->completed[i]) { return false; }
        }
        return true;
    }

    [[nodiscard]] int32_t completedCount() const {
        int32_t count = 0;
        for (int32_t i = 0; i < SPACE_PROJECT_COUNT; ++i) {
            if (this->completed[i]) { ++count; }
        }
        return count;
    }

    /// Get the next uncompleted project (for AI to work on).
    [[nodiscard]] SpaceProjectId nextProject() const {
        for (int32_t i = 0; i < SPACE_PROJECT_COUNT; ++i) {
            if (!this->completed[i]) {
                return static_cast<SpaceProjectId>(i);
            }
        }
        return SpaceProjectId::Count;
    }
};

/// Per-turn global processor: advances space-race progress for every
/// eligible player (non-eliminated, owns a Campus, has researched the
/// next project's required tech) using a fraction of their current
/// science output.
void processSpaceRace(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid);

} // namespace aoc::sim
