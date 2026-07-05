#pragma once

/**
 * @file Plate.hpp
 * @brief Tectonic-plate description shared by extracted MapGenerator passes.
 */

#include <cstdint>

namespace aoc::map::gen {

/// Live plate state. Authoritative position is (latDeg, lonDeg) on the
/// sphere, recomputed each epoch from the raster cells the plate owns
/// (recomputePlateCentroidsFromCells); motion is parameterised by the
/// Euler pole + angular velocity and integrated on the raster by
/// advectPlateOwnership.
struct Plate {
    float latDeg = 0.0f;       // [-90, 90]
    float lonDeg = 0.0f;       // [-180, 180]
    float eulerPoleLatDeg = 0.0f;
    float eulerPoleLonDeg = 0.0f;
    float angularVelDeg   = 0.0f;
    // LEGACY: Mollweide-projection cache, written at init only (2D
    // motion was removed 2026-07-05). Remaining consumers: pushPlate
    // seeding, seed min-gap scans, hotspot nearLand placement, and the
    // setPlateCenters persist for the debug overlay (stale after
    // epoch 0 until the lat/lon conversion lands with raster docking).
    float cx  = 0.0f;
    float cy  = 0.0f;
    float landFraction = 0.0f;
};

} // namespace aoc::map::gen
