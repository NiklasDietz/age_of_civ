/**
 * @file SphereGeometry.cpp
 * @brief Implementation of the spherical-geometry primitives declared in
 *        aoc/map/gen/SphereGeometry.hpp.
 *
 * Pure math, no logging, no allocations, no globals. Every public function
 * is referentially transparent so the unit tests at the bottom of this
 * file (compiled when AOC_SPHERE_TESTS is defined) can exercise them in
 * isolation.
 */

#include "aoc/map/gen/SphereGeometry.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::map::gen {

namespace {

// Hardcoded pi as a float -- M_PI is a non-standard double constant and
// promoting it to float at every site adds noise to hot paths.
constexpr float kPi      = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

// Earth's mean radius in km. Used by haversineKm only; kept local because
// no other call site in this module needs it.
constexpr float kEarthRadiusKm = 6371.0f;

// Mollweide projection constants. The unprojected ellipse spans
// x in [-2*sqrt(2), 2*sqrt(2)] and y in [-sqrt(2), sqrt(2)] which gives
// the standard 2:1 aspect ratio. We rescale that to the unit square below.
const float kSqrt2     = std::sqrt(2.0f);
const float kTwoSqrt2  = 2.0f * kSqrt2;
const float kFourSqrt2 = 4.0f * kSqrt2;

// Newton-Raphson tolerance + iteration cap for the Mollweide auxiliary
// angle solve. 1e-6 is well below single-precision noise; 8 iterations
// is comfortably above the empirical 3-4 needed at any latitude.
constexpr float kMollweideTolerance = 1.0e-6f;
constexpr int   kMollweideMaxIters  = 8;

/// Solve 2*theta + sin(2*theta) = pi * sin(lat) for theta via
/// Newton-Raphson. Initial guess is lat itself, which is excellent at low
/// latitudes and acceptable at the poles (the iteration converges in a
/// handful of steps either way). At the poles the derivative
/// 2 + 2*cos(2*theta) collapses to zero, so we clamp the input there and
/// short-circuit.
float solveMollweideTheta(float latRad) {
    // Pole short-circuit: theta = +/- pi/2 exactly.
    const float halfPi = 0.5f * kPi;
    if (latRad >=  halfPi - 1.0e-7f) { return  halfPi; }
    if (latRad <= -halfPi + 1.0e-7f) { return -halfPi; }

    const float target = kPi * std::sin(latRad);
    float theta = latRad; // initial guess

    for (int iter = 0; iter < kMollweideMaxIters; ++iter) {
        const float twoTheta   = 2.0f * theta;
        const float residual   = twoTheta + std::sin(twoTheta) - target;
        const float derivative = 2.0f + 2.0f * std::cos(twoTheta);
        // Derivative bottoms out at the poles; guarded above.
        const float step       = residual / derivative;
        theta -= step;
        if (std::fabs(step) < kMollweideTolerance) { break; }
    }
    return theta;
}

} // namespace

// ---------------------------------------------------------------------------
// Coordinate conversions
// ---------------------------------------------------------------------------

Vec3 latLonToVec3(LatLon p) {
    const float latRad = p.latDeg * kDegToRad;
    const float lonRad = p.lonDeg * kDegToRad;
    const float cosLat = std::cos(latRad);
    Vec3 v;
    v.x = cosLat * std::cos(lonRad);
    v.y = cosLat * std::sin(lonRad);
    v.z = std::sin(latRad);
    return v;
}

LatLon vec3ToLatLon(Vec3 v) {
    // Normalise defensively so callers can pass non-unit vectors (e.g.
    // straight from a rotation that has accumulated rounding error).
    const float lengthSq = v.x * v.x + v.y * v.y + v.z * v.z;
    const float length   = std::sqrt(lengthSq);
    const float invLen   = (length > 0.0f) ? (1.0f / length) : 0.0f;
    const float nx = v.x * invLen;
    const float ny = v.y * invLen;
    const float nz = v.z * invLen;

    LatLon out;
    out.latDeg = std::asin(std::clamp(nz, -1.0f, 1.0f)) * kRadToDeg;
    out.lonDeg = std::atan2(ny, nx) * kRadToDeg;
    return out;
}

// ---------------------------------------------------------------------------
// Distances
// ---------------------------------------------------------------------------

float haversineRadians(LatLon a, LatLon b) {
    const float lat1 = a.latDeg * kDegToRad;
    const float lat2 = b.latDeg * kDegToRad;
    const float dLat = (b.latDeg - a.latDeg) * kDegToRad;
    const float dLon = (b.lonDeg - a.lonDeg) * kDegToRad;

    const float sinHalfDLat = std::sin(0.5f * dLat);
    const float sinHalfDLon = std::sin(0.5f * dLon);

    const float h = sinHalfDLat * sinHalfDLat
                  + std::cos(lat1) * std::cos(lat2)
                  * sinHalfDLon * sinHalfDLon;

    // asin clamp guards against h slightly above 1 due to rounding at
    // antipodal points.
    return 2.0f * std::asin(std::min(1.0f, std::sqrt(h)));
}

float haversineKm(LatLon a, LatLon b) {
    return haversineRadians(a, b) * kEarthRadiusKm;
}

// ---------------------------------------------------------------------------
// Euler-pole rotation (Rodrigues' formula)
// ---------------------------------------------------------------------------

LatLon rotateAroundEulerPole(LatLon p, LatLon pole, float angleDeg) {
    const Vec3 pVec = latLonToVec3(p);
    const Vec3 kVec = latLonToVec3(pole); // pole is already a unit vector

    const float angleRad = angleDeg * kDegToRad;
    const float c = std::cos(angleRad);
    const float s = std::sin(angleRad);
    const float oneMinusC = 1.0f - c;

    // k . v
    const float kDotV = kVec.x * pVec.x + kVec.y * pVec.y + kVec.z * pVec.z;

    // k x v
    const float crossX = kVec.y * pVec.z - kVec.z * pVec.y;
    const float crossY = kVec.z * pVec.x - kVec.x * pVec.z;
    const float crossZ = kVec.x * pVec.y - kVec.y * pVec.x;

    // Rodrigues: v_rot = v*cos + (k x v)*sin + k*(k . v)*(1 - cos)
    Vec3 rotated;
    rotated.x = pVec.x * c + crossX * s + kVec.x * kDotV * oneMinusC;
    rotated.y = pVec.y * c + crossY * s + kVec.y * kDotV * oneMinusC;
    rotated.z = pVec.z * c + crossZ * s + kVec.z * kDotV * oneMinusC;

    return vec3ToLatLon(rotated);
}

LatLon greatCircleWalk(LatLon start, float azimuthDeg, float distanceRad) {
    const float latRad = start.latDeg * kDegToRad;
    const float lonRad = start.lonDeg * kDegToRad;
    const float az     = azimuthDeg * kDegToRad;
    const float sinLat = std::sin(latRad);
    const float cosLat = std::cos(latRad);
    const float sinD   = std::sin(distanceRad);
    const float cosD   = std::cos(distanceRad);

    const float sinNewLat = sinLat * cosD + cosLat * sinD * std::cos(az);
    const float clampedSinLat = std::clamp(sinNewLat, -1.0f, 1.0f);
    const float newLat = std::asin(clampedSinLat);
    const float newLon = lonRad + std::atan2(
        std::sin(az) * sinD * cosLat,
        cosD - sinLat * clampedSinLat);

    LatLon out;
    out.latDeg = newLat * kRadToDeg;
    out.lonDeg = newLon * kRadToDeg;
    // Wrap longitude to [-180, 180].
    while (out.lonDeg >  180.0f) { out.lonDeg -= 360.0f; }
    while (out.lonDeg < -180.0f) { out.lonDeg += 360.0f; }
    return out;
}

TangentVelocity eulerVelocityAt(LatLon p, LatLon pole, float angularVelDeg) {
    // omega = angularVelRad * pole_unit_vec
    const float angVelRad = angularVelDeg * kDegToRad;
    const Vec3 poleVec = latLonToVec3(pole);
    const Vec3 pVec    = latLonToVec3(p);

    // v3D = omega x r (in 3D Cartesian)
    const float vx3 = angVelRad * (poleVec.y * pVec.z - poleVec.z * pVec.y);
    const float vy3 = angVelRad * (poleVec.z * pVec.x - poleVec.x * pVec.z);
    const float vz3 = angVelRad * (poleVec.x * pVec.y - poleVec.y * pVec.x);

    // Local east basis at p: e_east = (-sin(lon), cos(lon), 0)
    // Local north basis at p: e_north = (-sin(lat)cos(lon), -sin(lat)sin(lon), cos(lat))
    const float latRad = p.latDeg * kDegToRad;
    const float lonRad = p.lonDeg * kDegToRad;
    const float cLat = std::cos(latRad);
    const float sLat = std::sin(latRad);
    const float cLon = std::cos(lonRad);
    const float sLon = std::sin(lonRad);

    TangentVelocity tv;
    tv.east  = -sLon * vx3 +  cLon * vy3;
    tv.north = -sLat * cLon * vx3 - sLat * sLon * vy3 + cLat * vz3;
    return tv;
}

// ---------------------------------------------------------------------------
// Mollweide forward + inverse
// ---------------------------------------------------------------------------

MollweidePoint mollweideForward(LatLon p) {
    const float latRad = p.latDeg * kDegToRad;
    const float lonRad = p.lonDeg * kDegToRad;

    const float theta = solveMollweideTheta(latRad);
    const float cosTheta = std::cos(theta);
    const float sinTheta = std::sin(theta);

    // Unprojected coordinates: x in [-2*sqrt(2), 2*sqrt(2)], y in [-sqrt(2), sqrt(2)].
    const float x = (kTwoSqrt2 / kPi) * lonRad * cosTheta;
    const float y = kSqrt2 * sinTheta;

    // Linearly remap to the unit square. The full unprojected range maps
    // onto [0, 1]; the ellipse occupies the central diamond of this
    // bounding box. dx, dy below are normalised offsets from the centre.
    MollweidePoint out;
    out.mapX = 0.5f + x / kFourSqrt2;
    out.mapY = 0.5f + y / kTwoSqrt2;

    // Ellipse test in normalised coords: the ellipse touches mapX = 0 / 1
    // along the equator and mapY = 0 / 1 at the poles. Semi-axes are 0.5
    // each in the normalised frame, so the canonical x^2 + y^2 <= 1 test
    // (with both deltas multiplied by 2) suffices.
    const float dx = (out.mapX - 0.5f) * 2.0f;
    const float dy = (out.mapY - 0.5f) * 2.0f;
    out.inEllipse = (dx * dx + dy * dy) <= 1.0f + 1.0e-5f;
    return out;
}

MollweideInverseResult mollweideInverse(float mapX, float mapY) {
    MollweideInverseResult result;
    result.valid = false;
    result.coord = LatLon{0.0f, 0.0f};

    // Reverse the unit-square remap.
    const float x = (mapX - 0.5f) * kFourSqrt2;
    const float y = (mapY - 0.5f) * kTwoSqrt2;

    // Reject points outside the Mollweide ellipse before any trig: the
    // inverse formulas explode (division by cos(theta) -> 0) at the
    // boundary and produce NaNs beyond it.
    const float dx = (mapX - 0.5f) * 2.0f;
    const float dy = (mapY - 0.5f) * 2.0f;
    if ((dx * dx + dy * dy) > 1.0f) {
        return result;
    }

    // theta from y-axis. Clamp guards against rounding at the poles.
    const float sinTheta = std::clamp(y / kSqrt2, -1.0f, 1.0f);
    const float theta    = std::asin(sinTheta);
    const float cosTheta = std::cos(theta);

    // Latitude from the auxiliary angle relation.
    const float sinLat = (2.0f * theta + std::sin(2.0f * theta)) / kPi;
    const float latRad = std::asin(std::clamp(sinLat, -1.0f, 1.0f));

    // Longitude. cosTheta -> 0 only exactly at the poles; degenerate case
    // collapses to lon = 0 which is the convention for a pole singularity.
    float lonRad = 0.0f;
    if (cosTheta > 1.0e-7f) {
        lonRad = (kPi * x) / (kTwoSqrt2 * cosTheta);
    }

    float lonDeg = lonRad * kRadToDeg;
    lonDeg = std::clamp(lonDeg, -180.0f, 180.0f);

    result.coord.latDeg = latRad * kRadToDeg;
    result.coord.lonDeg = lonDeg;
    result.valid = true;
    return result;
}

MollweideInverseResult tileToLatLon(
    int32_t col, int32_t row, int32_t width, int32_t height) {
    // Matches the legacy Voronoi convention used by the elevation /
    // orogeny / plate-stash passes: nx = col / width, ny = row / height
    // (NOT the centred (col + 0.5) / width form). Switching the
    // sampling convention would shift every tile half a pixel and
    // perturb determinism for downstream noise lookups that take the
    // same nx/ny as input.
    const float mapX = (width  > 0) ? (static_cast<float>(col) / static_cast<float>(width))  : 0.5f;
    const float mapY = (height > 0) ? (static_cast<float>(row) / static_cast<float>(height)) : 0.5f;
    return mollweideInverse(mapX, mapY);
}

} // namespace aoc::map::gen


// ===========================================================================
// Inline tests (compile with -DAOC_SPHERE_TESTS to build the test main).
// Pure stdout output -- no test framework dependency. Returns non-zero on
// any failure so the binary can be wired into CI as a smoke test.
// ===========================================================================
#ifdef AOC_SPHERE_TESTS

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

int g_failures = 0;

bool approxEqual(float a, float b, float tol) {
    return std::fabs(a - b) <= tol;
}

void check(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", label);
        ++g_failures;
    } else {
        std::fprintf(stdout, "[ OK ] %s\n", label);
    }
}

} // namespace

int main() {
    using aoc::map::gen::LatLon;
    using aoc::map::gen::haversineRadians;
    using aoc::map::gen::rotateAroundEulerPole;
    using aoc::map::gen::mollweideForward;
    using aoc::map::gen::mollweideInverse;

    const float pi = 3.14159265358979323846f;

    // 1. haversine of two equator points 90 deg apart = pi/2 radians.
    {
        const float d = haversineRadians(
            LatLon{0.0f, 0.0f}, LatLon{0.0f, 90.0f});
        check(approxEqual(d, 0.5f * pi, 1.0e-5f),
              "haversine equator 90deg == pi/2");
    }

    // 2. Rotate equatorial point 90 deg around the north pole -> longitude
    //    advances by 90 deg.
    {
        const LatLon rotated = rotateAroundEulerPole(
            LatLon{0.0f, 0.0f}, LatLon{90.0f, 0.0f}, 90.0f);
        check(approxEqual(rotated.latDeg, 0.0f, 1.0e-4f),
              "Euler-pole rotation preserves equator latitude");
        check(approxEqual(rotated.lonDeg, 90.0f, 1.0e-3f),
              "Euler-pole rotation advances longitude by +90");
    }

    // 3. mollweideForward(0, 0) -> (0.5, 0.5, true).
    {
        const aoc::map::gen::MollweidePoint mp =
            mollweideForward(LatLon{0.0f, 0.0f});
        check(approxEqual(mp.mapX, 0.5f, 1.0e-5f),
              "mollweideForward(0,0).mapX == 0.5");
        check(approxEqual(mp.mapY, 0.5f, 1.0e-5f),
              "mollweideForward(0,0).mapY == 0.5");
        check(mp.inEllipse, "mollweideForward(0,0).inEllipse == true");
    }

    // 4. mollweideForward(90, 0) -> (0.5, ~1.0, true).
    {
        const aoc::map::gen::MollweidePoint mp =
            mollweideForward(LatLon{90.0f, 0.0f});
        check(approxEqual(mp.mapX, 0.5f, 1.0e-4f),
              "mollweideForward(90,0).mapX == 0.5");
        check(approxEqual(mp.mapY, 1.0f, 1.0e-4f),
              "mollweideForward(90,0).mapY ~ 1.0");
    }

    // 5. mollweideInverse(0.5, 0.5) -> (0, 0, valid).
    {
        const aoc::map::gen::MollweideInverseResult r =
            mollweideInverse(0.5f, 0.5f);
        check(r.valid, "mollweideInverse(0.5,0.5).valid == true");
        check(approxEqual(r.coord.latDeg, 0.0f, 1.0e-4f),
              "mollweideInverse(0.5,0.5).latDeg == 0");
        check(approxEqual(r.coord.lonDeg, 0.0f, 1.0e-4f),
              "mollweideInverse(0.5,0.5).lonDeg == 0");
    }

    // 6. mollweideInverse(0.0, 0.5) -> longitude near -180.
    {
        const aoc::map::gen::MollweideInverseResult r =
            mollweideInverse(0.0f, 0.5f);
        check(r.valid, "mollweideInverse(0.0,0.5).valid == true");
        check(approxEqual(r.coord.lonDeg, -180.0f, 1.0e-2f),
              "mollweideInverse(0.0,0.5).lonDeg ~ -180");
    }

    if (g_failures > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::fprintf(stdout, "all sphere geometry tests passed\n");
    return EXIT_SUCCESS;
}

#endif // AOC_SPHERE_TESTS
