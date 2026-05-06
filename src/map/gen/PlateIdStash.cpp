/**
 * @file PlateIdStash.cpp
 * @brief Per-tile plate-id assignment via haversine-closest plate on the
 *        sphere. 2026-05-06: rewritten for the physics-first rewrite (P3
 *        step 1). Replaced the legacy Voronoi point-in-polygon ownership +
 *        domain-warped Voronoi-fallback + 2-pass majority-vote smoother
 *        with a single haversine-distance pass. Tiles outside the
 *        Mollweide ellipse (polar voids) keep `0xFF` (unowned).
 */

#include "aoc/map/gen/PlateIdStash.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/gen/Plate.hpp"
#include "aoc/map/gen/SphereGeometry.hpp"

#include <cstdint>
#include <vector>

#ifdef _OPENMP
#  define AOC_PIS_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_PIS_PARALLEL_FOR_ROWS
#endif

namespace aoc::map::gen {

void runPlateIdStash(HexGrid& grid, bool /*cylindrical*/,
                     const std::vector<Plate>& plates,
                     aoc::Random& /*noiseRng*/) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    if (plates.empty()) { return; }

    AOC_PIS_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const MollweideInverseResult tileLL =
                tileToLatLon(col, row, width, height);
            if (!tileLL.valid) { continue; }

            float bestDistRad = 1e9f;
            int32_t nearest   = -1;
            for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                const Plate& p = plates[pi];
                const LatLon plateLatLon{p.latDeg, p.lonDeg};
                const float d = haversineRadians(tileLL.coord, plateLatLon);
                if (d < bestDistRad) {
                    bestDistRad = d;
                    nearest = static_cast<int32_t>(pi);
                }
            }
            if (nearest >= 0 && nearest < 255) {
                grid.setPlateId(row * width + col,
                    static_cast<uint8_t>(nearest));
            }
        }
    }
}

} // namespace aoc::map::gen
