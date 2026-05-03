/**
 * @file PostSim.cpp
 * @brief POST-SIM GEOLOGICAL PASSES implementation.
 */

#include "aoc/map/gen/PostSim.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"
#include "aoc/map/gen/MapGenContext.hpp"
#include "aoc/map/gen/Plate.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _OPENMP
#  include <omp.h>
#  define AOC_PS_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_PS_PARALLEL_FOR_ROWS
#endif

namespace aoc::map::gen {

void runPostSimPasses(MapGenContext& ctx) {
    HexGrid& grid                              = *ctx.grid;
    const int32_t width                        = ctx.width;
    const int32_t height                       = ctx.height;
    const bool cylSim                          = ctx.cylindrical;
    const std::vector<Plate>& plates           = *ctx.plates;
    const std::vector<SutureSeam>& sutureSeams = *ctx.sutureSeams;
    std::vector<float>& elevationMap           = *ctx.elevationMap;
    std::vector<float>& orogeny                = *ctx.orogeny;
    std::vector<float>& sediment               = *ctx.sediment;
    std::vector<float>& crustAgeTile           = *ctx.crustAgeTile;
    std::vector<uint8_t>& marginTypeTile       = *ctx.marginTypeTile;
    std::vector<uint8_t>& ophioliteMask        = *ctx.ophioliteMask;

    auto neighbourIdx = [&](int32_t col, int32_t row,
                             int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylSim, col, row, dir, outIdx);
    };

    // Pass 1: per-tile crust age.
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            const uint8_t pid = grid.plateId(idx);
            if (pid == 0xFFu) { continue; }
            if (pid >= plates.size()) { continue; }
            const Plate& p = plates[pid];
            float dx = (static_cast<float>(col) + 0.5f)
                       / static_cast<float>(width)  - p.cx;
            float dy = (static_cast<float>(row) + 0.5f)
                       / static_cast<float>(height) - p.cy;
            if (cylSim) {
                if (dx >  0.5f) { dx -= 1.0f; }
                if (dx < -0.5f) { dx += 1.0f; }
            }
            const float dist = std::sqrt(dx * dx + dy * dy);
            const float radial = std::clamp(dist / 0.40f, 0.0f, 1.0f);
            float age;
            if (p.landFraction > 0.40f) {
                age = p.crustAge * (1.0f - radial * 0.30f);
            } else {
                age = p.crustAge * (1.0f - radial * 0.60f);
            }
            crustAgeTile[static_cast<std::size_t>(idx)] = std::max(0.0f, age);
        }
    }

    // Pass 2: ophiolite suture marking.
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            const float wx = (static_cast<float>(col) + 0.5f)
                             / static_cast<float>(width);
            const float wy = (static_cast<float>(row) + 0.5f)
                             / static_cast<float>(height);
            for (const SutureSeam& s : sutureSeams) {
                float ddx = wx - s.x;
                float ddy = wy - s.y;
                if (cylSim) {
                    if (ddx >  0.5f) { ddx -= 1.0f; }
                    if (ddx < -0.5f) { ddx += 1.0f; }
                }
                const float dd = std::sqrt(ddx * ddx + ddy * ddy);
                if (dd < s.r * 0.45f) {
                    ophioliteMask[static_cast<std::size_t>(idx)] = 1;
                    break;
                }
            }
        }
    }

    // Pass 3: sediment yield + downhill deposition (2 passes).
    for (int32_t pass = 0; pass < 2; ++pass) {
#ifdef _OPENMP
        const int32_t nThreads = omp_get_max_threads();
#else
        const int32_t nThreads = 1;
#endif
        const int32_t sedTotal = width * height;
        std::vector<std::vector<float>> threadSed(
            static_cast<std::size_t>(nThreads),
            std::vector<float>(static_cast<std::size_t>(sedTotal), 0.0f));
        AOC_PS_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
#ifdef _OPENMP
            const int32_t tid = omp_get_thread_num();
#else
            const int32_t tid = 0;
#endif
            std::vector<float>& myBuf =
                threadSed[static_cast<std::size_t>(tid)];
            for (int32_t col = 0; col < width; ++col) {
                const int32_t idx = row * width + col;
                const float oro = orogeny[
                    static_cast<std::size_t>(idx)];
                if (oro < 0.08f) { continue; }
                const float yield = (oro - 0.05f) * 0.10f;
                int32_t bestIdx[3] = {-1, -1, -1};
                float   bestOro[3] = {1e9f, 1e9f, 1e9f};
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!neighbourIdx(col, row, d, nIdx)) { continue; }
                    const float nOro = orogeny[
                        static_cast<std::size_t>(nIdx)];
                    if (nOro >= oro) { continue; }
                    for (int32_t k = 0; k < 3; ++k) {
                        if (nOro < bestOro[k]) {
                            for (int32_t j = 2; j > k; --j) {
                                bestOro[j] = bestOro[j - 1];
                                bestIdx[j] = bestIdx[j - 1];
                            }
                            bestOro[k] = nOro;
                            bestIdx[k] = nIdx;
                            break;
                        }
                    }
                }
                int32_t targets = 0;
                for (int32_t k = 0; k < 3; ++k) {
                    if (bestIdx[k] >= 0) { ++targets; }
                }
                if (targets == 0) { continue; }
                const float perTarget = yield
                    / static_cast<float>(targets);
                for (int32_t k = 0; k < 3; ++k) {
                    if (bestIdx[k] < 0) { continue; }
                    myBuf[static_cast<std::size_t>(bestIdx[k])]
                        += perTarget;
                }
            }
        }
        for (const auto& buf : threadSed) {
            AOC_PS_PARALLEL_FOR_ROWS
            for (int32_t i = 0; i < sedTotal; ++i) {
                sediment[static_cast<std::size_t>(i)]
                    += buf[static_cast<std::size_t>(i)];
            }
        }
    }

    // Pass 4: foreland basin flexural loading.
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            const float oro = orogeny[
                static_cast<std::size_t>(idx)];
            if (oro >= 0.08f) { continue; }
            float nbMtnSum = 0.0f;
            int32_t nbMtnCount = 0;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!neighbourIdx(col, row, d, nIdx)) { continue; }
                const float nOro = orogeny[
                    static_cast<std::size_t>(nIdx)];
                if (nOro > 0.10f) {
                    nbMtnSum += nOro;
                    ++nbMtnCount;
                }
            }
            if (nbMtnCount > 0) {
                sediment[static_cast<std::size_t>(idx)]
                    += 0.04f * nbMtnSum;
            }
        }
    }

    // Pass 5: active vs passive margin classification.
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            if (elevationMap[static_cast<std::size_t>(idx)]
                    < 0.0f) { continue; }
            bool nearWater = false;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!neighbourIdx(col, row, d, nIdx)) { continue; }
                if (elevationMap[static_cast<std::size_t>(nIdx)]
                        < 0.0f) { nearWater = true; break; }
            }
            if (!nearWater) { continue; }
            const uint8_t myPid = grid.plateId(idx);
            if (myPid == 0xFFu || myPid >= plates.size()) { continue; }
            int32_t otherPid = -1;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!neighbourIdx(col, row, d, nIdx)) { continue; }
                const uint8_t pid = grid.plateId(nIdx);
                if (pid == 0xFFu) { continue; }
                if (pid != myPid) { otherPid = pid; break; }
            }
            if (otherPid < 0) {
                marginTypeTile[static_cast<std::size_t>(idx)] = 2;
                continue;
            }
            const Plate& A = plates[myPid];
            const Plate& B = plates[static_cast<std::size_t>(otherPid)];
            float bx = B.cx - A.cx;
            float by = B.cy - A.cy;
            if (cylSim) {
                if (bx >  0.5f) { bx -= 1.0f; }
                if (bx < -0.5f) { bx += 1.0f; }
            }
            const float bnLen = std::sqrt(bx * bx + by * by);
            if (bnLen < 1e-4f) { continue; }
            bx /= bnLen; by /= bnLen;
            const float relVx = A.vx - B.vx;
            const float relVy = A.vy - B.vy;
            const float closingRate = relVx * bx + relVy * by;
            if (closingRate > 0.04f) {
                marginTypeTile[static_cast<std::size_t>(idx)] = 1;
            } else {
                marginTypeTile[static_cast<std::size_t>(idx)] = 2;
            }
        }
    }

    // Pass 6: apply sediment + ophiolite uplift onto elevationMap.
    for (std::size_t i = 0; i < elevationMap.size(); ++i) {
        elevationMap[i] += sediment[i] * 0.55f;
        if (ophioliteMask[i] != 0) {
            elevationMap[i] += 0.06f;
        }
    }

    // Persist tile-level fields onto the grid.
    grid.setCrustAgeTile(crustAgeTile);
    grid.setSedimentDepth(sediment);
    grid.setMarginType(marginTypeTile);
}

} // namespace aoc::map::gen
