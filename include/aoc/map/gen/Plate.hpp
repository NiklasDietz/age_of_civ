#pragma once

/**
 * @file Plate.hpp
 * @brief Tectonic-plate description shared by extracted MapGenerator passes.
 */

#include <cstdint>

namespace aoc::map::gen {

/// Live plate state. Authoritative position is (latDeg, lonDeg) on the
/// sphere; (cx, cy) is the Mollweide-forward projection cached each
/// motion step for legacy 2D consumers (hotspot proximity, rift
/// seeding, EarthSystem boundary normals). p.rot is updated each
/// epoch by the Euler-pole local-vertical projection.
struct Plate {
    float latDeg = 0.0f;       // [-90, 90]
    float lonDeg = 0.0f;       // [-180, 180]
    float eulerPoleLatDeg = 0.0f;
    float eulerPoleLonDeg = 0.0f;
    float angularVelDeg   = 0.0f;
    // LEGACY: Mollweide-projection cache. Authoritative position is
    // (latDeg, lonDeg). cx/cy are recomputed each motion step from
    // mollweideForward(latDeg, lonDeg) for surviving 2D consumers
    // (hotspot/rift seeding, EarthSystem boundary normals). Removal
    // is deferred per project_physics_first_rewrite memory (11 live refs).
    float cx  = 0.0f;
    float cy  = 0.0f;
    float rot = 0.0f;
    float landFraction = 0.0f;
    int32_t ageEpochs = 0;
};

} // namespace aoc::map::gen
