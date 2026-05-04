#pragma once

/**
 * @file PolygonClipping.hpp
 * @brief Pure-math polygon helpers used by map-gen passes that work with
 *        plate / region polygons (clipping, area, centroid, validity).
 *
 * Added 2026-05-04. Single-precision float throughout to match the rest of
 * the gen/ subsystem. No dependency on Random / MapGenContext / hex grid:
 * this header is intentionally a leaf so any pass can pull it in without
 * link-cycle risk.
 *
 * All functions are exception-free. Degenerate inputs (empty / fewer than
 * three vertices) yield empty rings, zero area, or a (0, 0) centroid as
 * appropriate -- callers are expected to check.
 */

#include <utility>
#include <vector>

namespace aoc::map::gen {

/// CCW-oriented polygon ring as a flat list of (x, y) vertices. The ring
/// is implicitly closed (last vertex connects back to the first); callers
/// must NOT duplicate the first vertex at the end.
using PolygonRing = std::vector<std::pair<float, float>>;

/**
 * @brief Sutherland-Hodgman polygon clip.
 *
 * Returns the polygon that is the intersection of @p subject and @p clip.
 * Both polygons must be CONVEX and given in CCW order. The result is the
 * (possibly empty) convex polygon of overlap.
 *
 * @param subject  Convex CCW subject polygon.
 * @param clip     Convex CCW clip polygon.
 * @return         Intersection polygon (CCW), or empty ring on no overlap
 *                 / degenerate input.
 */
[[nodiscard]] PolygonRing clipPolygonSH(const PolygonRing& subject,
                                         const PolygonRing& clip);

/**
 * @brief Test whether a polygon is wound counter-clockwise via signed area.
 * @return true if signed area is strictly positive (CCW), false otherwise.
 *         Degenerate (<3 vertices) returns false.
 */
[[nodiscard]] bool isCCW(const PolygonRing& poly);

/**
 * @brief Reverse vertex order in-place if the polygon is CW. No-op if
 *        already CCW or degenerate.
 */
void ensureCCW(PolygonRing& poly);

/**
 * @brief Signed area of a polygon (shoelace formula).
 * @return Positive for CCW, negative for CW, zero for degenerate input.
 */
[[nodiscard]] float polygonArea(const PolygonRing& poly);

/// Axis-aligned bounding box returned by ::polygonAABB.
struct AABB {
    float minX;
    float minY;
    float maxX;
    float maxY;
};

/**
 * @brief Compute the axis-aligned bounding box of a polygon.
 * @return AABB enclosing all vertices. Returns a zero-size box at the
 *         origin for empty input.
 */
[[nodiscard]] AABB polygonAABB(const PolygonRing& poly);

/**
 * @brief Fast point-in-polygon test.
 *
 * Uses the supplied @p box as an early-out filter (cheap reject for points
 * far from the polygon), then a standard horizontal ray-cast crossing
 * count. Works for arbitrary simple polygons (convex or concave).
 *
 * @param px,py  Test point in the same coordinate space as @p poly.
 * @param poly   Polygon ring (any orientation).
 * @param box    Pre-computed AABB; pass ::polygonAABB(poly) if unsure.
 * @return       true if the point lies strictly inside @p poly.
 */
[[nodiscard]] bool pointInPolygon(float px, float py,
                                   const PolygonRing& poly,
                                   const AABB& box);

/**
 * @brief Geometric centroid of a polygon (area-weighted).
 *
 * Uses the standard signed-area centroid formula, which is correct for
 * any simple polygon regardless of winding. Falls back to (0, 0) on
 * degenerate input (fewer than three vertices or zero area).
 *
 * @return (cx, cy) centroid coordinates, or (0, 0) on degenerate input.
 */
[[nodiscard]] std::pair<float, float> polygonCentroid(const PolygonRing& poly);

/**
 * @brief Detect self-intersection.
 *
 * Returns true if any two non-adjacent edges of the polygon ring cross.
 * Used as a validity check after polygon-evolution steps that can fold
 * the boundary onto itself (e.g. plate growth, coastal smoothing).
 *
 * @param poly  Polygon ring.
 * @return      true if the polygon is self-intersecting.
 */
[[nodiscard]] bool isSelfIntersecting(const PolygonRing& poly);

/**
 * @brief Simplify a polygon in place.
 *
 * Removes consecutive vertices closer than @p epsilon and removes
 * collinear vertices (where the cross product of the two adjacent edges
 * is below @p epsilon in magnitude). The polygon is modified in place;
 * its winding is preserved.
 *
 * @param poly     Polygon ring (modified in place).
 * @param epsilon  Distance / cross-product tolerance. Default 1e-5.
 */
void simplifyPolygon(PolygonRing& poly, float epsilon = 1e-5f);

} // namespace aoc::map::gen
