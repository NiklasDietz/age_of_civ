#pragma once

/**
 * @file PolygonSpatialIndex.hpp
 * @brief Coarse spatial-hash index over plate-polygon AABBs.
 *
 * Added 2026-05-04. The map-gen polygon-ownership override pass scans every
 * plate's polygon for every tile (O(plates * verts) per tile). With ~12
 * plates and ~80 000 tiles that is ~960 k AABB checks per pass. Bucketing
 * polygon AABBs into a coarse uniform grid lets each tile look up only the
 * plates whose AABB intersects its bucket -- typically 2-3 candidates --
 * cutting the candidate count by ~5x without changing the PIP geometry.
 *
 * The index is intentionally a leaf header: no MapGenerator or Plate
 * dependency, so any pass can use it in isolation. World coordinates are
 * the plate-coordinate system used elsewhere in gen/ (unit square [0, 1]).
 * Polygon AABBs that drift slightly outside [0, 1] due to plate motion are
 * clamped to the grid bounds; cylindrical wrap is NOT modelled here -- the
 * existing PIP fast-reject path also does no wrap, so behaviour matches.
 *
 * All operations are deterministic and exception-free. Failed / out-of-
 * range lookups return a reference to a shared empty vector (caller falls
 * back to the full Voronoi scan).
 */

#include <array>
#include <cstdint>
#include <vector>

namespace aoc::map::gen {

/// Coarse spatial-hash index: divides the unit square into
/// @ref GRID_W × @ref GRID_H cells. Each cell stores plate-IDs whose polygon
/// AABB intersects it. Per-tile lookup: O(1) hash to find candidate plates.
///
/// Plate IDs are stored as @c uint8_t (cap = 255). MapGenerator currently
/// runs ~12 majors + microplates, well below the cap; if the cap is ever
/// exceeded, ::rebuild silently truncates and the override falls back to
/// the full Voronoi scan for affected tiles (correct, just slower).
class PolygonSpatialIndex {
public:
    /// Grid dimensions. Tuned for ~12 plates over a unit square: 2 px
    /// per cell horizontally, 4 px vertically at 64x32 grid resolution.
    /// Empirical sweet spot between bucket count and per-bucket size.
    static constexpr std::int32_t GRID_W = 32;
    static constexpr std::int32_t GRID_H = 16;

    /// Reset and rebuild from a list of plate AABBs. Each entry is
    /// (minX, minY, maxX, maxY) for the plate at that index. Plate AABBs
    /// with min > max (degenerate / empty polygon) are silently skipped.
    /// Plate indices >= 255 are silently dropped (uint8_t bucket type).
    void rebuild(const std::vector<std::array<float, 4>>& aabbs);

    /// Returns plate IDs whose AABB intersects the cell containing
    /// (worldX, worldY). Cheap: returns reference to internal bucket. The
    /// returned reference is valid until the next ::rebuild call.
    [[nodiscard]] const std::vector<std::uint8_t>& candidatesAt(
        float worldX, float worldY) const;

private:
    std::vector<std::vector<std::uint8_t>> m_buckets; // size GRID_W * GRID_H
    static const std::vector<std::uint8_t> s_empty;
};

} // namespace aoc::map::gen
