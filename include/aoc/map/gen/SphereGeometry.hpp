#pragma once

/**
 * @file SphereGeometry.hpp
 * @brief Spherical-geometry primitives for the plate-tectonic simulation
 *        rewrite. Lat/lon <-> unit-vector conversions, haversine distance,
 *        Euler-pole rotation, and a Mollweide equal-area projection
 *        (forward + inverse).
 *
 * All angles are degrees on the public API. Internal trigonometry is in
 * radians. The unit sphere is the canonical representation; latitude is
 * geographic, not geocentric (no ellipsoidal flattening — Earth is treated
 * as a perfect sphere).
 *
 * The Mollweide projection emits coordinates in the unit square
 * [0, 1] x [0, 1]. The Mollweide ellipse has a 2:1 aspect ratio, so when
 * the unit square is sampled by a non-2:1 tile grid the ellipse will be
 * stretched. Callers that want true equal-area pixels should provision a
 * 2W x H tile grid (twice as wide as tall) and feed (mapX, mapY) =
 * ((col + 0.5) / (2W), (row + 0.5) / H) into mollweideInverse().
 */

#include <cstdint>

namespace aoc::map::gen {

/// Lat/lon position in degrees. latDeg in [-90, 90], lonDeg in [-180, 180].
/// Values outside these ranges are tolerated by the conversion functions
/// (they remain mathematically well-defined) but the Mollweide forward pass
/// expects normalised input.
struct LatLon {
    float latDeg;
    float lonDeg;
};

/// Cartesian unit vector on the unit sphere (||v|| = 1).
/// Convention: +x through (lat=0, lon=0), +y through (lat=0, lon=90),
/// +z through the north pole (lat=90).
struct Vec3 {
    float x;
    float y;
    float z;
};

// ---------------------------------------------------------------------------
// Coordinate conversions
// ---------------------------------------------------------------------------

/// Convert a (lat, lon) in degrees to a Cartesian unit vector on the unit
/// sphere. Uses the convention documented on Vec3.
[[nodiscard]] Vec3 latLonToVec3(LatLon p);

/// Convert a Cartesian vector to (lat, lon) in degrees. The input is
/// normalised internally; the caller does not need to pre-normalise.
/// Returns latDeg in [-90, 90] and lonDeg in [-180, 180].
[[nodiscard]] LatLon vec3ToLatLon(Vec3 v);

// ---------------------------------------------------------------------------
// Distances
// ---------------------------------------------------------------------------

/// Great-circle distance in radians (= angular separation on the unit
/// sphere) computed via the haversine formula. Numerically stable for both
/// short and antipodal point pairs.
[[nodiscard]] float haversineRadians(LatLon a, LatLon b);

/// Great-circle distance in kilometres assuming a spherical Earth of radius
/// 6371 km.
[[nodiscard]] float haversineKm(LatLon a, LatLon b);

// ---------------------------------------------------------------------------
// Euler-pole rotation
// ---------------------------------------------------------------------------

/// Rotate point `p` around an Euler pole (also given as a LatLon position)
/// by angleDeg degrees. Positive angle is counter-clockwise when looking
/// down at the pole from outside the sphere (right-hand rule with the
/// pole's outward unit vector as the axis). Returns the rotated lat/lon.
///
/// Implemented via Rodrigues' rotation formula on the Cartesian unit
/// vectors. Suitable for plate motion: each plate carries an Euler pole
/// and an angular speed; rotating the plate's geometry by `omega * dt`
/// advances it one step.
[[nodiscard]] LatLon rotateAroundEulerPole(
    LatLon p, LatLon pole, float angleDeg);

/// Tangent-plane velocity vector at `p` produced by Euler-pole rotation
/// around `pole` at `angularVelDeg` degrees per unit time. Returned as
/// (eastComponent, northComponent) in radians-per-unit-time -- magnitudes
/// directly comparable across plates regardless of latitude. Used for
/// boundary-stress dot-products: relV = vA - vB then project onto the
/// boundary normal.
///
/// Derivation: v = omega x r where omega is the Euler vector
/// (angularVelDeg * unit_vec(pole)) and r is unit_vec(p). The resulting
/// 3D vector is decomposed into the local east/north tangent basis at p.
struct TangentVelocity {
    float east;   // radians per unit time
    float north;
};
[[nodiscard]] TangentVelocity eulerVelocityAt(
    LatLon p, LatLon pole, float angularVelDeg);

/// Walk along a great circle from `start` at azimuth `azimuthDeg`
/// (clockwise from north, like a compass bearing) for `distanceRad`
/// radians of arc. Returns the endpoint (latDeg, lonDeg). Used by the
/// polygon-construction pass to ray-cast plate boundaries on the sphere
/// without going through unit-square coords.
[[nodiscard]] LatLon greatCircleWalk(
    LatLon start, float azimuthDeg, float distanceRad);

// ---------------------------------------------------------------------------
// Mollweide projection (equal-area pseudocylindrical)
// ---------------------------------------------------------------------------

/// Result of mollweideForward(). mapX and mapY are normalised to the unit
/// square [0, 1] x [0, 1] with the projection ellipse centred at
/// (0.5, 0.5). `inEllipse` is true when the projected point lies inside
/// the Mollweide ellipse; corner regions outside the ellipse still receive
/// numeric mapX/mapY but are flagged false so the caller can mask them.
struct MollweidePoint {
    float mapX;
    float mapY;
    bool  inEllipse;
};

/// Forward Mollweide: (lat, lon) -> (mapX, mapY) in [0, 1]^2.
/// Solves the auxiliary angle theta from 2*theta + sin(2*theta) =
/// pi*sin(lat) by Newton-Raphson (8 iterations cap, converges in ~3-4 for
/// any latitude). The unprojected coordinates land in
/// x in [-2*sqrt(2), 2*sqrt(2)] and y in [-sqrt(2), sqrt(2)] which are
/// then linearly remapped to [0, 1] x [0, 1].
[[nodiscard]] MollweidePoint mollweideForward(LatLon p);

/// Result of mollweideInverse(). `valid` is false when the input
/// (mapX, mapY) lies outside the Mollweide ellipse — in that case `coord`
/// is unspecified and must not be used.
struct MollweideInverseResult {
    LatLon coord;
    bool   valid;
};

/// Inverse Mollweide: (mapX, mapY) in [0, 1]^2 -> (lat, lon).
/// Returns `valid = false` when the input falls outside the ellipse.
/// The unit-square parametrisation matches mollweideForward(): a tile at
/// grid position (col, row) in a W x H map corresponds to
/// (mapX, mapY) = ((col + 0.5) / W, (row + 0.5) / H).
[[nodiscard]] MollweideInverseResult mollweideInverse(float mapX, float mapY);

/// Convenience wrapper around mollweideInverse() for tile grids. Computes
/// the (mapX, mapY) of the tile centre as (col / width, row / height) --
/// matching the legacy 2D Voronoi convention used in MapGenerator's
/// elevation / orogeny / plate-stash passes -- and returns the lat/lon (or
/// `valid = false` for tiles whose centre falls outside the Mollweide
/// ellipse, i.e. the polar voids).
[[nodiscard]] MollweideInverseResult tileToLatLon(
    int32_t col, int32_t row, int32_t width, int32_t height);

} // namespace aoc::map::gen
