/**
 * @file Relief.cpp
 * @brief Local topographic relief from the hex elevation field.
 */

#include "aoc/map/gen/Relief.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace aoc::map::gen {

namespace {

/// Length of one degree of latitude, km. One degree of longitude is this times
/// cos(latitude).
constexpr float KM_PER_DEGREE = 111.32f;

/// One unitless elevation step is this many metres (surfaceElevationM / 5000).
constexpr float ELEV_UNIT_TO_M = 5000.0f;

/// Smallest spacing accepted, km. Guards the division when a projection
/// degenerates -- Lambert's east-west pitch goes to zero AT the pole, and an
/// unguarded divide would report infinite relief for the polar rows.
constexpr float MIN_SPACING_KM = 1.0f;

} // namespace

TileSpacingKm tileSpacingKm(const HexGrid& grid, int32_t row) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const float latDeg   = grid.rowLatitudeDeg(row);

    // East-west: one column is 360/width degrees of longitude, shrinking with
    // cos(latitude).
    const float lonStepDeg = 360.0f / static_cast<float>(std::max(1, width));
    const float cosLat     = std::cos(latDeg * 0.01745329252f);
    const float ewKm       = lonStepDeg * KM_PER_DEGREE * std::max(0.0f, cosLat);

    // North-south: finite-difference the grid's own latitude table, so this
    // works for any projection. One-sided at the two polar rows.
    const int32_t rowAbove = std::max(0, row - 1);
    const int32_t rowBelow = std::min(height - 1, row + 1);
    float nsKm             = 0.0f;
    if (rowBelow > rowAbove) {
        const float dLat = std::fabs(grid.rowLatitudeDeg(rowBelow) - grid.rowLatitudeDeg(rowAbove));
        nsKm             = dLat * KM_PER_DEGREE / static_cast<float>(rowBelow - rowAbove);
    }

    return TileSpacingKm{std::max(MIN_SPACING_KM, ewKm), std::max(MIN_SPACING_KM, nsKm)};
}

float localReliefMPer100Km(const HexGrid& grid, const std::vector<float>& elevationMap,
                           float waterThreshold, int32_t col, int32_t row) {
    const int32_t width    = grid.width();
    const int32_t height   = grid.height();
    const bool cylindrical = (grid.topology() == MapTopology::Cylindrical);
    const std::size_t here = static_cast<std::size_t>(row * width + col);
    if (here >= elevationMap.size()) {
        return 0.0f;
    }
    const float elevHere      = elevationMap[here];
    const TileSpacingKm pitch = tileSpacingKm(grid, row);

    float worst = 0.0f;
    for (int32_t dir = 0; dir < 6; ++dir) {
        int32_t nIdx = 0;
        if (!hexNeighbor(width, height, cylindrical, col, row, dir, nIdx)) {
            continue;
        }
        const std::size_t n = static_cast<std::size_t>(nIdx);
        if (n >= elevationMap.size()) {
            continue;
        }
        if (elevationMap[n] < waterThreshold) {
            continue; // see header: the continental slope is not roughness
        }
        // hexNeighbor directions 0 and 1 are the two pure east-west
        // neighbours; 2-5 change row, and their displacement is dominated by
        // the north-south pitch. Using one pitch per group rather than the
        // exact per-direction vector is an approximation, but it removes the
        // latitude and map-width dependence, which is what matters for a
        // threshold test.
        const float spacingKm = (dir < 2) ? pitch.eastWest : pitch.northSouth;
        const float dropM     = std::fabs(elevationMap[n] - elevHere) * ELEV_UNIT_TO_M;
        worst                 = std::max(worst, dropM * 100.0f / spacingKm);
    }
    return worst;
}

} // namespace aoc::map::gen
