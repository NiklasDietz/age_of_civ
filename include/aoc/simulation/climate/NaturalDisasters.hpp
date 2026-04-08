#pragma once

/**
 * @file NaturalDisasters.hpp
 * @brief Natural disaster events tied to the climate and geology systems.
 *
 * Disasters are deterministic (hash-based), not random. Higher climate
 * temperature increases frequency of storms and droughts.
 *
 * Types:
 *   - Volcanic Eruption: Near mountain tiles on convergent plate boundaries.
 *     Destroys improvements in 2-tile radius, creates fertile soil (+2 food after 5 turns).
 *   - Earthquake: Near mountains/faults. Damages city buildings (random building destroyed).
 *     Stronger near convergent boundaries.
 *   - Tsunami: Coastal tiles after underwater earthquake. Floods coast tiles for 3 turns,
 *     destroys coastal improvements.
 *   - Drought: Plains/grassland during high global temperature. -2 food for affected tiles
 *     for 5 turns. Frequency increases with climate change.
 *   - Flood: Existing flooding system in RiverGameplay.hpp, enhanced here with
 *     severity scaling based on deforestation upstream.
 *   - Wildfire: Forest tiles during drought. Removes forest feature, +1 production
 *     for 3 turns (ash nutrients). Frequency increases with temperature.
 *   - Hurricane: Coast/ocean tiles. Damages naval units and coastal improvements.
 *     More frequent at higher temperatures.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <string_view>

namespace aoc::ecs { class World; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

enum class DisasterType : uint8_t {
    None,
    VolcanicEruption,
    Earthquake,
    Tsunami,
    Drought,
    Wildfire,
    Hurricane,

    Count
};

struct DisasterEvent {
    DisasterType     type = DisasterType::None;
    hex::AxialCoord  epicenter;
    int32_t          radius = 1;
    int32_t          severity = 1;   ///< 1-3, affects damage
    int32_t          duration = 1;   ///< Turns the effect lasts
    PlayerId         affectedPlayer = INVALID_PLAYER;
};

/// Per-tile active disaster effect tracking.
struct TileDisasterComponent {
    DisasterType activeDisaster = DisasterType::None;
    int32_t      turnsRemaining = 0;
    int32_t      yieldModifierFood = 0;
    int32_t      yieldModifierProd = 0;
};

/**
 * @brief Process natural disasters for this turn.
 *
 * Checks disaster triggers based on geology, climate temperature, and
 * deterministic hashing. Creates disaster events, applies effects to
 * tiles, units, and cities.
 *
 * @param world       ECS world.
 * @param grid        Hex grid.
 * @param turnNumber  Current turn (for deterministic hashing).
 * @param globalTemp  Current global temperature (from climate system).
 * @return Number of disasters that occurred this turn.
 */
int32_t processNaturalDisasters(aoc::ecs::World& world, aoc::map::HexGrid& grid,
                                int32_t turnNumber, float globalTemp);

/**
 * @brief Tick down active disaster effects on tiles.
 *
 * Removes expired disaster effects and restores tile yields.
 */
void tickDisasterEffects(aoc::ecs::World& world, aoc::map::HexGrid& grid);

} // namespace aoc::sim
