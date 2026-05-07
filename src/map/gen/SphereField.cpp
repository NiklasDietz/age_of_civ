#include "aoc/map/gen/SphereField.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::map::gen {

void SphereField::resize() {
    surfaceElevationM.assign(CELL_COUNT, 0.0f);
    crustThicknessKm.assign(CELL_COUNT, 0.0f);
    continentalFraction.assign(CELL_COUNT, 0.0f);
    plateId.assign(CELL_COUNT, static_cast<int16_t>(-1));
    convergenceRateRadPerMy.assign(CELL_COUNT, 0.0f);
    crustAgeMy.assign(CELL_COUNT, 0.0f);
}

LatLon SphereField::cellCenter(int32_t lonIdx, int32_t latIdx) noexcept {
    LatLon p;
    p.lonDeg = -180.0f + (static_cast<float>(lonIdx) + 0.5f) * CELL_DEG;
    p.latDeg =  -90.0f + (static_cast<float>(latIdx) + 0.5f) * CELL_DEG;
    return p;
}

SphereField::CellCoord SphereField::locate(float latDeg, float lonDeg) noexcept {
    // Wrap longitude to [-180, 180).
    float lon = std::fmod(lonDeg + 180.0f, 360.0f);
    if (lon < 0.0f) lon += 360.0f;
    // Clamp latitude to [-90, 90].
    float lat = std::clamp(latDeg, -90.0f, 90.0f);

    int32_t lonIdx = static_cast<int32_t>(lon / CELL_DEG);
    int32_t latIdx = static_cast<int32_t>((lat + 90.0f) / CELL_DEG);
    if (lonIdx >= LON_CELLS) lonIdx = LON_CELLS - 1;
    if (latIdx >= LAT_CELLS) latIdx = LAT_CELLS - 1;
    if (lonIdx < 0) lonIdx = 0;
    if (latIdx < 0) latIdx = 0;
    return {lonIdx, latIdx};
}

float SphereField::bilinearSample(
    const std::vector<float>& field, float latDeg, float lonDeg) const noexcept {
    // Continuous fractional cell coordinate, with cell centres at
    // (lonIdx + 0.5, latIdx + 0.5) -- so the sample at the centre of
    // cell (i, j) is exactly field[index(i, j)] (no smoothing).
    float lon = std::fmod(lonDeg + 180.0f, 360.0f);
    if (lon < 0.0f) lon += 360.0f;
    float lat = std::clamp(latDeg, -90.0f, 90.0f);

    float fx = lon / CELL_DEG - 0.5f;
    float fy = (lat + 90.0f) / CELL_DEG - 0.5f;

    int32_t i0 = static_cast<int32_t>(std::floor(fx));
    int32_t j0 = static_cast<int32_t>(std::floor(fy));
    float tx = fx - static_cast<float>(i0);
    float ty = fy - static_cast<float>(j0);

    // Longitude wraps periodically.
    auto wrapLon = [](int32_t i) noexcept -> int32_t {
        i %= LON_CELLS;
        if (i < 0) i += LON_CELLS;
        return i;
    };
    // Latitude clamps at the poles (no antipodal wrap; the pole
    // singularity is not a cell-centre and bilinear behaves correctly
    // by repeating the polar row).
    auto clampLat = [](int32_t j) noexcept -> int32_t {
        if (j < 0) return 0;
        if (j >= LAT_CELLS) return LAT_CELLS - 1;
        return j;
    };

    int32_t i1 = wrapLon(i0 + 1);
    int32_t j1 = clampLat(j0 + 1);
    int32_t iA = wrapLon(i0);
    int32_t jA = clampLat(j0);

    const float v00 = field[cellIndex(iA, jA)];
    const float v10 = field[cellIndex(i1, jA)];
    const float v01 = field[cellIndex(iA, j1)];
    const float v11 = field[cellIndex(i1, j1)];

    const float a = v00 * (1.0f - tx) + v10 * tx;
    const float b = v01 * (1.0f - tx) + v11 * tx;
    return a * (1.0f - ty) + b * ty;
}

float SphereField::peakSample(
    const std::vector<float>& field, float latDeg, float lonDeg,
    int32_t halfSearchCells) const noexcept {
    const CellCoord c = locate(latDeg, lonDeg);
    auto wrapLon = [](int32_t i) noexcept -> int32_t {
        i %= LON_CELLS;
        if (i < 0) i += LON_CELLS;
        return i;
    };
    auto clampLat = [](int32_t j) noexcept -> int32_t {
        if (j < 0) return 0;
        if (j >= LAT_CELLS) return LAT_CELLS - 1;
        return j;
    };
    float peak = -1e30f;
    for (int32_t dj = -halfSearchCells; dj <= halfSearchCells; ++dj) {
        const int32_t jj = clampLat(c.latIdx + dj);
        for (int32_t di = -halfSearchCells; di <= halfSearchCells; ++di) {
            const int32_t ii = wrapLon(c.lonIdx + di);
            const float v = field[cellIndex(ii, jj)];
            if (v > peak) peak = v;
        }
    }
    return peak;
}

} // namespace aoc::map::gen
