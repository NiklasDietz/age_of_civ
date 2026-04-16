#pragma once

/**
 * @file HexCoord.hpp
 * @brief Hex coordinate types and constexpr conversion/utility functions.
 *
 * Three coordinate systems:
 *   - Offset (odd-r): storage format. col/row into a flat array.
 *   - Axial (q, r):   primary game-logic coordinate. Two-axis with implicit third.
 *   - Cube (q, r, s): constraint q+r+s==0. Used for distance, rotation, rings.
 *
 * Layout: pointy-top hexagons. Odd rows are shifted right by half a hex width.
 *
 * Reference: https://www.redblobgames.com/grids/hexagons/
 */

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>

namespace aoc::hex {

// ============================================================================
// Coordinate types
// ============================================================================

struct AxialCoord {
    int32_t q = 0;
    int32_t r = 0;
    constexpr bool operator==(const AxialCoord&) const = default;
};

struct CubeCoord {
    int32_t q = 0;
    int32_t r = 0;
    int32_t s = 0;
    constexpr bool operator==(const CubeCoord&) const = default;
};

struct OffsetCoord {
    int32_t col = 0;
    int32_t row = 0;
    constexpr bool operator==(const OffsetCoord&) const = default;
};

// ============================================================================
// Conversions (all constexpr)
// ============================================================================

/// Offset (odd-r) -> Axial
[[nodiscard]] constexpr AxialCoord offsetToAxial(OffsetCoord c) {
    int32_t q = c.col - (c.row - (c.row & 1)) / 2;
    int32_t r = c.row;
    return {q, r};
}

/// Axial -> Offset (odd-r)
[[nodiscard]] constexpr OffsetCoord axialToOffset(AxialCoord a) {
    int32_t col = a.q + (a.r - (a.r & 1)) / 2;
    int32_t row = a.r;
    return {col, row};
}

/// Axial -> Cube
[[nodiscard]] constexpr CubeCoord axialToCube(AxialCoord a) {
    return {a.q, a.r, -a.q - a.r};
}

/// Cube -> Axial
[[nodiscard]] constexpr AxialCoord cubeToAxial(CubeCoord c) {
    return {c.q, c.r};
}

// ============================================================================
// Distance
// ============================================================================

[[nodiscard]] constexpr int32_t abs(int32_t x) {
    return x < 0 ? -x : x;
}

/// Manhattan distance on the hex grid (flat / non-wrapping).
[[nodiscard]] constexpr int32_t distance(AxialCoord a, AxialCoord b) {
    CubeCoord ac = axialToCube(a);
    CubeCoord bc = axialToCube(b);
    return (abs(ac.q - bc.q) + abs(ac.r - bc.r) + abs(ac.s - bc.s)) / 2;
}

/// Distance accounting for east-west cylindrical wrapping.
/// Computes the minimum distance across direct path and the two wrapped paths
/// (shifting by +gridWidth and -gridWidth in offset-column space).
[[nodiscard]] inline int32_t wrappedDistance(AxialCoord a, AxialCoord b, int32_t gridWidth) {
    int32_t direct = distance(a, b);

    // Shift b by +gridWidth columns in offset space, convert back to axial
    OffsetCoord ob = axialToOffset(b);
    OffsetCoord shiftedPlus  = {ob.col + gridWidth, ob.row};
    OffsetCoord shiftedMinus = {ob.col - gridWidth, ob.row};

    int32_t dPlus  = distance(a, offsetToAxial(shiftedPlus));
    int32_t dMinus = distance(a, offsetToAxial(shiftedMinus));

    int32_t minDist = direct;
    if (dPlus  < minDist) { minDist = dPlus; }
    if (dMinus < minDist) { minDist = dMinus; }
    return minDist;
}

// ============================================================================
// Neighbors (pointy-top, axial coordinates)
// ============================================================================

/// The 6 axial direction vectors (pointy-top layout).
inline constexpr std::array<AxialCoord, 6> DIRECTIONS = {{
    { 1,  0}, { 1, -1}, { 0, -1},
    {-1,  0}, {-1,  1}, { 0,  1}
}};

/// Get the 6 neighbors of a hex.
[[nodiscard]] constexpr std::array<AxialCoord, 6> neighbors(AxialCoord c) {
    return {{
        {c.q + 1, c.r    }, {c.q + 1, c.r - 1}, {c.q,     c.r - 1},
        {c.q - 1, c.r    }, {c.q - 1, c.r + 1}, {c.q,     c.r + 1}
    }};
}

// ============================================================================
// Pixel conversion (pointy-top)
// ============================================================================

/// Hex size = outer radius (center to vertex).
/// Width  = sqrt(3) * size
/// Height = 2 * size

/// Axial coordinate -> world pixel center (pointy-top).
inline void axialToPixel(AxialCoord a, float hexSize, float& px, float& py) {
    const float sqrt3 = 1.7320508075688772f;
    px = hexSize * (sqrt3 * static_cast<float>(a.q) + sqrt3 * 0.5f * static_cast<float>(a.r));
    py = hexSize * (1.5f * static_cast<float>(a.r));
}

/// World pixel -> nearest axial coordinate (pointy-top). Uses cube rounding.
inline AxialCoord pixelToAxial(float px, float py, float hexSize) {
    const float sqrt3 = 1.7320508075688772f;
    float q = (sqrt3 / 3.0f * px - 1.0f / 3.0f * py) / hexSize;
    float r = (2.0f / 3.0f * py) / hexSize;

    // Cube round
    float s = -q - r;
    int32_t rq = static_cast<int32_t>(std::round(q));
    int32_t rr = static_cast<int32_t>(std::round(r));
    int32_t rs = static_cast<int32_t>(std::round(s));

    float dq = std::abs(static_cast<float>(rq) - q);
    float dr = std::abs(static_cast<float>(rr) - r);
    float ds = std::abs(static_cast<float>(rs) - s);

    if (dq > dr && dq > ds) {
        rq = -rr - rs;
    } else if (dr > ds) {
        rr = -rq - rs;
    }

    return {rq, rr};
}

/// Compute the 6 vertex positions for a pointy-top hex at a given center.
inline void hexVertices(float cx, float cy, float hexSize, float* outXY12) {
    constexpr float PI = 3.14159265358979323846f;
    for (int i = 0; i < 6; ++i) {
        float angle = PI / 180.0f * (60.0f * static_cast<float>(i) + 30.0f);
        outXY12[i * 2]     = cx + hexSize * std::cos(angle);
        outXY12[i * 2 + 1] = cy + hexSize * std::sin(angle);
    }
}

// ============================================================================
// Ring / spiral iteration
// ============================================================================

/// Get all hexes exactly N steps away from center (ring).
/// Appends to the output vector. Ring 0 = just the center.
template<typename OutputIt>
constexpr void ring(AxialCoord center, int32_t radius, OutputIt out) {
    if (radius == 0) {
        *out++ = center;
        return;
    }
    // Start at the hex radius steps in direction 4 (-q, +r)
    AxialCoord current = {center.q - radius, center.r + radius};
    for (std::size_t dir = 0; dir < 6; ++dir) {
        for (int32_t step = 0; step < radius; ++step) {
            *out++ = current;
            current = {current.q + DIRECTIONS[dir].q, current.r + DIRECTIONS[dir].r};
        }
    }
}

/// Get all hexes within radius N of center (filled disk).
template<typename OutputIt>
constexpr void spiral(AxialCoord center, int32_t radius, OutputIt out) {
    for (int32_t r = 0; r <= radius; ++r) {
        ring(center, r, out);
    }
}

} // namespace aoc::hex

// ============================================================================
// Hash for AxialCoord (for use in unordered containers)
// ============================================================================

namespace std {
template<>
struct hash<aoc::hex::AxialCoord> {
    size_t operator()(const aoc::hex::AxialCoord& c) const noexcept {
        uint64_t a = static_cast<uint64_t>(static_cast<int64_t>(c.q) + 1000000);
        uint64_t b = static_cast<uint64_t>(static_cast<int64_t>(c.r) + 1000000);
        uint64_t combined = (a + b) * (a + b + 1) / 2 + b;
        return hash<uint64_t>{}(combined);
    }
};
} // namespace std
