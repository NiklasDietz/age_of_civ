// PolygonSpatialIndex.cpp -- coarse uniform-grid hash over plate AABBs.
//
// Added 2026-05-04. See PolygonSpatialIndex.hpp for design notes. The
// implementation is intentionally minimal: no allocator pools, no SIMD, no
// hierarchical refinement. The grid is small (32x16 = 512 cells) and the
// rebuild fires a handful of times per generator run, so the simplest
// possible loop is the right answer.

#include "aoc/map/gen/PolygonSpatialIndex.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::map::gen {

const std::vector<std::uint8_t> PolygonSpatialIndex::s_empty;

namespace {

/// Map a unit-square coord (clamped to [0, 1)) to a cell index in [0, dim).
[[nodiscard]] inline std::int32_t cellIndex(float coord, std::int32_t dim) noexcept
{
    // Clamp first so polygon AABBs that drifted outside [0, 1] still hit
    // valid edge cells. Plate motion can push centroids ~0.05 outside on
    // long sims; without clamping we would index a negative cell and
    // skip those plates entirely.
    if (!(coord > 0.0f)) { return 0; }
    if (coord >= 1.0f)   { return dim - 1; }
    const std::int32_t idx = static_cast<std::int32_t>(
        std::floor(coord * static_cast<float>(dim)));
    if (idx < 0)        { return 0; }
    if (idx >= dim)     { return dim - 1; }
    return idx;
}

} // namespace

void PolygonSpatialIndex::rebuild(
    const std::vector<std::array<float, 4>>& aabbs)
{
    constexpr std::size_t kCells =
        static_cast<std::size_t>(GRID_W) * static_cast<std::size_t>(GRID_H);
    if (m_buckets.size() != kCells) {
        m_buckets.assign(kCells, {});
    } else {
        for (auto& bucket : m_buckets) { bucket.clear(); }
    }

    constexpr std::size_t kMaxPlateId = 255u;
    const std::size_t plateCount = std::min(aabbs.size(), kMaxPlateId);
    for (std::size_t pi = 0; pi < plateCount; ++pi) {
        const auto& box = aabbs[pi];
        const float minX = box[0];
        const float minY = box[1];
        const float maxX = box[2];
        const float maxY = box[3];
        // Skip degenerate / empty polygons (min > max means uninitialised
        // / cleared in MapGenerator).
        if (!(maxX >= minX) || !(maxY >= minY)) { continue; }

        const std::int32_t c0x = cellIndex(minX, GRID_W);
        const std::int32_t c1x = cellIndex(maxX, GRID_W);
        const std::int32_t c0y = cellIndex(minY, GRID_H);
        const std::int32_t c1y = cellIndex(maxY, GRID_H);
        const std::uint8_t plateId = static_cast<std::uint8_t>(pi);
        for (std::int32_t cy = c0y; cy <= c1y; ++cy) {
            const std::size_t row =
                static_cast<std::size_t>(cy) * static_cast<std::size_t>(GRID_W);
            for (std::int32_t cx = c0x; cx <= c1x; ++cx) {
                m_buckets[row + static_cast<std::size_t>(cx)].push_back(
                    plateId);
            }
        }
    }
}

const std::vector<std::uint8_t>& PolygonSpatialIndex::candidatesAt(
    float worldX, float worldY) const
{
    if (m_buckets.empty()) { return s_empty; }
    const std::int32_t cx = cellIndex(worldX, GRID_W);
    const std::int32_t cy = cellIndex(worldY, GRID_H);
    const std::size_t idx = static_cast<std::size_t>(cy)
        * static_cast<std::size_t>(GRID_W)
        + static_cast<std::size_t>(cx);
    return m_buckets[idx];
}

} // namespace aoc::map::gen
