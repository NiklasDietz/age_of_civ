#include "aoc/map/gen/Terrane.hpp"

#include <cmath>

namespace aoc::map::gen {

Vec3 applyRot(const float rot[9], Vec3 v) noexcept {
    return Vec3{rot[0] * v.x + rot[1] * v.y + rot[2] * v.z,
                rot[3] * v.x + rot[4] * v.y + rot[5] * v.z,
                rot[6] * v.x + rot[7] * v.y + rot[8] * v.z};
}

Vec3 applyRotT(const float rot[9], Vec3 v) noexcept {
    return Vec3{rot[0] * v.x + rot[3] * v.y + rot[6] * v.z,
                rot[1] * v.x + rot[4] * v.y + rot[7] * v.z,
                rot[2] * v.x + rot[5] * v.y + rot[8] * v.z};
}

void composeRot(const float a[9], const float b[9], float out[9]) noexcept {
    float tmp[9];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            tmp[r * 3 + c] =
                a[r * 3 + 0] * b[0 * 3 + c] + a[r * 3 + 1] * b[1 * 3 + c] + a[r * 3 + 2] * b[2 * 3 + c];
        }
    }
    for (int i = 0; i < 9; ++i) {
        out[i] = tmp[i];
    }
}

void eulerRotMatrix(float poleLatDeg, float poleLonDeg, float angleDeg, float out[9]) noexcept {
    // Rodrigues, built directly as a matrix. The axis is the Euler pole as a
    // unit vector, so this is the same rotation rotateAroundEulerPole applies
    // to a single point -- expressed once per terrane per epoch instead of once
    // per cell, and composable so accumulated drift is a single multiply rather
    // than a chain of resamplings.
    const Vec3 k     = latLonToVec3(LatLon{poleLatDeg, poleLonDeg});
    const float th   = angleDeg * 0.01745329252f;
    const float c    = std::cos(th);
    const float s    = std::sin(th);
    const float t    = 1.0f - c;
    out[0] = t * k.x * k.x + c;
    out[1] = t * k.x * k.y - s * k.z;
    out[2] = t * k.x * k.z + s * k.y;
    out[3] = t * k.x * k.y + s * k.z;
    out[4] = t * k.y * k.y + c;
    out[5] = t * k.y * k.z - s * k.x;
    out[6] = t * k.x * k.z - s * k.y;
    out[7] = t * k.y * k.z + s * k.x;
    out[8] = t * k.z * k.z + c;
}

} // namespace aoc::map::gen
