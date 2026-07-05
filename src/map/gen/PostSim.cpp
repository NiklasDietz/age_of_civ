/**
 * @file PostSim.cpp
 * @brief POST-SIM GEOLOGICAL PASSES implementation.
 */

#include "aoc/map/gen/PostSim.hpp"

#include <cstdlib>

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

    // Per-tile crust age unsupported in legacy 2D state; SphereField
    // crustAgeMy is the live source. Tile output stays zero.
    std::fill(crustAgeTile.begin(), crustAgeTile.end(), 0.0f);
    (void)ophioliteMask;

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

    // 2026-05-04: passive-margin sediment transport. Real Earth: rivers
    // drain the continental interior toward the coast, depositing 5-15
    // km of sediment in passive-margin shelf prisms (Atlantic, Gulf of
    // Mexico). We approximate by collecting orogen sediment seaward
    // along passive-margin tiles -- each passive-margin tile receives a
    // bonus proportional to the average orogeny within 5 hex inland.
    // Only fires after pass 3+4 so `sediment` already has yield from
    // mountains; pass 6 (run after this) lifts the elevation by sediment.
    {
        const int32_t totalT = width * height;
        std::vector<float> shelfBonus(static_cast<std::size_t>(totalT), 0.0f);
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t idx = row * width + col;
                const std::size_t si = static_cast<std::size_t>(idx);
                // Only continental tiles flagged as passive margin
                // accumulate the sediment prism. They feed neighbouring
                // ocean tiles in pass-6 via the shelf-widening pass --
                // but the prism itself raises the LAND tile so coastal
                // plains stand a few metres above sea level.
                if (marginTypeTile[si] != 2) { continue; }
                // Walk 5 hex inland toward higher orogeny, summing
                // contribution. Inland distance approximated by sweeping
                // outward in 6 hex directions and averaging the highest
                // orogeny seen.
                float maxOroNearby = 0.0f;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t cc = col, rr = row;
                    for (int32_t step = 1; step <= 5; ++step) {
                        int32_t nIdx;
                        if (!neighbourIdx(cc, rr, d, nIdx)) { break; }
                        cc = nIdx % width;
                        rr = nIdx / width;
                        const float nOro = orogeny[
                            static_cast<std::size_t>(nIdx)];
                        if (nOro > maxOroNearby) {
                            maxOroNearby = nOro;
                        }
                    }
                }
                if (maxOroNearby > 0.04f) {
                    shelfBonus[si] = 0.06f * maxOroNearby;
                }
            }
        }
        for (std::size_t i = 0; i < sediment.size(); ++i) {
            sediment[i] += shelfBonus[i];
        }
    }

    // Pass 5: active vs passive margin classification. A coast tile
    // is ACTIVE (Andean-type: offshore trench, narrow shelf, coastal
    // cordillera) when a convergent plate boundary lies within
    // ACTIVE_MARGIN_RANGE_HEXES of it; otherwise PASSIVE
    // (Atlantic-type: the ocean-continent transition is NOT a plate
    // boundary -- wide shelf, sediment prism, no mountains). Passive
    // margins dominate real coastlines because most coasts date to a
    // supercontinent rifting event, not to the current boundary
    // network. Uses the boundaryTypeTile layer projected from the
    // SphereField raster (2026-07-05; replaces the every-coast-passive
    // stub). Known accepted bias: belts that went quiet in the final
    // epochs read passive even where mountains still stand.
    // Range 1: the boundaryTypeTile layer is already footprint-
    // aggregated (~1 hex of smear either side of the raster boundary
    // line), so one further ring puts the trench-to-coast reach at
    // ~2 hexes ~ 500 km -- Andean scale. 3 rings over-classified
    // active (36-56 % passive vs Earth's ~60-75 %).
    constexpr int32_t ACTIVE_MARGIN_RANGE_HEXES = 1;
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
            // BFS out to ACTIVE_MARGIN_RANGE_HEXES looking for a
            // convergent boundary tile. Range is small (3) so a
            // per-tile ring walk stays cheap.
            bool nearConvergent =
                (grid.boundaryTypeTile(idx) == 1u);
            for (int32_t d = 0; d < 6 && !nearConvergent; ++d) {
                int32_t cc = col;
                int32_t rr = row;
                for (int32_t step = 0;
                     step < ACTIVE_MARGIN_RANGE_HEXES && !nearConvergent;
                     ++step) {
                    int32_t nIdx;
                    if (!neighbourIdx(cc, rr, d, nIdx)) { break; }
                    cc = nIdx % width;
                    rr = nIdx / width;
                    if (grid.boundaryTypeTile(nIdx) == 1u) {
                        nearConvergent = true;
                    }
                }
            }
            marginTypeTile[static_cast<std::size_t>(idx)] =
                nearConvergent ? 1u : 2u;
        }
    }
    if (std::getenv("AOC_DUMP_MARGINS") != nullptr) {
        std::size_t nActive = 0, nPassive = 0;
        for (const uint8_t m : marginTypeTile) {
            if (m == 1u) ++nActive;
            else if (m == 2u) ++nPassive;
        }
        std::fprintf(stderr, "[margins] active=%zu passive=%zu (%.0f%% passive)\n",
            nActive, nPassive,
            100.0 * static_cast<double>(nPassive)
                / static_cast<double>(std::max<std::size_t>(1, nActive + nPassive)));
    }

    // Pass 6: apply sediment + margin-type elevation modifier.
    // marginTypeTile values: 0 interior, 1 active, 2 passive.
    (void)ophioliteMask;
    for (std::size_t i = 0; i < elevationMap.size(); ++i) {
        elevationMap[i] += sediment[i] * 0.55f;
        const uint8_t m = marginTypeTile[i];
        if (m == 2) {
            // Passive margin: thicken sediment prism, raise shelf so
            // tiles seaward of the coast stay slightly above water for a
            // few hexes (continental shelf ~50-200 km on Earth).
            elevationMap[i] += 0.025f;
        } else if (m == 1) {
            // Active margin: trench cut along the subduction front. The
            // overrider's coast itself stays high (already orogeny-fed),
            // but adjacent ocean tile drops as a trench. We can only
            // touch land tiles here; depress *toward* the threshold so
            // the next-out hex tends to fall below it.
            elevationMap[i] -= 0.015f;
        }
    }

    // 2026-05-04: shelf widening pass for passive margins. Land tiles
    // marked passive (m == 2) sit on the EDGE of the continent; we want
    // ocean tiles within 2 hexes seaward of those tiles to be raised
    // toward the water threshold so they read as ShallowWater shelf, not
    // deep Ocean. Active-margin (m == 1) coasts get the opposite: a
    // small extra depression on the seaward neighbour to deepen the
    // trench. Done as a single neighbour-sweep so we touch each tile at
    // most once per role.
    {
        const int32_t totalT = width * height;
        std::vector<float> elevDelta(static_cast<std::size_t>(totalT), 0.0f);
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t idx = row * width + col;
                const uint8_t m = marginTypeTile[
                    static_cast<std::size_t>(idx)];
                if (m == 0) { continue; }
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!neighbourIdx(col, row, d, nIdx)) { continue; }
                    if (elevationMap[
                            static_cast<std::size_t>(nIdx)] >= 0.0f) {
                        // Only modify ocean-side neighbours (negative
                        // pre-threshold elevation).
                        continue;
                    }
                    if (m == 2) {
                        elevDelta[static_cast<std::size_t>(nIdx)] += 0.04f;
                    } else if (m == 1) {
                        // 2026-07-05: -0.03 -> -0.06 now that active
                        // margins actually classify (trenches are the
                        // deepest bathymetry on Earth; the offshore
                        // drop is what visually separates Pacific-type
                        // from Atlantic-type coasts).
                        elevDelta[static_cast<std::size_t>(nIdx)] -= 0.06f;
                    }
                }
            }
        }
        for (std::size_t i = 0; i < elevationMap.size(); ++i) {
            elevationMap[i] += elevDelta[i];
        }
    }

    // 2026-05-04: GEOMAGNETIC ANOMALY STRIPES. Earth's magnetic field
    // reverses every ~250-500 ky. New oceanic crust freezes the
    // current polarity in remanent magnetism as it cools at mid-ocean
    // ridges. Result: alternating positive/negative magnetic stripes
    // parallel to spreading axes. We approximate by quantising
    // crustAgeTile by a "reversal period" and assigning alternating
    // polarity per band. Works on ANY tile (oceanic or continental)
    // but stripes only visible on oceanic crust where age varies
    // smoothly along plate-motion direction. Cost: 1 byte per tile.
    std::vector<int8_t> magneticPolarity(
        static_cast<std::size_t>(width * height), 0);
    {
        // Reversal period in our age-time-units. Earth: ~0.5 My; sim:
        // each epoch ~10 My, so a stripe per ~0.05 epochs would be too
        // fine. Use 1.0 unit period -> ~5 stripes per 5-My segment.
        constexpr float REVERSAL_PERIOD = 1.0f;
        for (std::size_t i = 0; i < magneticPolarity.size(); ++i) {
            const float age = crustAgeTile[i];
            if (age <= 0.0f) { continue; }
            const int32_t band = static_cast<int32_t>(
                std::floor(age / REVERSAL_PERIOD));
            magneticPolarity[i] = (band & 1) ? -1 : 1;
        }
    }

    // Persist tile-level fields onto the grid.
    grid.setCrustAgeTile(crustAgeTile);
    grid.setMagneticPolarity(std::move(magneticPolarity));
    grid.setSedimentDepth(sediment);
    grid.setMarginType(marginTypeTile);
}

} // namespace aoc::map::gen
