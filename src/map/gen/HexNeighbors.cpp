/**
 * @file HexNeighbors.cpp
 * @brief Hex neighbour lookup implementation.
 */

#include "aoc/map/gen/HexNeighbors.hpp"

namespace aoc::map::gen {

bool hexNeighbor(int32_t width, int32_t height, bool cylindrical,
                 int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
    const bool oddRow = (row & 1) != 0;
    static const int32_t DCOL_EVEN[6] = { +1,  0, -1, -1,  0, +1};
    static const int32_t DCOL_ODD[6]  = { +1, +1,  0, -1, -1,  0};
    static const int32_t DROW[6]      = {  0, -1, -1,  0, +1, +1};
    const int32_t dc = oddRow ? DCOL_ODD[dir] : DCOL_EVEN[dir];
    const int32_t dr = DROW[dir];
    int32_t nc = col + dc;
    int32_t nr = row + dr;
    if (cylindrical) {
        if (nc < 0)        { nc += width; }
        if (nc >= width)   { nc -= width; }
    } else {
        if (nc < 0 || nc >= width) { return false; }
    }
    if (nr < 0 || nr >= height) { return false; }
    outIdx = nr * width + nc;
    return true;
}

} // namespace aoc::map::gen
