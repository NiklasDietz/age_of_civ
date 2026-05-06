#pragma once

/**
 * @file PlateReference.hpp
 * @brief Real-world reference plate catalog used to seed continental vs
 *        oceanic crust composition in the physics-first sphere simulation.
 *
 * Source: P. Bird, "An updated digital model of plate boundaries",
 *   Geochemistry, Geophysics, Geosystems 4(3), 2003.
 *   doi:10.1029/2001GC000252
 *
 * The Bird (2003) PB2002 model identifies 52 plates (14 large + 38 micro/
 * orogen). For each plate the catalog records a representative geographic
 * centroid (lat/lon of the plate's areal centre, computed offline from the
 * digital boundary geometry) and a bulk composition class (continental,
 * oceanic, or mixed).
 *
 * The simulation uses this catalog to assign a `PlateCompositionType` to
 * each in-sim plate via nearest-centroid lookup at sim init: the in-sim
 * plate inherits the composition of the closest Bird plate by haversine
 * distance. This replaces the legacy fractalNoise-based continental seeding
 * inherited from the Voronoi-era pipeline.
 *
 * Plate names retain Bird's convention (capitalised geographic identifier,
 * no abbreviations).
 */

#include "aoc/map/gen/SphereGeometry.hpp"

#include <cstddef>
#include <cstdint>

namespace aoc::map::gen {

enum class PlateCompositionType : uint8_t {
    Continental = 0,
    Oceanic     = 1,
    Mixed       = 2,
};

struct ReferencePlate {
    const char*          name;
    LatLon               centroid;
    PlateCompositionType type;
};

/// Static catalog of the 52 Bird (2003) plates with attribution above.
/// Stable address; safe to store iterators / pointers into this span.
[[nodiscard]] const ReferencePlate* birdPlateCatalog() noexcept;
[[nodiscard]] std::size_t           birdPlateCatalogSize() noexcept;

/// Classify an in-sim plate by haversine distance to the nearest Bird (2003)
/// reference plate centroid. Returns the composition class of the nearest
/// reference plate.
[[nodiscard]] PlateCompositionType classifyByNearestReference(
    LatLon centroid) noexcept;

} // namespace aoc::map::gen
